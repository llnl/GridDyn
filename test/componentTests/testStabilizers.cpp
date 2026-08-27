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
#include "griddyn/stabilizers/StabilizerST2CUT.h"
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
