/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/Stabilizer.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/stabilizers/StabilizerIEEEST.h"
#include "griddyn/stabilizers/StabilizerIee2st.h"
#include "griddyn/stabilizers/StabilizerPss2a.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include "griddyn/stabilizers/StabilizerStab3.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace griddyn;

class StabilizerTests: public GridDynSimulationTestFixture, public ::testing::Test {};

namespace {
void configureSt2cut(stabilizers::StabilizerST2CUT& stabilizer)
{
    stabilizer.set("mode", 1.0);
    stabilizer.set("mode2", 0.0);
    stabilizer.set("k1", 1.2);
    stabilizer.set("k2", 0.8);
    stabilizer.set("t1", 0.1);
    stabilizer.set("t2", 0.2);
    stabilizer.set("t3", 3.0);
    stabilizer.set("t4", 1.0);
    stabilizer.set("t5", 0.5);
    stabilizer.set("t6", 0.1);
    stabilizer.set("t7", 0.3);
    stabilizer.set("t8", 0.15);
    stabilizer.set("t9", 0.2);
    stabilizer.set("t10", 0.1);
    stabilizer.set("lsmax", 0.05);
    stabilizer.set("lsmin", -0.05);
    stabilizer.set("vcu", 0.1);
    stabilizer.set("vcl", -0.1);
}

void configureIeeest(stabilizers::StabilizerIEEEST& stabilizer)
{
    stabilizer.set("mode", 1.0);
    stabilizer.set("busr", 0.0);
    stabilizer.set("a1", 0.2);
    stabilizer.set("a2", 0.1);
    stabilizer.set("a3", 0.3);
    stabilizer.set("a4", 0.2);
    stabilizer.set("a5", 0.05);
    stabilizer.set("a6", 0.02);
    stabilizer.set("t1", 0.1);
    stabilizer.set("t2", 0.2);
    stabilizer.set("t3", 0.15);
    stabilizer.set("t4", 0.3);
    stabilizer.set("t5", 0.6);
    stabilizer.set("t6", 1.2);
    stabilizer.set("ks", 2.0);
    stabilizer.set("lsmax", 0.05);
    stabilizer.set("lsmin", -0.05);
    stabilizer.set("vcu", 0.1);
    stabilizer.set("vcl", -0.1);
}

void configureIee2st(stabilizers::StabilizerIee2st& stabilizer)
{
    stabilizer.set("mode1", 1.0);
    stabilizer.set("mode2", 3.0);
    stabilizer.set("k1", 2.0);
    stabilizer.set("k2", 3.0);
    stabilizer.set("t1", 0.1);
    stabilizer.set("t2", 0.2);
    stabilizer.set("t3", 3.0);
    stabilizer.set("t4", 1.0);
    stabilizer.set("t5", 0.5);
    stabilizer.set("t6", 0.1);
    stabilizer.set("t7", 0.3);
    stabilizer.set("t8", 0.15);
    stabilizer.set("t9", 0.2);
    stabilizer.set("t10", 0.1);
    stabilizer.set("lsmax", 2.0);
    stabilizer.set("lsmin", -2.0);
    stabilizer.set("vcu", 0.1);
    stabilizer.set("vcl", -0.1);
}

void configurePss2a(stabilizers::StabilizerPss2a& stabilizer)
{
    stabilizer.set("mode1", 1.0);
    stabilizer.set("mode2", 3.0);
    stabilizer.set("tw1", 2.0);
    stabilizer.set("tw2", 2.0);
    stabilizer.set("t6", 0.5);
    stabilizer.set("tw3", 2.0);
    stabilizer.set("tw4", 2.0);
    stabilizer.set("t7", 0.4);
    stabilizer.set("ks2", 0.8);
    stabilizer.set("ks3", 1.1);
    stabilizer.set("t8", 0.5);
    stabilizer.set("t9", 0.1);
    stabilizer.set("ks1", 4.0);
    stabilizer.set("t1", 0.15);
    stabilizer.set("t2", 0.025);
    stabilizer.set("t3", 0.15);
    stabilizer.set("t4", 0.025);
    stabilizer.set("vstmax", 2.0);
    stabilizer.set("vstmin", -2.0);
}

void configureStab3(stabilizers::StabilizerStab3& stabilizer)
{
    stabilizer.set("tt", 0.1);
    stabilizer.set("tx1", 0.2);
    stabilizer.set("tx2", 0.3);
    stabilizer.set("kx", 2.0);
    stabilizer.set("vlim", 2.0);
}

void runAttachedStabilizerLoadPulseTest(
    std::unique_ptr<GridDynSimulation>& simulation,
    const std::string& modelName,
    const std::function<void(DynamicGenerator*)>& attachStabilizer)
{
    SCOPED_TRACE(modelName);
    simulation =
        readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(simulation->getGen(0));
    ASSERT_NE(generator, nullptr);
    attachStabilizer(generator);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    const auto initialState = simulation->getState();

    ASSERT_EQ(simulation->run(2.0), 0);
    const auto disturbedState = simulation->getState();
    bool respondedToLoadPulse = false;
    ASSERT_EQ(initialState.size(), disturbedState.size());
    for (std::size_t index = 0; index < initialState.size(); ++index) {
        if (std::abs(disturbedState[index] - initialState[index]) > 1e-6) {
            respondedToLoadPulse = true;
            break;
        }
    }
    EXPECT_TRUE(respondedToLoadPulse);

    ASSERT_EQ(simulation->run(18.0), 0);
    const auto recoveredState = simulation->getState();
    ASSERT_EQ(simulation->run(20.0), 0);
    const auto finalState = simulation->getState();
    ASSERT_EQ(recoveredState.size(), finalState.size());
    // The first state is the reference generator angle; compare the remaining states for settling.
    for (std::size_t index = 0; index < finalState.size(); ++index) {
        EXPECT_TRUE(std::isfinite(finalState[index]));
        EXPECT_LT(std::abs(finalState[index]), 10.0);
        if (index != 0) {
            EXPECT_NEAR(finalState[index], recoveredState[index], 0.005);
        }
    }
}
}  // namespace

