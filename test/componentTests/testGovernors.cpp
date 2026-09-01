/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModel6.h"
#include "griddyn/governors/GovernorHydro.h"
#include "griddyn/governors/GovernorHygov.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/governors/GovernorTgov1.h"
#include "griddyn/simulation/Diagnostics.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <print>
#include <string>
#include <vector>
// test case for CoreObject object

#define GOVERNOR_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/governor_tests/"

using namespace griddyn;

class GovernorTests: public GridDynSimulationTestFixture, public ::testing::Test {};

namespace {
void configureTgov1(governors::GovernorTgov1& governor)
{
    governor.set("r", 0.05);
    governor.set("t1", 0.05);
    governor.set("pmax", 1.05);
    governor.set("pmin", 0.30);
    governor.set("t2", 1.0);
    governor.set("t3", 2.1);
    governor.set("dt", 0.1);
}

void configureIeeeG1(governors::GovernorIeeeG1& governor)
{
    governor.set("k", 20.0);
    governor.set("t1", 0.2);
    governor.set("t2", 0.05);
    governor.set("t3", 0.1);
    governor.set("uo", 0.3);
    governor.set("uc", -0.25);
    governor.set("pmax", 1.2);
    governor.set("pmin", 0.1);
    governor.set("t4", 0.4);
    governor.set("k1", 0.3);
    governor.set("k2", 0.1);
    governor.set("t5", 0.0);
    governor.set("k3", 0.2);
    governor.set("k4", 0.1);
    governor.set("t6", 0.5);
    governor.set("k5", 0.1);
    governor.set("k6", 0.05);
    governor.set("t7", 0.2);
    governor.set("k7", 0.1);
    governor.set("k8", 0.05);
}

void configureHydro(governors::GovernorHydro& governor)
{
    governor.set("k", 5.0);
    governor.set("t1", 0.25);
    governor.set("t2", 0.0);
    governor.set("t3", 0.1);
    governor.set("tw", 0.04);
    governor.set("pmax", 2.0);
    governor.set("pmin", 0.0);
}

void configureHygov(governors::GovernorHygov& governor)
{
    governor.set("r", 0.05);
    governor.set("temporarydroop", 0.3);
    governor.set("tr", 5.0);
    governor.set("tf", 0.05);
    governor.set("tg", 0.5);
    governor.set("velm", 0.2);
    governor.set("gmax", 0.9);
    governor.set("gmin", 0.0);
    governor.set("tw", 1.25);
    governor.set("at", 1.2);
    governor.set("dturb", 0.2);
    governor.set("qnl", 0.08);
}
}  // namespace

TEST(GovernorModelTests, HydroMatchesCgmesSimpleHydroEquations)
{
    governors::GovernorHydro governor;
    configureHydro(governor);
    governor.dynInitializeA(0.0, 0);
    EXPECT_DOUBLE_EQ(governor.get("t4"), 0.04);

    IOdata inputs{1.0, 0.8};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.8}, fieldSet);

    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 4U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 0.0);
    EXPECT_DOUBLE_EQ(initialized[2], 0.0);
    EXPECT_DOUBLE_EQ(initialized[3], 0.8);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.8);

    std::vector<double> state{0.75, 0.01, 0.02, 0.81};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 0.99;

    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.08, 1e-14);
    EXPECT_NEAR(derivative[2], 0.30, 1e-14);
    EXPECT_NEAR(derivative[3], -0.90, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.06, 1e-14);
    EXPECT_NEAR(residual[1], -0.08, 1e-14);
    EXPECT_NEAR(residual[2], 0.30, 1e-14);
    EXPECT_NEAR(residual[3], -0.90, 1e-14);
}

