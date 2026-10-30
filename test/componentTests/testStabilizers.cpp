/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/Stabilizer.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/stabilizers/StabilizerIEEEST.h"
#include "griddyn/stabilizers/StabilizerIee2st.h"
#include "griddyn/stabilizers/StabilizerPss2a.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include "griddyn/stabilizers/StabilizerStab2a.h"
#include "griddyn/stabilizers/StabilizerStab3.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace griddyn;

class StabilizerTests: public GridDynSimulationTestFixture, public ::testing::Test {};

namespace {
void configureStab2a(stabilizers::StabilizerStab2a& stabilizer)
{
    for (const auto& [name, value] :
         std::initializer_list<std::pair<std::string_view, double>>{{"k2", 2.0},
                                                                    {"t2", 0.5},
                                                                    {"k3", 3.0},
                                                                    {"t3", 0.4},
                                                                    {"k4", 0.7},
                                                                    {"k5", 0.8},
                                                                    {"t5", 0.2},
                                                                    {"hlim", 2.0}}) {
        stabilizer.set(name, value);
    }
}

void checkStab2aJacobian(stabilizers::StabilizerStab2a& stabilizer,
                         const IOdata& inputs,
                         const std::vector<double>& state)
{
    stabilizer.setOffset(0, cDaeSolverMode);
    constexpr double step = 1e-6;
    constexpr double coefficient = 0.73;
    const auto count = static_cast<index_t>(state.size());
    IOlocs inputLocs(inputs.size());
    for (index_t i = 0; i < inputs.size(); ++i) {
        inputLocs[i] = count + i;
    }
    std::vector<double> rate(state.size(), 0.0);
    StateData data(0.0, state.data(), rate.data());
    data.cj = coefficient;
    MatrixDataSparse<double> matrix;
    stabilizer.jacobianElements(inputs, data, matrix, inputLocs, cDaeSolverMode);
    EXPECT_LE(matrix.size(), stabilizer.jacSize(cDaeSolverMode));
    matrix.compact();
    for (index_t col = 0; col < count + inputs.size(); ++col) {
        auto plusState = state;
        auto minusState = state;
        auto plusRate = rate;
        auto minusRate = rate;
        auto plusInput = inputs;
        auto minusInput = inputs;
        if (col < count) {
            plusState[col] += step;
            minusState[col] -= step;
            plusRate[col] += coefficient * step;
            minusRate[col] -= coefficient * step;
        } else {
            plusInput[col - count] += step;
            minusInput[col - count] -= step;
        }
        const StateData plusData(0.0, plusState.data(), plusRate.data());
        const StateData minusData(0.0, minusState.data(), minusRate.data());
        std::vector<double> plus(count, 0.0), minus(count, 0.0);
        stabilizer.residual(plusInput, plusData, plus.data(), cDaeSolverMode);
        stabilizer.residual(minusInput, minusData, minus.data(), cDaeSolverMode);
        for (index_t row = 0; row < count; ++row) {
            const double numerical = (plus[row] - minus[row]) / (2.0 * step);
            EXPECT_NEAR(matrix.at(row, col), numerical, 2e-6 * std::max(1.0, std::abs(numerical)))
                << "row " << row << " column " << col;
        }
    }
}

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

template<class StabilizerType>
void expectOutputLimitsStableUnderFrequencyNoise(std::string_view modelName,
                                                 StabilizerType& stabilizer,
                                                 std::string_view upperLimit,
                                                 std::string_view lowerLimit,
                                                 std::size_t upperRoot,
                                                 std::size_t lowerRoot)
{
    SCOPED_TRACE(modelName);
    // Keep a 50x solver-tolerance root margin while applying 10x-tolerance
    // frequency perturbations in both directions.
    constexpr double rootMargin = 1e-4;
    constexpr double inputNoise = 1e-5;
    IOdata inputs{1.0, 1.0, 0.8, 0.7};
    stabilizer.dynInitializeA(0.0, 0);
    stabilizer.setRootOffset(0, cLocalSolverMode);
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    auto state = stabilizer.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    const StateData stateData(0.0, state.data(), stateDerivative.data());
    std::array<double, 4> roots{};

    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    const double unlimitedOutput = stabilizer.get(upperLimit) + rootMargin - roots[upperRoot];
    const double upperTestLimit = unlimitedOutput;
    const double lowerTestLimit = unlimitedOutput - 0.1;
    if (upperTestLimit < stabilizer.get(lowerLimit)) {
        stabilizer.set(lowerLimit, lowerTestLimit);
    }
    stabilizer.set(upperLimit, upperTestLimit);
    stabilizer.set(lowerLimit, lowerTestLimit);
    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[upperRoot], rootMargin, 1e-12);
    for (const double delta : {-inputNoise, inputNoise}) {
        inputs[pssOmegaInLocation] = 1.0 + delta;
        stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[upperRoot], 0.0) << delta;
    }

    inputs[pssOmegaInLocation] = 1.0;
    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    const double lowerBoundaryOutput = roots[lowerRoot] + stabilizer.get(lowerLimit) - rootMargin;
    const double lowerTestBoundary = lowerBoundaryOutput;
    const double upperTestBoundary = lowerBoundaryOutput + 0.1;
    if (lowerTestBoundary > stabilizer.get(upperLimit)) {
        stabilizer.set(upperLimit, upperTestBoundary);
    }
    stabilizer.set(lowerLimit, lowerTestBoundary);
    stabilizer.set(upperLimit, upperTestBoundary);
    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[lowerRoot], rootMargin, 1e-12);
    for (const double delta : {-inputNoise, inputNoise}) {
        inputs[pssOmegaInLocation] = 1.0 + delta;
        stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[lowerRoot], 0.0) << delta;
    }
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

