/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/CoreOwningPtr.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/griddyn-config.h"
#include "griddyn/simulation/FaultResetRecovery.h"
#include "griddyn/solvers/SolverInterface.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/optObjectFactory.h"
#endif

namespace {
class OrderingTestSimulation: public griddyn::GridDynSimulation {
  public:
    OffsetOrdering ordering() const { return default_ordering; }
};

class CachedRootTestSimulation: public griddyn::GridDynSimulation {
    count_t cachedGeneration = 0;
    double cachedInput = 0.0;

  public:
    CachedRootTestSimulation() { residCount = 1; }
    void rootTest([[maybe_unused]] const griddyn::IOdata& inputs,
                  const griddyn::StateData& stateData,
                  double roots[],
                  [[maybe_unused]] const griddyn::SolverMode& sMode) override
    {
        if (stateData.updateRequired(cachedGeneration)) {
            cachedInput = stateData.state[0];
            cachedGeneration = stateData.seqID;
        }
        roots[0] = cachedInput;
    }
};

class FaultResetTestSolver: public griddyn::SolverInterface {
  public:
    int initialConditionCalls = 0;
    int successfulCall = 0;
    int result;
    std::array<double, 1> state{};
    std::array<double, 1> derivative{};

    FaultResetTestSolver(griddyn::GridDynSimulation* simulation, int solveResult):
        SolverInterface(simulation, griddyn::cDaeSolverMode), result(solveResult)
    {
    }

    double* stateData() noexcept override { return state.data(); }
    double* derivData() noexcept override { return derivative.data(); }
    void getCurrentData() override {}
    int calcIC([[maybe_unused]] griddyn::CoreTime initialTime,
               [[maybe_unused]] griddyn::CoreTime tstep0,
               [[maybe_unused]] IcModes solverMode,
               [[maybe_unused]] bool constraints) override
    {
        ++initialConditionCalls;
        if (successfulCall > 0 && initialConditionCalls >= successfulCall) {
            return 0;
        }
        return result;
    }
};
}  // namespace

TEST(SimulationRootTests, EachInterpolatedRootCallbackRefreshesCachedInputs)
{
    CachedRootTestSimulation simulation;
    double state = 1.0;
    double derivative = 0.0;
    double root = 0.0;
    ASSERT_EQ(
        simulation.rootFindingFunction(1.0, &state, &derivative, &root, griddyn::cDaeSolverMode),
        0);
    EXPECT_DOUBLE_EQ(root, 1.0);
    state = -1.0;
    ASSERT_EQ(
        simulation.rootFindingFunction(1.0, &state, &derivative, &root, griddyn::cDaeSolverMode),
        0);
    EXPECT_DOUBLE_EQ(root, -1.0);
}

TEST(FaultResetRecoveryTests, StopsAfterFirstSuccessfulRecovery)
{
    griddyn::GridDynSimulation simulation;
    auto solver = std::make_shared<FaultResetTestSolver>(&simulation, 0);
    griddyn::faultResetRecovery recovery(&simulation, solver);

    EXPECT_EQ(recovery.attemptFix(), 0);
    EXPECT_EQ(recovery.attempts(), 1);
    EXPECT_EQ(solver->initialConditionCalls, 1);
}

TEST(FaultResetRecoveryTests, ExhaustedSolvesCannotReportSuccess)
{
    griddyn::GridDynSimulation simulation;
    auto solver = std::make_shared<FaultResetTestSolver>(&simulation, SOLVER_CONVERGENCE_ERROR);
    griddyn::faultResetRecovery recovery(&simulation, solver);

    EXPECT_EQ(recovery.attemptFix(), SOLVER_CONVERGENCE_ERROR);
    EXPECT_FALSE(recovery.hasMoreFixes());
    EXPECT_GT(solver->initialConditionCalls, 0);
    EXPECT_LT(recovery.attemptFix(), 0);
}

TEST(FaultResetRecoveryTests, VoltageSearchStopsAtFirstValidSolution)
{
    griddyn::GridDynSimulation simulation;
    auto solver = std::make_shared<FaultResetTestSolver>(&simulation, SOLVER_CONVERGENCE_ERROR);
    // The first four strategies make seven failed IC calls. The voltage-search
    // strategy then obtains a valid solution on its first trial.
    solver->successfulCall = 8;
    griddyn::faultResetRecovery recovery(&simulation, solver);

    EXPECT_EQ(recovery.attemptFix(), 0);
    EXPECT_EQ(recovery.attempts(), 5);
    EXPECT_EQ(solver->initialConditionCalls, 8);
}