TEST(GovernorModelTests, HygovMatchesOpenIpslInitializationAndPerturbedEquations)
{
    governors::GovernorHygov governor;
    configureHygov(governor);
    governor.dynInitializeA(0.0, 0);

    IOdata inputs{1.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.4}, fieldSet);

    const double initialFlow = (0.4 / 1.2) + 0.08;
    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 5U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.4);
    EXPECT_DOUBLE_EQ(initialized[1], 0.0);
    EXPECT_NEAR(initialized[2], initialFlow, 1e-14);
    EXPECT_NEAR(initialized[3], initialFlow, 1e-14);
    EXPECT_NEAR(initialized[4], initialFlow, 1e-14);
    EXPECT_NEAR(fieldSet[govpSetInLocation], 0.05 * initialFlow, 1e-14);

    std::vector<double> state{0.42, 0.02, 0.40, 0.41, 0.42};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 0.99;
    inputs[govpSetInLocation] = fieldSet[govpSetInLocation];

    const double governorError =
        fieldSet[govpSetInLocation] - (inputs[govOmegaInLocation] - 1.0) - (0.05 * state[2]);
    const double filterDerivative = (governorError - state[1]) / 0.05;
    const double gateRate =
        std::clamp(((5.0 * filterDerivative) + state[1]) / (0.3 * 5.0), -0.2, 0.2);
    const double head = (state[4] / state[3]) * (state[4] / state[3]);

    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], filterDerivative, 1e-14);
    EXPECT_NEAR(derivative[2], gateRate, 1e-14);
    EXPECT_NEAR(derivative[3], (state[2] - state[3]) / 0.5, 1e-14);
    EXPECT_NEAR(derivative[4], (1.0 - head) / 1.25, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    const double pmech =
        (1.2 * head * (state[4] - 0.08)) -
        (0.2 * (inputs[govOmegaInLocation] - 1.0) * state[3]);
    EXPECT_NEAR(residual[0], pmech - state[0], 1e-14);
}

TEST(GovernorModelTests, HygovEnforcesVelocityAndGatePositionLimits)
{
    governors::GovernorHygov governor;
    configureHygov(governor);
    governor.dynInitializeA(0.0, 0);

    IOdata inputs{0.95, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.4}, fieldSet);

    std::vector<double> state{0.4, 0.0, 0.4, 0.4, 0.4};
    std::vector<double> stateDerivative(state.size(), 0.0);
    std::vector<double> derivative(state.size(), 0.0);

    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.2);

    state[2] = 0.9;
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);

    inputs[govOmegaInLocation] = 1.05;
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], -0.2);

    state[2] = 0.0;
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
}

TEST(GovernorModelTests, HygovFactoryCloneAndParameterValidation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("governor", "hygov"));
    auto* governor = dynamic_cast<governors::GovernorHygov*>(object.get());
    ASSERT_NE(governor, nullptr);
    EXPECT_DOUBLE_EQ(governor->get("tr"), 5.0);
    EXPECT_DOUBLE_EQ(governor->get("t1"), 5.0);
    configureHygov(*governor);

    std::unique_ptr<CoreObject> clonedObject(governor->clone());
    auto* clone = dynamic_cast<governors::GovernorHygov*>(clonedObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("r"), 0.05);
    EXPECT_DOUBLE_EQ(clone->get("temporarydroop"), 0.3);
    EXPECT_DOUBLE_EQ(clone->get("velm"), 0.2);
    EXPECT_DOUBLE_EQ(clone->get("qnl"), 0.08);

    EXPECT_ANY_THROW(governor->set("tf", 0.0));
    EXPECT_ANY_THROW(governor->set("tg", 0.0));
    EXPECT_ANY_THROW(governor->set("tw", 0.0));
    EXPECT_ANY_THROW(governor->set("temporarydroop", 0.0));
    governor->set("gmax", -0.1);
    EXPECT_ANY_THROW(governor->dynInitializeA(0.0, 0));

    governors::GovernorHygov singularGovernor;
    singularGovernor.set("qnl", 0.0);
    singularGovernor.dynInitializeA(0.0, 0);
    IOdata fieldSet(2, 0.0);
    EXPECT_ANY_THROW(singularGovernor.dynInitializeB({1.0, 0.0}, {0.0}, fieldSet));
}