TEST(StabilizerModelTests, IeeestLimitRootsStayClearOfZeroForSmallTwoSidedInputNoise)
{
    stabilizers::StabilizerIEEEST stabilizer;
    configureIeeest(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    stabilizer.setRootOffset(0, cLocalSolverMode);
    IOdata inputs{1.0, 1.0, 0.8, 0.7};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    auto state = stabilizer.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    const StateData stateData(0.0, state.data(), stateDerivative.data());
    std::array<double, 4> roots{};

    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    constexpr double rootMargin = 1e-4;
    const double unlimitedOutput = stabilizer.get("lsmax") + rootMargin - roots[0];
    stabilizer.set("lsmin", unlimitedOutput - 0.1);
    stabilizer.set("lsmax", unlimitedOutput);
    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[0], rootMargin, 1e-12);
    for (const double speed : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[pssOmegaInLocation] = speed;
        stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[0], 0.0);
    }

    inputs[pssOmegaInLocation] = 1.0;
    stabilizer.set("lsmax", unlimitedOutput + 0.1);
    stabilizer.set("lsmin", unlimitedOutput);
    stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[1], rootMargin, 1e-12);
    for (const double speed : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[pssOmegaInLocation] = speed;
        stabilizer.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[1], 0.0);
    }
}

TEST(StabilizerModelTests, AllOutputLimitRootFamiliesStayClearUnderSmallFrequencyNoise)
{
    stabilizers::StabilizerIee2st iee2st;
    configureIee2st(iee2st);
    expectOutputLimitsStableUnderFrequencyNoise("IEE2ST", iee2st, "lsmax", "lsmin", 0, 1);

    stabilizers::StabilizerPss2a pss2a;
    configurePss2a(pss2a);
    expectOutputLimitsStableUnderFrequencyNoise("PSS2A", pss2a, "vstmax", "vstmin", 0, 1);

    stabilizers::StabilizerST2CUT st2cut;
    configureSt2cut(st2cut);
    expectOutputLimitsStableUnderFrequencyNoise("ST2CUT", st2cut, "lsmax", "lsmin", 0, 1);
}