TEST(StabilizerModelTests, IeeestMatchesAndesInitializationAndPerturbedEquations)
{
    stabilizers::StabilizerIEEEST stabilizer;
    configureIeeest(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);

    const auto& initialized = stabilizer.getStates();
    ASSERT_EQ(initialized.size(), 8U);
    for (const auto value : initialized) {
        EXPECT_NEAR(value, 0.0, 1e-14);
    }

    // State order is [VSS, F1_x, F1_y, F2_x, F2_y, LL1_x, LL2_x, WO_x].
    // These are direct evaluations of frozen ANDES IEEEST equations.
    std::vector<double> state{0.0, 0.02, -0.01, 0.03, -0.02, 0.04, -0.03, 0.05};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[pssOmegaInLocation] = 1.01;

    std::vector<double> derivative(state.size(), 0.0);
    stabilizer.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.16, 1e-14);
    EXPECT_NEAR(derivative[2], 0.02, 1e-14);
    EXPECT_NEAR(derivative[3], 0.005, 1e-14);
    EXPECT_NEAR(derivative[4], 0.03, 1e-14);
    EXPECT_NEAR(derivative[5], -0.292, 1e-14);
    EXPECT_NEAR(derivative[6], 0.136, 1e-14);
    EXPECT_NEAR(derivative[7], -0.057666666666666665, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.0346, 1e-14);
    for (std::size_t index = 1; index < residual.size(); ++index) {
        EXPECT_NEAR(residual[index], derivative[index], 1e-14) << index;
    }
}