TEST(GovernorModelTests, IeeeG1MatchesAndesInitializationAndPerturbedEquations)
{
    governors::GovernorIeeeG1 governor;
    configureIeeeG1(governor);
    governor.dynInitializeA(0.0, 0);
    governor.setOutputInitializationTarget(governors::GovernorIeeeG1::lpOutput, 0.3);

    IOdata inputs{1.0, 0.7};
    IOdata desiredOutput{0.7};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, desiredOutput, fieldSet);

    // Frozen ANDES initializes the lead-lag state to zero and the valve and
    // all non-bypassed turbine stages to tm0 + tm02. T5=0 is an exact bypass.
    const auto& initialized = governor.getStates();
    const std::vector<double> expectedInitial{0.7, 0.3, 0.0, 1.0, 1.0, 1.0, 1.0};
    ASSERT_EQ(initialized.size(), expectedInitial.size());
    for (std::size_t index = 0; index < expectedInitial.size(); ++index) {
        EXPECT_NEAR(initialized[index], expectedInitial[index], 1e-14) << index;
    }
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 1.0);

    // Local order is [PHP, PLP, LL_x, valve, L4, L6, L7]. These references
    // are a direct evaluation of frozen ANDES IEEEG1 away from equilibrium.
    std::vector<double> state{0.69, 0.31, 0.02, 0.90, 0.85, 0.80, 0.75};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 0.99;

    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], -0.05, 1e-14);
    EXPECT_NEAR(derivative[3], 0.30, 1e-14);
    EXPECT_NEAR(derivative[4], 0.125, 1e-14);
    EXPECT_NEAR(derivative[5], 0.10, 1e-14);
    EXPECT_NEAR(derivative[6], 0.25, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.11, 1e-14);
    EXPECT_NEAR(residual[1], -0.0625, 1e-14);
    for (std::size_t index = 2; index < state.size(); ++index) {
        EXPECT_NEAR(residual[index], derivative[index], 1e-14) << index;
    }
}

TEST(GovernorModelTests, IeeeG1RateAndAntiWindupLimitTransitions)
{
    governors::GovernorIeeeG1 governor;
    configureIeeeG1(governor);
    governor.dynInitializeA(0.0, 0);
    governor.setRootOffset(0, cLocalSolverMode);
    governor.setOutputInitializationTarget(governors::GovernorIeeeG1::lpOutput, 0.3);

    // A 10% underspeed drives the valve rate upward at Pmax, so the
    // non-windup limiter must hold the valve state.
    IOdata inputs{0.90, 0.7};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.7}, fieldSet);

    std::vector<double> state{0.7, 0.3, 0.0, 1.2, 1.0, 1.0, 1.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);

    std::array<double, 2> roots{};
    governor.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(roots[1], 0.0);
    governor.rootTrigger(0.0, inputs, {1, 1}, cLocalSolverMode);

    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[3], 0.0);

    inputs[govOmegaInLocation] = 1.10;
    governor.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[1], 0.0);
    governor.rootTrigger(0.0, inputs, {1, 1}, cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[3], -0.25);
}

TEST(GovernorModelTests, IeeeG1FactoryCloneAndParameterValidation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("governor", "ieeeg1"));
    auto* governor = dynamic_cast<governors::GovernorIeeeG1*>(object.get());
    ASSERT_NE(governor, nullptr);
    configureIeeeG1(*governor);

    std::unique_ptr<CoreObject> clonedObject(governor->clone());
    auto* clone = dynamic_cast<governors::GovernorIeeeG1*>(clonedObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("uo"), 0.3);
    EXPECT_DOUBLE_EQ(clone->get("t7"), 0.2);
    EXPECT_DOUBLE_EQ(clone->get("k8"), 0.05);

    EXPECT_ANY_THROW(governor->set("t3", 0.0));
    EXPECT_ANY_THROW(governor->set("uo", -0.1));
    EXPECT_ANY_THROW(governor->set("uc", 0.1));
    EXPECT_ANY_THROW(governor->set("k4", -0.1));
    governor->set("pmax", 0.05);
    EXPECT_ANY_THROW(governor->dynInitializeA(0.0, 0));
}

TEST(GovernorModelTests, Tgov1MatchesAndesInitializationAndPerturbedEquations)
{
    governors::GovernorTgov1 governor;
    configureTgov1(governor);
    governor.dynInitializeA(0.0, 0);

    IOdata inputs{1.0, 0.8};
    IOdata desiredOutput{0.8};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, desiredOutput, fieldSet);

    // ANDES initializes the valve, lead-lag output, and mechanical output to
    // tm0 when the speed deviation is zero.
    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 3U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 0.8);
    EXPECT_DOUBLE_EQ(initialized[2], 0.8);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.8);

    // Local storage order is [pmech, lead-lag state, valve state].  The
    // expected values are a direct evaluation of ANDES TGOV1Model away from
    // equilibrium, using its 1/R gain and T2/T3 lead-lag convention.
    std::vector<double> state{0.85, 0.75, 0.80};
    std::vector<double> stateDerivative(3, 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 1.01;
    inputs[govpSetInLocation] = 0.90;

    std::vector<double> derivative(3, 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 41.0 / 42.0, 1e-14);
    EXPECT_NEAR(derivative[2], -2.0, 1e-14);

    std::vector<double> residual(3, 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.101, 1e-14);
    EXPECT_NEAR(residual[1], 41.0 / 42.0, 1e-14);
    EXPECT_NEAR(residual[2], -2.0, 1e-14);
}