TEST(StabilizerModelTests, Stab3UpperAndLowerLimitRootsStayClearUnderSmallPowerNoise)
{
    constexpr double rootMargin = 1e-4;
    constexpr double inputNoise = 1e-5;
    stabilizers::StabilizerStab3 stabilizer;
    configureStab3(stabilizer);
    // With the input filters bypassed, the initialized electrical-power
    // perturbation is the only small signal crossing the limiter surface.
    stabilizer.set("tt", 0.0);
    stabilizer.set("tx1", 0.0);
    stabilizer.dynInitializeA(0.0, 0);
    stabilizer.setRootOffset(0, cLocalSolverMode);
    IOdata inputs{1.0, 1.0, 0.8, 0.8};
    IOdata fieldSet;
    stabilizer.dynInitializeB(inputs, {0.0}, fieldSet);
    auto state = stabilizer.getStates();
    ASSERT_EQ(state.size(), 2U);
    std::vector<double> stateDerivative(state.size(), 0.0);
    std::array<double, 2> roots{};

    state[1] = 1e-3;
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    const StateData upperState(0.0, state.data(), stateDerivative.data());
    stabilizer.rootTest(inputs, upperState, roots.data(), cLocalSolverMode);
    const double upperOutput = stabilizer.get("vlim") + rootMargin - roots[0];
    stabilizer.set("vlim", upperOutput);
    stabilizer.rootTest(inputs, upperState, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[0], rootMargin, 1e-12);
    for (const double delta : {-inputNoise, inputNoise}) {
        inputs[pssElectricalPowerInLocation] = 0.8 + delta;
        stabilizer.rootTest(inputs, upperState, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[0], 0.0) << delta;
    }

    inputs[pssElectricalPowerInLocation] = 0.8;
    state[1] = -1e-3;
    stabilizer.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    const StateData lowerState(0.0, state.data(), stateDerivative.data());
    stabilizer.rootTest(inputs, lowerState, roots.data(), cLocalSolverMode);
    const double lowerOutput = -stabilizer.get("vlim") + roots[1] - rootMargin;
    stabilizer.set("vlim", -lowerOutput);
    stabilizer.rootTest(inputs, lowerState, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[1], rootMargin, 1e-12);
    for (const double delta : {-inputNoise, inputNoise}) {
        inputs[pssElectricalPowerInLocation] = 0.8 + delta;
        stabilizer.rootTest(inputs, lowerState, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[1], 0.0) << delta;
    }
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
    EXPECT_NEAR(bypassResidual[0], 1.4, 1e-14);
    bypass.set("t6", 1.0);
    bypass.set("t5", 0.2);
    bypass.dynInitializeA(0.0, 0);
    bypass.dynInitializeB(inputs, {0.0}, bypassFields);
    std::vector<double> bypassDerivative(bypass.getStates().size(), 0.0);
    bypass.derivative(inputs, emptyStateData, bypassDerivative.data(), cLocalSolverMode);
    EXPECT_NEAR(bypassDerivative[2], 1.4, 1e-14);
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
        if (index != 4U) {
            EXPECT_NEAR(stabilizer.getStates()[index], 0.0, 1e-14);
        }
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

TEST(Stab2aTests, InitializationAndPerturbedEquationsMatchOpenIpsl)
{
    stabilizers::StabilizerStab2a stabilizer;
    configureStab2a(stabilizer);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 1.0, 0.8, 0.7}, fields;
    stabilizer.dynInitializeB(inputs, {0.0}, fields);
    ASSERT_EQ(stabilizer.getStates().size(), 7U);
    EXPECT_DOUBLE_EQ(stabilizer.getStates()[1], 0.7);
    std::vector<double> residual(7, 0.0);
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (const double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-14);
    }
    // Independent evaluation of the six OpenIPSL transfer-function blocks:
    // washout outputs are -0.08, -0.24, -0.42; the parallel input is -0.244.
    std::vector<double> state{0.02, 0.71, 0.04, -0.03, 0.05, -0.02, 0.03};
    std::vector<double> rate(7, 0.0);
    stabilizer.setState(0.0, state.data(), rate.data(), cLocalSolverMode);
    inputs[pssElectricalPowerInLocation] = 0.75;
    stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    const std::array<double, 7> expected{0.01, 0.08, -0.24, -0.42, -3.275, -0.876, -0.23};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_NEAR(residual[i], expected[i], 1e-13) << i;
    }
    checkStab2aJacobian(stabilizer, inputs, state);
    EXPECT_EQ(stabilizer.findIndex("washout1", cLocalSolverMode), 1);
    EXPECT_EQ(stabilizer.findIndex("outputlag2", cLocalSolverMode), 6);
}