TEST(SimulationOrderingTests, SupportedNamesSelectExpectedOrdering)
{
    using Ordering = griddyn::GridDynSimulation::OffsetOrdering;
    constexpr std::array<std::pair<std::string_view, Ordering>, 6> orderings{{
        {"mixed", Ordering::MIXED},
        {"grouped", Ordering::GROUPED},
        {"algebraic_grouped", Ordering::ALGEBRAIC_GROUPED},
        {"voltage_first", Ordering::VOLTAGE_FIRST},
        {"angle_first", Ordering::ANGLE_FIRST},
        {"differential_first", Ordering::DIFFERENTIAL_FIRST},
    }};

    OrderingTestSimulation simulation;
    for (const auto& [name, expected] : orderings) {
        SCOPED_TRACE(name);
        simulation.set("ordering", name);
        EXPECT_EQ(simulation.ordering(), expected);
    }
}

TEST(SimulationOrderingTests, UnknownNameIsRejected)
{
    OrderingTestSimulation simulation;
    EXPECT_THROW(simulation.set("ordering", "unknown_order"), griddyn::InvalidParameterValue);
}

namespace {
class CountingRootObject: public griddyn::CoreObject {
  public:
    int addCount = 0;
    std::vector<griddyn::CoreObject*> addedObjects;

    void add(griddyn::CoreObject* obj) override
    {
        if (obj == nullptr) {
            return;
        }

        ++addCount;
        obj->addOwningReference();
        obj->setParent(this);
        addedObjects.push_back(obj);
    }

    void remove(griddyn::CoreObject* obj) override
    {
        const auto foundObject = std::find(addedObjects.begin(), addedObjects.end(), obj);
        if (foundObject == addedObjects.end()) {
            return;
        }

        griddyn::removeReference(*foundObject, this);
        addedObjects.erase(foundObject);
    }

    ~CountingRootObject() override
    {
        for (auto* obj : addedObjects) {
            griddyn::removeReference(obj, this);
        }
    }
};

class FactoryTestGridObject: public griddyn::CoreObject {
  public:
    FactoryTestGridObject() = default;
    explicit FactoryTestGridObject(const std::string& objectName): CoreObject(objectName) {}
};
}  // namespace

TEST(CoreFactoryTests, PrepObjectsIgnoresInvalidRequests)
{
    CountingRootObject root;
    griddyn::TypeFactory<FactoryTestGridObject> factory("core-factory-prep-invalid-test", "object");

    factory.prepObjects(0, &root);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);

    factory.prepObjects(2, nullptr);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);
}

TEST(CoreFactoryTests, PrepObjectsCreatesNonEmptyHolderOnce)
{
    CountingRootObject root;
    griddyn::TypeFactory<FactoryTestGridObject> factory("core-factory-prep-test", "object");

    factory.prepObjects(3, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    griddyn::CoreOwningPtr<FactoryTestGridObject> object{factory.makeTypeObject()};
    ASSERT_TRUE(static_cast<bool>(object));
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(5, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 5U);

    std::vector<griddyn::CoreOwningPtr<FactoryTestGridObject>> objects;
    objects.emplace_back(factory.makeTypeObject());
    objects.emplace_back(factory.makeTypeObject());
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    objects.emplace_back(factory.makeTypeObject());
    EXPECT_EQ(root.addCount, 2);
    EXPECT_EQ(factory.remainingPrepped(), 2U);
}

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
namespace {
class FactoryTestOptObject: public griddyn::GridOptObject {
  public:
    FactoryTestOptObject() = default;
    explicit FactoryTestOptObject(griddyn::CoreObject* obj): source(obj) {}

    griddyn::CoreObject* source = nullptr;

    [[nodiscard]] griddyn::CoreObject* sourceObject() const override { return source; }

    void add(griddyn::CoreObject* obj) override { source = obj; }
};
}  // namespace

TEST(OptimizationFactoryTests, PrepObjectsReusesAttachedHolder)
{
    CountingRootObject root;
    FactoryTestGridObject gridObject;
    griddyn::OptObjectFactory<FactoryTestOptObject, FactoryTestGridObject> factory(
        "factory-prep-test", "object");

    factory.prepObjects(3, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    griddyn::CoreOwningPtr<FactoryTestOptObject> optObject{factory.makeTypeObject(&gridObject)};
    ASSERT_TRUE(static_cast<bool>(optObject));
    EXPECT_EQ(optObject->source, &gridObject);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(3, &root);
    EXPECT_EQ(root.addCount, 2);
    EXPECT_EQ(factory.remainingPrepped(), 3U);
}

TEST(OptimizationFactoryTests, PrepObjectsIgnoresInvalidRequests)
{
    CountingRootObject root;
    griddyn::OptObjectFactory<FactoryTestOptObject, FactoryTestGridObject> factory(
        "factory-prep-invalid-test", "object");

    factory.prepObjects(0, &root);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);

    factory.prepObjects(2, nullptr);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);
}
#endif