TEST(GovernorModelTests, Tgov1AppliesValveLimitsAndRejectsSingularParameters)
{
    governors::GovernorTgov1 governor;
    configureTgov1(governor);
    governor.dynInitializeA(0.0, 0);
    governor.setRootOffset(0, cLocalSolverMode);

    IOdata inputs{1.0, 0.8};
    IOdata desiredOutput{0.8};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, desiredOutput, fieldSet);

    std::vector<double> state{0.8, 0.8, 1.05};
    std::vector<double> stateDerivative(3, 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 0.99;
    inputs[govpSetInLocation] = 0.9;

    double root = 0.0;
    governor.rootTest(inputs, emptyStateData, &root, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(root, 0.0);
    governor.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);

    std::vector<double> derivative(3, 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    EXPECT_NEAR(derivative[1], 0.25 / 2.1, 1e-14);

    // The limited valve is an algebraic hold on its differential state.  Its
    // Jacobian must retain the turbine lead-lag state equation while removing
    // the unconstrained valve-input derivatives.
    MatrixDataSparse<double> jacobian;
    IOlocs inputLocs{3, 4};
    governor.jacobianElements(inputs, emptyStateData, jacobian, inputLocs, cLocalSolverMode);
    EXPECT_NEAR(jacobian.at(1, 1), -(1.0 / 2.1) - 1.0, 1e-14);
    EXPECT_NEAR(jacobian.at(1, 2), 1.0 / 2.1, 1e-14);
    EXPECT_DOUBLE_EQ(jacobian.at(2, 2), -1.0);

    governors::GovernorTgov1 invalidGovernor;
    configureTgov1(invalidGovernor);
    EXPECT_ANY_THROW(invalidGovernor.set("t1", 0.0));
    EXPECT_DOUBLE_EQ(invalidGovernor.get("t1"), 0.05);

    // Valve-limit ordering is deferred until the complete record is loaded.
    invalidGovernor.set("pmax", 0.2);
    EXPECT_ANY_THROW(invalidGovernor.dynInitializeA(0.0, 0));
}

TEST(GovernorModelTests, Tgov1SpeedStepTrajectoryMatchesAndesEquations)
{
    governors::GovernorTgov1 governor;
    configureTgov1(governor);
    governor.dynInitializeA(0.0, 0);

    IOdata inputs{1.0, 0.8};
    IOdata desiredOutput{0.8};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, desiredOutput, fieldSet);
    inputs[govOmegaInLocation] = 0.99;

    // Reference values are the 0.1, 0.5, and 1.0 s samples from a
    // high-accuracy integration of the frozen ANDES v2.0.0 TGOV1Model
    // equations for this speed step.  The GridDyn explicit local step is
    // deliberately small enough that its discretization error stays below
    // the declared tolerance.
    constexpr double step = 1e-4;
    for (int index = 1; index <= 10000; ++index) {
        governor.timestep(index * step, inputs, cLocalSolverMode);
        const auto& state = governor.getStates();
        if (index == 1000) {
            EXPECT_NEAR(state[0], 0.7264889256014327, 2e-4);
            EXPECT_NEAR(state[1], 0.7254889256014327, 2e-4);
            EXPECT_NEAR(state[2], 0.9729329433526738, 2e-4);
        } else if (index == 5000) {
            EXPECT_NEAR(state[0], 0.7626440998942028, 2e-4);
            EXPECT_NEAR(state[1], 0.7616440998942028, 2e-4);
            EXPECT_NEAR(state[2], 0.9999909200140873, 2e-4);
        } else if (index == 10000) {
            EXPECT_NEAR(state[0], 0.8131414647372036, 2e-4);
            EXPECT_NEAR(state[1], 0.8121414647372036, 2e-4);
            EXPECT_NEAR(state[2], 0.9999999995877648, 2e-4);
        }
    }
}