TEST(Stab2aTests, ZeroTimeConstantsHaveExactBypassesAndJacobians)
{
    for (const double t2 : {0.0, 0.5}) {
        for (const double t3 : {0.0, 0.4}) {
            for (const double t5 : {0.0, 0.2}) {
                SCOPED_TRACE(testing::Message() << t2 << ", " << t3 << ", " << t5);
                stabilizers::StabilizerStab2a stabilizer;
                configureStab2a(stabilizer);
                stabilizer.set("t2", t2);
                stabilizer.set("t3", t3);
                stabilizer.set("t5", t5);
                stabilizer.dynInitializeA(0.0, 0);
                IOdata inputs{1.0, 1.0, 0.8, 0.7}, fields;
                stabilizer.dynInitializeB(inputs, {0.0}, fields);
                const auto count =
                    1U + (t2 > 0.0 ? 3U : 0U) + (t3 > 0.0 ? 1U : 0U) + (t5 > 0.0 ? 2U : 0U);
                ASSERT_EQ(stabilizer.getStates().size(), count);
                std::vector<double> residual(count, 0.0);
                stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
                for (const double value : residual) {
                    EXPECT_NEAR(value, 0.0, 1e-14);
                }
                auto state = stabilizer.getStates();
                for (std::size_t i = 1; i < state.size(); ++i) {
                    state[i] += 0.003 * static_cast<double>(i);
                }
                checkStab2aJacobian(stabilizer, inputs, state);
                std::vector<double> rate(count, 0.0), update(count, 0.0);
                stabilizer.setState(0.0, state.data(), rate.data(), cLocalSolverMode);
                stabilizer.algebraicUpdate(
                    inputs, emptyStateData, update.data(), cLocalSolverMode, 1.0);
                state[0] = update[0];
                stabilizer.setState(0.0, state.data(), rate.data(), cLocalSolverMode);
                stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
                EXPECT_NEAR(residual[0], 0.0, 1e-14);
                if (t2 == 0.0 && t3 == 0.0 && t5 == 0.0) {
                    EXPECT_DOUBLE_EQ(update[0], 0.0);
                }
            }
        }
    }
    // Bypassing both output stages leaves K5 squared times the parallel gains.
    stabilizers::StabilizerStab2a bypass;
    configureStab2a(bypass);
    bypass.set("t3", 0.0);
    bypass.set("t5", 0.0);
    bypass.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 1.0, 0.8, 0.75}, fields;
    bypass.dynInitializeB(inputs, {0.0}, fields);
    std::vector<double> state{0.0, 0.71, 0.04, -0.03}, rate(4, 0.0), residual(4, 0.0);
    bypass.setState(0.0, state.data(), rate.data(), cLocalSolverMode);
    bypass.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.99456, 1e-13);
}

TEST(Stab2aTests, StepResponseMatchesIndependentTransferFunction)
{
    stabilizers::StabilizerStab2a stabilizer;
    stabilizer.set("k3", 0.0);
    stabilizer.set("t2", 0.5);
    stabilizer.set("t3", 0.5);
    stabilizer.set("t5", 0.5);
    stabilizer.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 1.0, 0.8, 0.7}, fields;
    stabilizer.dynInitializeB(inputs, {0.0}, fields);
    constexpr double amplitude = 0.02;
    inputs[pssElectricalPowerInLocation] += amplitude;
    // H(s) = -(tau*s)^3/(1+tau*s)^5 with K3=0 and K2=K4=K5=1.
    // Inverting H(s)/s gives -exp(-z)*(z^2/2-z^3/3+z^4/24), z=t/tau.
    int step = 0;
    for (const double sample : {0.25, 0.5, 1.0, 2.0, 4.0}) {
        while (step < static_cast<int>(std::lround(sample / 0.0001))) {
            stabilizer.timestep(++step * 0.0001, inputs, cLocalSolverMode);
        }
        const double z = sample / 0.5;
        const double expected =
            -amplitude * std::exp(-z) * (z * z / 2.0 - z * z * z / 3.0 + z * z * z * z / 24.0);
        EXPECT_NEAR(stabilizer.getOutput(), expected, 2e-6) << sample;
    }
}

