/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/Generator.h"
#include "griddyn/governors/GovernorTgov1.h"
#include "griddyn/simulation/Diagnostics.h"
#include "utilities/MatrixDataSparse.hpp"
#include <cmath>
#include <gtest/gtest.h>
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
}  // namespace

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
    invalidGovernor.set("t1", 0.0);
    EXPECT_THROW(invalidGovernor.dynInitializeA(0.0, 0), InvalidParameterValue);
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