TEST(StabilizerModelTests, IeeestLimitsVoltageGateAndRetainsAndesZeroBypasses)
{
    stabilizers::StabilizerIEEEST stabilizer;
    configureIeeest(stabilizer);
    stabilizer.set("lsmax", 0.03);
    stabilizer.set("lsmin", -0.03);
    stabilizer.dynInitializeA(0.0, 0);
    stabilizer.setRootOffset(0, cLocalSolverMode);
    IOdata inputs{1.01, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    std::vector<double> state{0.0, 0.02, -0.01, 0.03, -0.02, 0.04, -0.03, 0.05};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);

    std::vector<double> residual(state.size(), 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.03, 1e-14);
    std::vector<double> roots(4, 0.0);
    stabilizer.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[1], 0.0);
    stabilizer.rootTrigger(0.0, inputs, {0, 1, 0, 0}, cLocalSolverMode);
    EXPECT_TRUE(stabilizer.checkFlag(stabilizers::StabilizerIEEEST::OUTPUT_LIMITED));
    EXPECT_FALSE(stabilizer.checkFlag(stabilizers::StabilizerIEEEST::OUTPUT_LIMIT_HIGH));

    inputs[pssVoltageInLocation] = 1.2;
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.0, 1e-14);
    stabilizer.rootTrigger(0.0, inputs, {0, 0, 0, 1}, cLocalSolverMode);
    EXPECT_TRUE(stabilizer.checkFlag(stabilizers::StabilizerIEEEST::VOLTAGE_GATED));

    stabilizers::StabilizerIEEEST bypass;
    bypass.set("mode", 3.0);
    bypass.set("a1", 0.0);
    bypass.set("a2", 0.0);
    bypass.set("a4", 0.0);
    bypass.set("t2", 0.0);
    bypass.set("t3", 0.0);
    bypass.set("t4", 0.75);
    bypass.set("t5", 1.0);
    bypass.set("t6", 4.2);
    bypass.set("ks", -2.0);
    bypass.dynInitializeA(0.0, 0);
    IOdata bypassFields;
    bypass.dynInitializeB({1.0, 1.0, 0.8, 0.7}, {0.0}, bypassFields);
    EXPECT_EQ(bypass.getStates().size(), 3U);
    EXPECT_NEAR(bypass.getStates()[1], 0.7, 1e-14);
    EXPECT_NEAR(bypass.getStates()[2], -1.4, 1e-14);

    EXPECT_ANY_THROW(stabilizer.set("mode", 2.0));
    EXPECT_ANY_THROW(stabilizer.set("busr", 9.0));
    EXPECT_ANY_THROW(stabilizer.set("t6", 0.0));
}

TEST(StabilizerModelTests, IeeestFactoryCloneAndParameterValidation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("pss", "ieeest"));
    auto* stabilizer = dynamic_cast<stabilizers::StabilizerIEEEST*>(object.get());
    ASSERT_NE(stabilizer, nullptr);
    stabilizer->set("ks", 2.5);
    std::unique_ptr<CoreObject> clonedObject(stabilizer->clone());
    auto* clone = dynamic_cast<stabilizers::StabilizerIEEEST*>(clonedObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("ks"), 2.5);

    EXPECT_ANY_THROW(stabilizer->set("a1", -0.1));
    EXPECT_ANY_THROW(stabilizer->set("lsmin", 0.4));
}

TEST(StabilizerModelTests, Iee2stMatchesOpenIpslCascadeAndSupportsZeroBypasses)
{
    stabilizers::StabilizerIee2st stabilizer;
    configureIee2st(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.01, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    ASSERT_EQ(stabilizer.getStates().size(), 7U);

    std::vector<double> state{0.0, 0.02, 0.1, 0.2, 0.03, 0.04, 0.05};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    stabilizer.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0, 1e-14);
    EXPECT_NEAR(derivative[2], 10.0, 1e-14);
    EXPECT_NEAR(derivative[3], -0.08, 1e-14);
    EXPECT_NEAR(derivative[4], -2.7, 1e-14);
    EXPECT_NEAR(derivative[5], -9.066666666666666, 1e-14);
    EXPECT_NEAR(derivative[6], -27.3, 1e-14);

    stabilizers::StabilizerIee2st bypass;
    bypass.set("mode1", 3.0);
    bypass.set("mode2", 0.0);
    bypass.set("k1", 2.0);
    bypass.set("t1", 0.0);
    bypass.set("t2", 0.0);
    bypass.set("t3", 0.0);
    bypass.set("t5", 0.0);
    bypass.set("t6", 0.0);
    bypass.set("t7", 0.0);
    bypass.set("t8", 0.0);
    bypass.set("t9", 0.0);
    bypass.set("t10", 0.0);
    bypass.set("lsmax", 2.0);
    bypass.set("lsmin", -2.0);
    bypass.dynInitializeA(0.0, 0);
    IOdata bypassFields;
    bypass.dynInitializeB(inputs, {0.0}, bypassFields);
    EXPECT_EQ(bypass.getStates().size(), 2U);
    std::vector<double> bypassResidual(bypass.getStates().size(), 0.0);
    bypass.residual(inputs, emptyStateData, bypassResidual.data(), cLocalSolverMode);
    EXPECT_NEAR(bypassResidual[0], 1.6, 1e-14);
    bypass.set("t6", 1.0);
    bypass.set("t5", 0.2);
    bypass.dynInitializeA(0.0, 0);
    bypass.dynInitializeB(inputs, {0.0}, bypassFields);
    std::vector<double> bypassDerivative(bypass.getStates().size(), 0.0);
    bypass.derivative(inputs, emptyStateData, bypassDerivative.data(), cLocalSolverMode);
    EXPECT_NEAR(bypassDerivative[2], 1.6, 1e-14);
    EXPECT_ANY_THROW({
        bypass.set("t6", 0.0);
        bypass.dynInitializeA(0.0, 0);
    });
}