TEST(Stab2aTests, OutputLimitsReleaseWithoutFreezingFilters)
{
    stabilizers::StabilizerStab2a stabilizer;
    configureStab2a(stabilizer);
    stabilizer.set("hlim", 0.1);
    stabilizer.dynInitializeA(0.0, 0);
    stabilizer.setRootOffset(0, cLocalSolverMode);
    IOdata inputs{1.0, 1.0, 0.8, 0.7}, fields;
    stabilizer.dynInitializeB(inputs, {0.0}, fields);
    auto state = stabilizer.getStates();
    std::vector<double> rate(state.size(), 0.0), residual(state.size(), 0.0);
    for (const double outputState : {0.2, -0.2, 0.02}) {
        state.back() = outputState;
        stabilizer.setState(0.0, state.data(), rate.data(), cLocalSolverMode);
        stabilizer.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        EXPECT_NEAR(residual[0], std::clamp(outputState, -0.1, 0.1), 1e-14);
        EXPECT_NEAR(residual.back(), -outputState / 0.2, 1e-14);
        std::array<double, 2> roots{};
        stabilizer.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
        if (outputState > 0.1) {
            EXPECT_LT(roots[0], 0.0);
        } else if (outputState < -0.1) {
            EXPECT_LT(roots[1], 0.0);
        }
        stabilizer.rootTrigger(0.0, inputs, {1, 1}, cLocalSolverMode);
        EXPECT_EQ(stabilizer.checkFlag(stabilizers::StabilizerStab2a::OUTPUT_LIMITED),
                  std::abs(outputState) >= 0.1);
        EXPECT_EQ(stabilizer.checkFlag(stabilizers::StabilizerStab2a::OUTPUT_LIMIT_HIGH),
                  outputState >= 0.1);
        checkStab2aJacobian(stabilizer, inputs, state);
    }
}

TEST(Stab2aTests, FactoryCloneAndInvalidParameterHandling)
{
    std::unique_ptr<CoreObject> object(
        CoreObjectFactory::instance()->createObject("pss", "stab2a"));
    auto* stabilizer = dynamic_cast<stabilizers::StabilizerStab2a*>(object.get());
    ASSERT_NE(stabilizer, nullptr);
    configureStab2a(*stabilizer);
    std::unique_ptr<CoreObject> cloned(stabilizer->clone());
    auto* copy = dynamic_cast<stabilizers::StabilizerStab2a*>(cloned.get());
    ASSERT_NE(copy, nullptr);
    for (const auto name : {"k2", "t2", "k3", "t3", "k4", "k5", "t5", "hlim"}) {
        EXPECT_DOUBLE_EQ(copy->get(name), stabilizer->get(name)) << name;
        EXPECT_THROW(stabilizer->set(name, std::numeric_limits<double>::quiet_NaN()),
                     InvalidParameterValue);
    }
    for (const auto name : {"t2", "t3", "t5", "hlim"}) {
        EXPECT_THROW(stabilizer->set(name, -0.1), InvalidParameterValue);
    }
    stabilizer->set("h_lim", 0.0);
    EXPECT_DOUBLE_EQ(stabilizer->get("hlim"), 0.0);
    stabilizer->dynInitializeA(0.0, 0);
    IOdata fields;
    EXPECT_THROW(stabilizer->dynInitializeB({1.0, 1.0}, {0.0}, fields), InvalidParameterValue);
    EXPECT_THROW(stabilizer->dynInitializeB(
                     {1.0, 1.0, 0.0, std::numeric_limits<double>::infinity()}, {0.0}, fields),
                 InvalidParameterValue);
}

TEST_F(StabilizerTests, Stab2aAttachedGeneratorSettlesAfterLoadPulse)
{
    runAttachedStabilizerLoadPulseTest(gds, "STAB2A", [](DynamicGenerator* generator) {
        auto* stabilizer = new stabilizers::StabilizerStab2a();
        configureStab2a(*stabilizer);
        // A moderate gain keeps this generic fourth-order-machine case stable.
        stabilizer->set("k2", 0.5);
        generator->add(stabilizer);
    });
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