TEST_F(GovernorTests, GovStabilityTest)
{
    std::string fileName = std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml");

    GridDynSimulation::resetObjectCounters();
    gds = readSimXMLFile(fileName);
    auto* gen = static_cast<Generator*>(gds->findByUserID("gen", 2));
    ASSERT_NE(gen, nullptr);

    auto cof = CoreObjectFactory::instance();
    CoreObject* obj = cof->createObject("governor", "basic");
    ASSERT_NE(obj, nullptr);

    gen->add(obj);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);
    gds->run(0.005);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);

    gds->run(400.0);
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    const std::vector<double> state = gds->getState();
    gds->run(500.0);
    gds->saveRecorders();
    const std::vector<double> finalState = gds->getState();

    // check for stability
    ASSERT_EQ(state.size(), finalState.size());
    int ncnt = 0;
    const double referenceAngle = finalState[0];
    for (size_t kk = 0; kk < state.size(); ++kk) {
        if (std::abs(state[kk] - finalState[kk]) > 0.0001) {
            if (std::abs(state[kk] - finalState[kk] + referenceAngle) >
                0.005 * ((std::max)(state[kk], finalState[kk]))) {
                std::println("state[{}] orig={:f} new={:f}", kk, state[kk], finalState[kk]);
                ncnt++;
            }
        }
    }
    EXPECT_EQ(ncnt, 0);
}

TEST_F(GovernorTests, Tgov1AnalyticJacobianMatchesFiniteDifferences)
{
    std::string fileName = std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml");

    GridDynSimulation::resetObjectCounters();
    gds = readSimXMLFile(fileName);
    auto* gen = static_cast<Generator*>(gds->findByUserID("gen", 2));
    ASSERT_NE(gen, nullptr);

    auto* governor = new governors::GovernorTgov1();
    configureTgov1(*governor);
    gen->add(governor);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);
}

TEST_F(GovernorTests, IeeeG1AnalyticJacobianMatchesFiniteDifferences)
{
    const std::string fileName = std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml");

    GridDynSimulation::resetObjectCounters();
    gds = readSimXMLFile(fileName);
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->findByUserID("gen", 2));
    ASSERT_NE(generator, nullptr);

    auto* governor = new governors::GovernorIeeeG1();
    configureIeeeG1(*governor);
    governor->set("pmax", 2.0);
    governor->set("pmin", 0.0);
    for (const auto* coefficient : {"k2", "k4", "k6", "k8"}) {
        governor->set(coefficient, 0.0);
    }
    generator->add(governor);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);
}

TEST_F(GovernorTests, IeeeG1CouplesMixedGeneratorModelsExactlyOnce)
{
    const std::string fileName = std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml");

    GridDynSimulation::resetObjectCounters();
    gds = readSimXMLFile(fileName);
    auto* primary = dynamic_cast<DynamicGenerator*>(gds->findByUserID("gen", 1));
    auto* secondary = dynamic_cast<DynamicGenerator*>(gds->findByUserID("gen", 2));
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);

    // The lossless fixture dispatches 0.3 pu from gen1 and 1.2 pu from gen2.
    // Use different synchronous-machine classes to verify that the shared
    // governor connection is independent of the generator-model type.
    secondary->add(new genmodels::GenModel6());
    auto* unusedLocalGovernor = new governors::GovernorTgov1();
    configureTgov1(*unusedLocalGovernor);
    unusedLocalGovernor->set("pmax", 2.0);
    unusedLocalGovernor->set("pmin", 0.0);
    secondary->add(unusedLocalGovernor);
    auto* governor = new governors::GovernorIeeeG1("cross_compound_ieeeg1");
    configureIeeeG1(*governor);
    governor->set("pmax", 2.0);
    governor->set("pmin", 0.0);
    for (const auto* coefficient : {"k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8"}) {
        governor->set(coefficient, 0.0);
    }
    governor->set("k1", 0.2);
    governor->set("k2", 0.8);
    primary->add(governor);
    secondary->setMechanicalPowerSource(governor, governors::GovernorIeeeG1::lpOutput);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(primary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(primary->getMechanicalPowerOutput(), governors::GovernorIeeeG1::hpOutput);
    EXPECT_EQ(secondary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerOutput(), governors::GovernorIeeeG1::lpOutput);
    EXPECT_EQ(secondary->find("governor"), unusedLocalGovernor);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);
}