TEST(StabilizerModelTests, Pss2aInitializesTheOpenIpslDualBranchCascade)
{
    stabilizers::StabilizerPss2a stabilizer;
    configurePss2a(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.01, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    ASSERT_EQ(stabilizer.getStates().size(), 15U);
    EXPECT_NEAR(stabilizer.getStates()[0], 0.0, 1e-14);
    EXPECT_NEAR(stabilizer.getStates()[1], 0.01, 1e-14);
    EXPECT_NEAR(stabilizer.getStates()[4], 0.7, 1e-14);
    for (std::size_t index = 2; index < stabilizer.getStates().size(); ++index) {
        if (index != 4U) EXPECT_NEAR(stabilizer.getStates()[index], 0.0, 1e-14);
    }

    std::vector<double> state(stabilizer.getStates().size(), 0.0);
    for (std::size_t index = 1; index < state.size(); ++index) {
        state[index] = 0.001 * static_cast<double>(index);
    }
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    stabilizer.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    for (std::size_t index = 1; index < derivative.size(); ++index) {
        EXPECT_TRUE(std::isfinite(derivative[index])) << index;
    }
    EXPECT_NEAR(derivative[6], 1.367, 1e-14);
    EXPECT_TRUE(std::isfinite(stabilizer.getStates()[0]));
}

TEST(StabilizerModelTests, Pss2aUsesDerivativeLagPassThroughForZeroWashoutTimes)
{
    stabilizers::StabilizerPss2a stabilizer;
    configurePss2a(stabilizer);
    stabilizer.set("tw1", 2.0);
    stabilizer.set("tw2", 2.0);
    stabilizer.set("t6", 0.0);
    stabilizer.set("tw3", 2.0);
    stabilizer.set("tw4", 0.0);
    stabilizer.set("t7", 2.0);
    stabilizer.dynInitializeA(0.0, 0);
    const IOdata inputs{1.01, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    ASSERT_EQ(stabilizer.getStates().size(), 13U);
    std::vector<double> derivative(stabilizer.getStates().size(), 0.0);
    stabilizer.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0, 1e-14);
    EXPECT_NEAR(derivative[2], 0.0, 1e-14);
    EXPECT_NEAR(derivative[3], 0.0, 1e-14);
    EXPECT_NEAR(derivative[4], 0.0, 1e-14);
    EXPECT_NEAR(derivative[5], 0.0, 1e-14);
    EXPECT_NEAR(derivative[6], 0.0, 1e-14);
}

TEST(StabilizerModelTests, Pss2aAppliesKs2OnceForItsDynamicSecondLag)
{
    stabilizers::StabilizerPss2a stabilizer;
    stabilizer.set("mode1", 0.0);
    stabilizer.set("mode2", 3.0);
    stabilizer.set("tw1", 0.0);
    stabilizer.set("tw2", 0.0);
    stabilizer.set("t6", 0.0);
    stabilizer.set("tw3", 0.0);
    stabilizer.set("tw4", 0.0);
    stabilizer.set("t7", 1.0);
    stabilizer.set("ks2", 2.0);
    stabilizer.set("ks3", 0.0);
    stabilizer.set("t8", 0.0);
    stabilizer.set("t9", 1.0);
    stabilizer.set("ks1", 1.0);
    stabilizer.set("t1", 0.0);
    stabilizer.set("t2", 0.0);
    stabilizer.set("t3", 0.0);
    stabilizer.set("t4", 0.0);
    stabilizer.set("vstmax", 2.0);
    stabilizer.set("vstmin", -2.0);
    stabilizer.dynInitializeA(0.0, 0);
    const IOdata inputs{1.0, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    ASSERT_EQ(stabilizer.getStates().size(), 8U);

    std::vector<double> state{0.0, 0.3, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> residual(state.size(), 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    // SimpleLag2 has xdot = (KS2*u - x)/T7 and y = x.  With x = 0.3,
    // the final compensator input is 0.5 - 0.3, not 0.5 - KS2*0.3.
    EXPECT_NEAR(residual[0], 0.2, 1e-14);
}

TEST(StabilizerModelTests, Stab3MatchesPowerSensitiveReferenceEquation)
{
    stabilizers::StabilizerStab3 stabilizer;
    configureStab3(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    ASSERT_EQ(stabilizer.getStates().size(), 4U);

    std::vector<double> state{0.0, 0.71, 0.02, 0.03};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    stabilizer.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.1, 1e-14);
    EXPECT_NEAR(derivative[2], -0.05, 1e-14);
    EXPECT_NEAR(derivative[3], -0.03333333333333333, 1e-14);
    std::vector<double> residual(state.size(), 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.06666666666666667, 1e-14);
}

TEST(StabilizerModelTests, NewStabilizersAreRegisteredAndCloneParameters)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> iee2st(factory->createObject("pss", "iee2st"));
    std::unique_ptr<CoreObject> pss2a(factory->createObject("pss", "pss2a"));
    std::unique_ptr<CoreObject> stab3(factory->createObject("pss", "stab3"));
    ASSERT_NE(dynamic_cast<stabilizers::StabilizerIee2st*>(iee2st.get()), nullptr);
    ASSERT_NE(dynamic_cast<stabilizers::StabilizerPss2a*>(pss2a.get()), nullptr);
    ASSERT_NE(dynamic_cast<stabilizers::StabilizerStab3*>(stab3.get()), nullptr);
    auto* iee2stModel = dynamic_cast<stabilizers::StabilizerIee2st*>(iee2st.get());
    iee2stModel->set("k1", 3.0);
    std::unique_ptr<CoreObject> clone(iee2stModel->clone());
    EXPECT_DOUBLE_EQ(dynamic_cast<stabilizers::StabilizerIee2st*>(clone.get())->get("k1"), 3.0);
}

TEST(StabilizerModelTests, St2cutMatchesAndesInitializationAndPerturbedEquations)
{
    stabilizers::StabilizerST2CUT stabilizer;
    configureSt2cut(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);

    const auto& initialized = stabilizer.getStates();
    ASSERT_EQ(initialized.size(), 7U);
    for (const auto value : initialized) {
        EXPECT_NEAR(value, 0.0, 1e-14);
    }

    // State order is [VSS, L1_y, L2_y, WO_x, LL1_x, LL2_x, LL3_x].
    // These are direct evaluations of frozen ANDES ST2CUT equations.
    std::vector<double> state{0.0, 0.02, -0.01, 0.005, 0.004, 0.003, 0.002};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[pssOmegaInLocation] = 1.01;

    std::vector<double> derivative(state.size(), 0.0);
    stabilizer.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.08, 1e-14);
    EXPECT_NEAR(derivative[2], 0.05, 1e-14);
    EXPECT_NEAR(derivative[3], 0.005, 1e-14);
    EXPECT_NEAR(derivative[4], 0.11, 1e-14);
    EXPECT_NEAR(derivative[5], 0.37333333333333335, 1e-14);
    EXPECT_NEAR(derivative[6], 1.13, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.05, 1e-14);
    for (std::size_t index = 1; index < residual.size(); ++index) {
        EXPECT_NEAR(residual[index], derivative[index], 1e-14) << index;
    }
}

TEST(StabilizerModelTests, St2cutOutputLimitsVoltageGatingAndRejectsUnsupportedSignals)
{
    stabilizers::StabilizerST2CUT stabilizer;
    configureSt2cut(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    stabilizer.setRootOffset(0, cLocalSolverMode);
    IOdata inputs{1.0, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);

    std::vector<double> state{0.0, 0.02, -0.01, 0.005, 0.004, 0.003, 0.002};
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> residual(state.size(), 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.05, 1e-14);

    inputs[pssVoltageInLocation] = 1.2;
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.0, 1e-14);

    std::vector<double> roots(4, 0.0);
    stabilizer.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    EXPECT_LT(roots[3], 0.0);
    stabilizer.rootTrigger(0.0, inputs, {1, 0, 0, 0}, cLocalSolverMode);
    EXPECT_TRUE(stabilizer.checkFlag(stabilizers::StabilizerST2CUT::OUTPUT_LIMITED));
    EXPECT_TRUE(stabilizer.checkFlag(stabilizers::StabilizerST2CUT::OUTPUT_LIMIT_HIGH));
    stabilizer.rootTrigger(0.0, inputs, {0, 0, 0, 1}, cLocalSolverMode);
    EXPECT_TRUE(stabilizer.checkFlag(stabilizers::StabilizerST2CUT::VOLTAGE_GATED));

    EXPECT_ANY_THROW(stabilizer.set("mode", 2.0));
    EXPECT_ANY_THROW(stabilizer.set("mode2", 6.0));
    EXPECT_ANY_THROW(stabilizer.set("busr", 9.0));
}

TEST(StabilizerModelTests, St2cutFactoryCloneAndParameterValidation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("pss", "st2cut"));
    auto* stabilizer = dynamic_cast<stabilizers::StabilizerST2CUT*>(object.get());
    ASSERT_NE(stabilizer, nullptr);
    stabilizer->set("k1", 2.5);
    std::unique_ptr<CoreObject> clonedObject(stabilizer->clone());
    auto* clone = dynamic_cast<stabilizers::StabilizerST2CUT*>(clonedObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("k1"), 2.5);

    EXPECT_ANY_THROW(stabilizer->set("t4", 0.0));
    EXPECT_ANY_THROW(stabilizer->set("lsmin", 0.4));
}

TEST_F(StabilizerTests, St2cutAnalyticJacobianMatchesFiniteDifferencesWhenAttached)
{
    gds = readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* stabilizer = new stabilizers::StabilizerST2CUT();
    configureSt2cut(*stabilizer);
    stabilizer->set("lsmax", 2.0);
    stabilizer->set("lsmin", -2.0);
    generator->add(stabilizer);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(StabilizerTests, IeeestAnalyticJacobianMatchesFiniteDifferencesWhenAttached)
{
    gds = readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* stabilizer = new stabilizers::StabilizerIEEEST();
    configureIeeest(*stabilizer);
    stabilizer->set("lsmax", 2.0);
    stabilizer->set("lsmin", -2.0);
    generator->add(stabilizer);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(StabilizerTests, Iee2stAnalyticJacobianMatchesFiniteDifferencesWhenAttached)
{
    gds = readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* stabilizer = new stabilizers::StabilizerIee2st();
    configureIee2st(*stabilizer);
    generator->add(stabilizer);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(StabilizerTests, Pss2aAnalyticJacobianMatchesFiniteDifferencesWhenAttached)
{
    gds = readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* stabilizer = new stabilizers::StabilizerPss2a();
    configurePss2a(*stabilizer);
    generator->add(stabilizer);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(StabilizerTests, Stab3AnalyticJacobianMatchesFiniteDifferencesWhenAttached)
{
    gds = readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* stabilizer = new stabilizers::StabilizerStab3();
    configureStab3(*stabilizer);
    generator->add(stabilizer);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(StabilizerTests, AttachedStabilizersSettleAfterLoadPulse)
{
    runAttachedStabilizerLoadPulseTest(gds, "IEEEST", [](DynamicGenerator* generator) {
        auto* stabilizer = new stabilizers::StabilizerIEEEST();
        configureIeeest(*stabilizer);
        stabilizer->set("lsmax", 2.0);
        stabilizer->set("lsmin", -2.0);
        generator->add(stabilizer);
    });
    runAttachedStabilizerLoadPulseTest(gds, "ST2CUT", [](DynamicGenerator* generator) {
        auto* stabilizer = new stabilizers::StabilizerST2CUT();
        configureSt2cut(*stabilizer);
        stabilizer->set("lsmax", 2.0);
        stabilizer->set("lsmin", -2.0);
        generator->add(stabilizer);
    });
}
