/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModel6.h"
#include "griddyn/governors/GovernorGPWSCC.h"
#include "griddyn/governors/GovernorGast.h"
#include "griddyn/governors/GovernorGgov1.h"
#include "griddyn/governors/GovernorHydro.h"
#include "griddyn/governors/GovernorHygov.h"
#include "griddyn/governors/GovernorHygov4.h"
#include "griddyn/governors/GovernorHygovDB.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/governors/GovernorIeeeG2.h"
#include "griddyn/governors/GovernorIeeeSimple.h"
#include "griddyn/governors/GovernorSteamNR.h"
#include "griddyn/governors/GovernorSteamTCSR.h"
#include "griddyn/governors/GovernorTG2.h"
#include "griddyn/governors/GovernorTgov1.h"
#include "griddyn/governors/GovernorTgov1Variants.h"
#include "griddyn/simulation/Diagnostics.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>
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

void configureIeeeG2(governors::GovernorIeeeG2& governor)
{
    governor.set("k", 20.0);
    governor.set("t1", 50.0);
    governor.set("t2", 5.0);
    governor.set("t3", 1.0);
    governor.set("t4", 1.5);
    governor.set("pmax", 1.25);
    governor.set("pmin", 0.0);
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

void configureGast(governors::GovernorGast& governor)
{
    governor.set("r", 0.05);
    governor.set("t1", 0.4);
    governor.set("t2", 0.1);
    governor.set("t3", 3.0);
    governor.set("at", 1.2);
    governor.set("kt", 2.0);
    governor.set("vmax", 1.5);
    governor.set("vmin", -0.05);
    governor.set("dt", 0.1);
}

void configureGPWSCC(governors::GovernorGPWSCC& governor)
{
    governor.set("mwcap", 100.0);
    governor.set("mvabase", 100.0);
    governor.set("gmax", 0.85);
    governor.set("gmin", 0.0);
    governor.set("r", 0.055);
    governor.set("td", 0.04);
    governor.set("tf", 0.04);
    governor.set("tp", 0.13);
    governor.set("velopen", 0.3);
    governor.set("velclose", -0.3);
    governor.set("kp", 4.0);
    governor.set("kd", 1.5);
    governor.set("ki", 2.0);
    governor.set("kg", 15.0);
    governor.set("tturb", 1.0);
    governor.set("aturb", 0.8);
    governor.set("bturb", 1.0);
    governor.set("tt", 2.0);
    // The supplied PSLF records use an all-zero Gv/Pgv curve.  GPWSCC treats
    // that documented-default form as Pgv=Gv.
}

void configureSteam(governors::GovernorSteamNR& governor)
{
    governor.set("k", 10.0);
    governor.set("t1", 0.5);
    governor.set("t2", 0.1);
    governor.set("t3", 1.0);
    governor.set("tch", 1.2);
    governor.set("pup", 1.2);
    governor.set("pdown", -1.2);
    governor.set("pmax", 2.0);
    governor.set("pmin", 0.0);
}

void expectGovernorDaeJacobian(Governor& governor,
                               const IOdata& inputs,
                               const std::vector<double>& state,
                               double cj,
                               double tolerance = 2e-5)
{
    constexpr double step = 1e-6;
    const auto stateCount = state.size();
    ASSERT_EQ(governor.stateSize(cDaeSolverMode), state.size());
    governor.setOffset(0, cDaeSolverMode);
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    stateData.cj = cj;
    MatrixDataSparse<double> jacobian;
    IOlocs inputLocs(inputs.size(), kNullLocation);
    for (std::size_t index = 0; index < inputLocs.size(); ++index) {
        inputLocs[index] = static_cast<index_t>(20U + index);
    }
    governor.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);

    const auto residualAt = [&governor, &inputs](const std::vector<double>& trialState,
                                                 const std::vector<double>& trialDerivative) {
        StateData trialData(0.0, trialState.data(), trialDerivative.data());
        trialData.stateSize = static_cast<count_t>(trialState.size());
        std::vector<double> residual(trialState.size(), 0.0);
        governor.residual(inputs, trialData, residual.data(), cDaeSolverMode);
        return residual;
    };

    const auto baseResidual = residualAt(state, stateDerivative);
    for (std::size_t column = 0; column < stateCount; ++column) {
        auto trialState = state;
        auto trialDerivative = stateDerivative;
        trialState[column] += step;
        trialDerivative[column] += step;
        const auto trialResidual = residualAt(trialState, trialDerivative);
        for (std::size_t row = 0; row < stateCount; ++row) {
            const double numerical = (trialResidual[row] - baseResidual[row]) / step;
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        numerical,
                        tolerance)
                << "state row " << row << " column " << column;
        }
    }
}

void expectGovernorEquationConsistency(Governor& governor,
                                       const IOdata& inputs,
                                       const std::vector<double>& state,
                                       double tolerance = 2e-5)
{
    expectGovernorDaeJacobian(governor, inputs, state, 1.0, tolerance);

    constexpr double step = 1e-6;
    constexpr index_t firstInputColumn = 20;
    governor.setOffset(0, cDaeSolverMode);
    std::vector<double> stateDerivative(state.size(), 0.0);
    for (std::size_t index = 1; index < state.size(); ++index) {
        stateDerivative[index] = 0.01 * static_cast<double>(index);
    }
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    stateData.cj = 1.0;
    std::vector<double> derivative(state.size(), 0.0);
    std::vector<double> residual(state.size(), 0.0);
    governor.derivative(inputs, stateData, derivative.data(), cDaeSolverMode);
    governor.residual(inputs, stateData, residual.data(), cDaeSolverMode);
    for (std::size_t index = 1; index < state.size(); ++index) {
        EXPECT_NEAR(residual[index], derivative[index] - stateDerivative[index], tolerance)
            << "differential residual row " << index;
    }

    MatrixDataSparse<double> jacobian;
    IOlocs inputLocs(inputs.size(), kNullLocation);
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        inputLocs[index] = firstInputColumn + static_cast<index_t>(index);
    }
    governor.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        auto trialInputs = inputs;
        trialInputs[input] += step;
        std::vector<double> trialResidual(state.size(), 0.0);
        governor.residual(trialInputs, stateData, trialResidual.data(), cDaeSolverMode);
        for (std::size_t row = 0; row < state.size(); ++row) {
            const double numerical = (trialResidual[row] - residual[row]) / step;
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row),
                                    firstInputColumn + static_cast<index_t>(input)),
                        numerical,
                        tolerance)
                << "input row " << row << " column " << input;
        }
    }
}
}  // namespace

TEST(GovernorModelTests, IeeeSimpleAutoAdjustedLimitIsTheRuntimeBoundary)
{
    governors::GovernorIeeeSimple governor;
    governor.set("k", 10.0);
    governor.set("t1", 0.1);
    governor.set("t2", 0.15);
    governor.set("t3", 0.05);
    governor.set("pmin", 0.0);
    governor.set("pmax", 0.5);
    governor.dynInitializeA(0.0, 0);
    governor.setRootOffset(0, cLocalSolverMode);

    const IOdata inputs{1.0, 1.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {1.0}, fieldSet);
    EXPECT_DOUBLE_EQ(governor.get("pmax"), 1.0);
    ASSERT_EQ(governor.getStates().size(), 2U);
    EXPECT_DOUBLE_EQ(governor.getStates()[0], 1.0);

    double root = 0.0;
    governor.rootTest(inputs, emptyStateData, &root, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(root, 0.0);
    governor.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);

    std::vector<double> derivative(2, 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[0], 0.0);
    expectGovernorDaeJacobian(governor, inputs, governor.getStates(), 1.0);

    governors::GovernorIeeeSimple strictGovernor;
    strictGovernor.set("k", 10.0);
    strictGovernor.set("t1", 0.1);
    strictGovernor.set("t2", 0.15);
    strictGovernor.set("t3", 0.05);
    strictGovernor.set("pmin", 0.0);
    strictGovernor.set("pmax", 0.5);
    strictGovernor.dynInitializeA(0.0, 1U << STRICT_GOVERNOR_LIMITS);
    EXPECT_THROW(strictGovernor.dynInitializeB(inputs, {1.0}, fieldSet), InvalidParameterValue);
}

TEST(GovernorModelTests, SteamNrInitializesAndImplementsSteamChestDynamics)
{
    governors::GovernorSteamNR governor;
    configureSteam(governor);
    EXPECT_DOUBLE_EQ(governor.get("tch"), 1.2);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.8};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.8}, fieldSet);

    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 4U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 0.8);
    EXPECT_DOUBLE_EQ(initialized[2], 0.0);
    EXPECT_DOUBLE_EQ(initialized[3], 0.8);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.8);

    std::vector<double> residual(initialized.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-14);
    }

    inputs[govOmegaInLocation] = 0.99;
    inputs[govpSetInLocation] = 0.9;
    std::vector<double> derivative(initialized.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.12, 1e-14);
    EXPECT_NEAR(derivative[2], -0.016, 1e-14);
    EXPECT_NEAR(derivative[3], 0.0, 1e-14);
}

TEST(GovernorModelTests, SteamTcsrInitializesAndWeightsThreeSteamStages)
{
    governors::GovernorSteamTCSR governor;
    configureSteam(governor);
    governor.set("trh", 1.2);
    governor.set("tco", 1.2);
    governor.set("fch", 0.2);
    governor.set("fip", 0.3);
    governor.set("flp", 0.5);
    EXPECT_DOUBLE_EQ(governor.get("trh"), 1.2);
    EXPECT_DOUBLE_EQ(governor.get("tco"), 1.2);
    EXPECT_DOUBLE_EQ(governor.get("fch"), 0.2);
    EXPECT_DOUBLE_EQ(governor.get("fip"), 0.3);
    EXPECT_DOUBLE_EQ(governor.get("flp"), 0.5);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.8};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.8}, fieldSet);

    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 6U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 0.8);
    EXPECT_DOUBLE_EQ(initialized[2], 0.0);
    EXPECT_DOUBLE_EQ(initialized[3], 0.8);
    EXPECT_DOUBLE_EQ(initialized[4], 0.8);
    EXPECT_DOUBLE_EQ(initialized[5], 0.8);

    std::vector<double> state{0.8, 0.9, 0.0, 0.6, 0.7, 0.8};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> residual(state.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.07, 1e-14);
    EXPECT_NEAR(residual[3], 0.25, 1e-14);
    EXPECT_NEAR(residual[4], -1.0 / 12.0, 1e-14);
    EXPECT_NEAR(residual[5], -1.0 / 12.0, 1e-14);
}

TEST(GovernorModelTests, GastMatchesOpenIpslAndesEquations)
{
    governors::GovernorGast governor;
    configureGast(governor);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.8}, fieldSet);

    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 4U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 0.8);
    EXPECT_DOUBLE_EQ(initialized[2], 0.8);
    EXPECT_DOUBLE_EQ(initialized[3], 0.8);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.8);

    std::vector<double> state{0.82, 0.80, 0.77, 0.75};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 0.99;
    inputs[govpSetInLocation] = 0.8;
    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    // The speed-droop request is 1.0 while the temperature limit is 2.1,
    // so the low-value gate selects the droop path.
    EXPECT_NEAR(derivative[1], 0.5, 1e-14);
    EXPECT_NEAR(derivative[2], 0.30, 1e-14);
    EXPECT_NEAR(derivative[3], 0.006666666666666667, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.049, 1e-14);
    EXPECT_NEAR(residual[1], 0.5, 1e-14);
    EXPECT_NEAR(residual[2], 0.30, 1e-14);
    EXPECT_NEAR(residual[3], 0.006666666666666667, 1e-14);
}

TEST(GovernorModelTests, GastTemperatureSelectorAndValveAntiwindup)
{
    governors::GovernorGast governor;
    configureGast(governor);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.8, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.8}, fieldSet);

    // V_D=1.0 and V_T=0.8, so the temperature branch drives the valve down.
    inputs[govOmegaInLocation] = 0.99;
    std::vector<double> state{0.8, 0.9, 0.9, 1.4};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.25, 1e-14);

    // At the upper response limit an outward request is blocked, while an
    // inward request remains active.
    state[1] = 1.5;
    state[3] = 0.0;
    inputs[govOmegaInLocation] = 0.95;
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    inputs[govOmegaInLocation] = 1.02;
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);
}

TEST(GovernorModelTests, GastInitializationIncludesDampingAndRetainsInitialFlow)
{
    governors::GovernorGast governor;
    configureGast(governor);
    governor.set("vmax", 0.7);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.01, 0.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.8}, fieldSet);

    const double initialFlow = 0.801;
    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 4U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_NEAR(initialized[1], initialFlow, 1e-14);
    EXPECT_NEAR(initialized[2], initialFlow, 1e-14);
    EXPECT_NEAR(initialized[3], initialFlow, 1e-14);
    EXPECT_NEAR(fieldSet[govpSetInLocation], 1.001, 1e-14);

    inputs[govpSetInLocation] = fieldSet[govpSetInLocation];
    std::vector<double> residual(initialized.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }

    // The response bound is extended to the initial flow. An outward request
    // is blocked but an inward request can return the state toward VMAX.
    inputs[govOmegaInLocation] = 1.0;
    inputs[govpSetInLocation] = 0.9;
    std::vector<double> derivative(initialized.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    inputs[govpSetInLocation] = 0.6;
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);
}

TEST(GovernorModelTests, GastCanonicalDefaultsFactoryCloneAndTimeFloor)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("governor", "gast"));
    auto* governor = dynamic_cast<governors::GovernorGast*>(object.get());
    ASSERT_NE(governor, nullptr);
    EXPECT_DOUBLE_EQ(governor->get("r"), 0.05);
    EXPECT_DOUBLE_EQ(governor->get("t1"), 0.4);
    EXPECT_DOUBLE_EQ(governor->get("t2"), 0.1);
    EXPECT_DOUBLE_EQ(governor->get("t3"), 3.0);
    EXPECT_DOUBLE_EQ(governor->get("at"), 1.0);
    EXPECT_DOUBLE_EQ(governor->get("kt"), 2.0);
    EXPECT_DOUBLE_EQ(governor->get("vmax"), 1.0);
    EXPECT_DOUBLE_EQ(governor->get("vmin"), 0.0);

    governor->set("t1", 0.0);
    governor->set("t2", 0.0);
    governor->set("t3", 0.0);
    governor->dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.5, 0.0};
    IOdata fieldSet(2, 0.0);
    governor->dynInitializeB(inputs, {0.5}, fieldSet);
    std::vector<double> state{0.5, 0.4, 0.3, 0.2};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor->setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    governor->derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 100.0, 1e-10);
    EXPECT_NEAR(derivative[2], 100.0, 1e-10);
    EXPECT_NEAR(derivative[3], 100.0, 1e-10);
    governor->timestep(0.001, inputs, cLocalSolverMode);
    EXPECT_NEAR(governor->getStates()[1], 0.5, 1e-12);

    std::unique_ptr<CoreObject> cloneObject(governor->clone());
    auto* clone = dynamic_cast<governors::GovernorGast*>(cloneObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("t1"), 0.0);

    governors::GovernorGast temperatureLimited;
    temperatureLimited.set("at", 0.5);
    temperatureLimited.set("kt", 2.0);
    temperatureLimited.dynInitializeA(0.0, 0);
    EXPECT_THROW(temperatureLimited.dynInitializeB({1.0, 0.0, 0.0}, {0.8}, fieldSet),
                 InvalidParameterValue);
}

TEST(GovernorModelTests, GPWSCCMatchesPidGateAndTurbineDiagram)
{
    governors::GovernorGPWSCC governor;
    configureGPWSCC(governor);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.5, 0.5};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.5}, fieldSet);

    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 8U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.5);
    EXPECT_DOUBLE_EQ(initialized[1], 0.0);
    EXPECT_DOUBLE_EQ(initialized[2], 0.5);
    EXPECT_DOUBLE_EQ(initialized[3], 0.0);
    EXPECT_DOUBLE_EQ(initialized[4], 0.5);
    EXPECT_DOUBLE_EQ(initialized[5], 0.0);
    EXPECT_DOUBLE_EQ(initialized[6], 0.5);
    EXPECT_DOUBLE_EQ(initialized[7], 0.5);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.5);

    std::vector<double> residual(initialized.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (const auto value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-14);
    }

    // xD=.01, xDer=.005 gives CV=.7275.  At a 1% speed drop the documented
    // sign convention raises the gate command.  The all-zero curve is the
    // identity and the turbine is (1+.8s)/(1+s).
    std::vector<double> state{0.5, 0.01, 0.5, 0.005, 0.48, 0.02, 0.5, 0.48};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[govOmegaInLocation] = 0.99;
    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0275, 1e-14);
    EXPECT_NEAR(derivative[2], 0.02, 1e-14);
    EXPECT_NEAR(derivative[3], 0.125, 1e-14);
    EXPECT_NEAR(derivative[4], 0.01, 1e-14);
    EXPECT_NEAR(derivative[5], 26.096153846153847, 1e-13);
    EXPECT_NEAR(derivative[6], 0.02, 1e-14);
    EXPECT_NEAR(derivative[7], 0.02, 1e-14);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.004, 1e-14);

    expectGovernorEquationConsistency(governor, inputs, state, 5e-5);
}

TEST(GovernorModelTests, GPWSCCScalesCapacityBaseAndInvertsGateCurve)
{
    governors::GovernorGPWSCC governor;
    configureGPWSCC(governor);
    governor.set("mwcap", 50.0);
    governor.set("mvabase", 100.0);
    governor.set("gmax", 1.0);
    governor.set("gv1", 0.0);
    governor.set("pgv1", 0.0);
    governor.set("gv2", 0.5);
    governor.set("pgv2", 0.4);
    governor.set("gv3", 1.0);
    governor.set("pgv3", 1.0);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.25, 0.25};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.25}, fieldSet);

    // 0.25 pu on the 100 MVA machine base is 0.5 pu on MWCap.  Inverting
    // the 0/.0, .5/.4, 1/1 characteristic gives a raw gate of 7/12.
    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 8U);
    EXPECT_NEAR(initialized[6], 7.0 / 12.0, 1e-14);
    EXPECT_NEAR(initialized[7], 0.5, 1e-14);
    EXPECT_NEAR(governor.get("pmax"), 0.5, 1e-14);

    // Gate limits block outward motion and the PID integrator freezes only
    // while it would make the saturated command still larger.
    std::vector<double> state{0.25, 0.01, 1.0, 0.0, 0.5, 0.2, 1.0, 0.8};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    EXPECT_DOUBLE_EQ(derivative[6], 0.0);
}

TEST(GovernorModelTests, GPWSCCFactoryAndOriginalParameterSetAreAvailable)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("governor", "gpwscc"));
    auto* governor = dynamic_cast<governors::GovernorGPWSCC*>(object.get());
    ASSERT_NE(governor, nullptr);
    configureGPWSCC(*governor);
    EXPECT_DOUBLE_EQ(governor->get("mwcap"), 100.0);
    EXPECT_DOUBLE_EQ(governor->get("gmax"), 0.85);
    EXPECT_DOUBLE_EQ(governor->get("tturb"), 1.0);
    EXPECT_DOUBLE_EQ(governor->get("aturb"), 0.8);
    EXPECT_DOUBLE_EQ(governor->get("bturb"), 1.0);
}

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
        (1.2 * head * (state[4] - 0.08)) - (0.2 * (inputs[govOmegaInLocation] - 1.0) * state[3]);
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

TEST(GovernorModelTests, HygovLatchesRateLimitRootTransitions)
{
    governors::GovernorHygov governor;
    configureHygov(governor);
    governor.dynInitializeA(0.0, 0);
    governor.setRootOffset(0, cLocalSolverMode);

    IOdata inputs{0.95, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.4}, fieldSet);
    const std::vector<double> state{0.4, 0.0, 0.4, 0.4, 0.4};
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);

    std::array<double, 2> roots{};
    governor.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    governor.rootTrigger(0.0, inputs, {-1, 0}, cLocalSolverMode);
    EXPECT_TRUE(governor.checkFlag(governors::GovernorHygov::GATE_RATE_LIMITED));
    EXPECT_TRUE(governor.checkFlag(governors::GovernorHygov::GATE_RATE_LIMIT_HIGH));
    EXPECT_EQ(
        governor.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::NO_CHANGE);

    // A root in the opposite direction releases the upper rate limiter even
    // if the value is still on the floating-point boundary.
    inputs[govOmegaInLocation] = 1.05;
    governor.rootTrigger(0.0, inputs, {1, 0}, cLocalSolverMode);
    EXPECT_FALSE(governor.checkFlag(governors::GovernorHygov::GATE_RATE_LIMITED));
    EXPECT_EQ(
        governor.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::NO_CHANGE);

    inputs[govOmegaInLocation] = 1.057;
    governor.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    governor.rootTrigger(0.0, inputs, {-1, 0}, cLocalSolverMode);
    EXPECT_TRUE(governor.checkFlag(governors::GovernorHygov::GATE_RATE_LIMITED));
    EXPECT_FALSE(governor.checkFlag(governors::GovernorHygov::GATE_RATE_LIMIT_HIGH));
    EXPECT_EQ(
        governor.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::NO_CHANGE);

    inputs[govOmegaInLocation] = 0.95;
    governor.rootTrigger(0.0, inputs, {1, 0}, cLocalSolverMode);
    EXPECT_FALSE(governor.checkFlag(governors::GovernorHygov::GATE_RATE_LIMITED));
    EXPECT_EQ(
        governor.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::NO_CHANGE);
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

TEST(GovernorModelTests, IeeeG2MatchesOpenIpslBlockDiagramAndJacobian)
{
    governors::GovernorIeeeG2 governor;
    configureIeeeG2(governor);
    EXPECT_DOUBLE_EQ(governor.get("t4"), 1.5);
    governor.dynInitializeA(0.0, 0);

    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB({1.0, 0.8}, {0.8}, fieldSet);
    const auto& initialized = governor.getStates();
    ASSERT_EQ(initialized.size(), 4U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 0.0);
    EXPECT_DOUBLE_EQ(initialized[2], 0.0);
    EXPECT_DOUBLE_EQ(initialized[3], 0.8);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.8);

    const std::vector<double> state{0.78, 0.02, 0.01, 0.75};
    const std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    const IOdata inputs{0.99, 0.8};
    std::vector<double> derivative(state.size(), 0.0);
    governor.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.0002, 1e-14);
    EXPECT_NEAR(derivative[2], 0.37, 1e-14);
    EXPECT_NEAR(derivative[3], 0.08, 1e-14);

    std::vector<double> residual(state.size(), 0.0);
    governor.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.15, 1e-14);
    for (std::size_t index = 1; index < state.size(); ++index) {
        EXPECT_NEAR(residual[index], derivative[index], 1e-14);
    }
    expectGovernorEquationConsistency(governor, inputs, state);
}

TEST(GovernorModelTests, IeeeG1AdjustsInitialUpperLimitByDefault)
{
    governors::GovernorIeeeG1 governor;
    configureIeeeG1(governor);
    governor.dynInitializeA(0.0, 0);
    governor.setOutputInitializationTarget(governors::GovernorIeeeG1::lpOutput, 0.39);

    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB({1.0, 0.0}, {0.91}, fieldSet);

    // HP and LP fractions are 0.7 and 0.3, so these targets initialize the
    // total valve position at 1.3, above the entered PMAX of 1.2.
    EXPECT_DOUBLE_EQ(governor.get("pmax"), 1.3);
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 1.3);
    // The public state vector contains the two algebraic outputs first,
    // followed by the differential states; the valve is therefore index 3.
    EXPECT_DOUBLE_EQ(governor.getStates()[3], 1.3);

    governor.setRootOffset(0, cLocalSolverMode);
    std::array<double, 2> roots{};
    governor.rootTest({1.0, 0.0}, emptyStateData, roots.data(), cLocalSolverMode);
    // A permissively raised PMAX equal to the dispatch is an equilibrium,
    // rather than an immediate position-limit event.
    EXPECT_GT(roots[1], 0.0);
}

TEST(GovernorModelTests, GovernorStrictInitialUpperLimitPolicyRejectsViolation)
{
    governors::GovernorIeeeG1 governor;
    configureIeeeG1(governor);
    governor.dynInitializeA(0.0, 1U << STRICT_GOVERNOR_LIMITS);
    governor.setOutputInitializationTarget(governors::GovernorIeeeG1::lpOutput, 0.39);

    IOdata fieldSet(2, 0.0);
    EXPECT_THROW(governor.dynInitializeB({1.0, 0.0}, {0.91}, fieldSet), InvalidParameterValue);
}

TEST(GovernorModelTests, HygovAdjustsInitialGateUpperLimitByDefault)
{
    governors::GovernorHygov governor;
    configureHygov(governor);
    governor.dynInitializeA(0.0, 0);

    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB({1.0, 0.0}, {1.2}, fieldSet);

    // At=1.2, h0=1, and qNL=0.08 produce an initial gate of 1.08,
    // above the entered GMAX of 0.9.
    EXPECT_DOUBLE_EQ(governor.get("gmax"), 1.08);
    EXPECT_DOUBLE_EQ(governor.getStates()[2], 1.08);

    governor.setRootOffset(0, cLocalSolverMode);
    std::array<double, 2> roots{};
    governor.rootTest({1.0, 0.0}, emptyStateData, roots.data(), cLocalSolverMode);
    // A permissively raised GMAX equal to the dispatch is an equilibrium,
    // rather than an immediate position-limit event.
    EXPECT_GT(roots[1], 0.0);
}

TEST(GovernorModelTests, HygovPositionLimitReleaseRootHasHysteresis)
{
    governors::GovernorHygov governor;
    configureHygov(governor);
    governor.dynInitializeA(0.0, 0);
    governor.setRootOffset(0, cLocalSolverMode);

    IOdata inputs{1.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {1.2}, fieldSet);
    std::vector<double> state = governor.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    governor.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);

    std::array<double, 2> roots{};
    governor.rootTrigger(0.0, inputs, {0, -1}, cLocalSolverMode);
    governor.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[1], 0.0);

    inputs[govOmegaInLocation] = 1.05;
    governor.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[1], 0.0);
    governor.rootTrigger(0.0, inputs, {0, 1}, cLocalSolverMode);
    expectGovernorDaeJacobian(governor, inputs, governor.getStates(), 1.0);
}

TEST(GovernorModelTests, Tgov1VariantsMatchLeadLagAndDeadbandEquations)
{
    for (const std::string_view model : {"tgov1db", "tgov1n", "tgov1ndb"}) {
        SCOPED_TRACE(std::string(model));
        std::unique_ptr<CoreObject> object(
            CoreObjectFactory::instance()->createObject("governor", model));
        auto* governor = dynamic_cast<governors::GovernorTgov1Variant*>(object.get());
        ASSERT_NE(governor, nullptr);
        governor->set("r", 0.05);
        governor->set("t1", 0.2);
        governor->set("t2", 0.4);
        governor->set("t3", 2.0);
        governor->set("pmin", 0.0);
        governor->set("pmax", 1.2);
        governor->set("dt", 0.1);
        governor->set("dbl", -0.02);
        governor->set("dbu", 0.02);
        governor->set("paux", 0.01);
        governor->dynInitializeA(0.0, 0);
        IOdata input{1.0, 0.0};
        IOdata fieldSet(2, 0.0);
        governor->dynInitializeB(input, {0.5}, fieldSet);
        governor->set("paux", 0.02);
        ASSERT_EQ(governor->getStates().size(), 3U);
        EXPECT_NEAR(governor->getStates()[0], 0.5, 1e-14);
        input[govOmegaInLocation] = 1.03;
        input[govpSetInLocation] = fieldSet[govpSetInLocation];
        const std::vector<double> state{0.49, 0.52, 0.48};
        std::vector<double> zero(3, 0.0);
        std::vector<double> deriv(3, 0.0);
        std::vector<double> resid(3, 0.0);
        governor->setState(0.0, state.data(), zero.data(), cLocalSolverMode);
        governor->derivative(input, emptyStateData, deriv.data(), cLocalSolverMode);
        governor->residual(input, emptyStateData, resid.data(), cLocalSolverMode);
        const bool deadbandEnabled = model != "tgov1n";
        const double speedDeviation = deadbandEnabled ? 0.01 : 0.03;
        const double pauxGain = model == "tgov1db" ? 20.0 : 1.0;
        EXPECT_NEAR(deriv[1],
                    (fieldSet[govpSetInLocation] + (pauxGain * 0.01) - (20.0 * speedDeviation) -
                     0.52) /
                        0.2,
                    1e-12);
        EXPECT_NEAR(deriv[2], (0.52 - 0.48) / 2.0, 1e-12);
        EXPECT_NEAR(resid[0], 0.48 + (0.2 * (0.52 - 0.48)) - (0.1 * speedDeviation) - 0.49, 1e-12);
        EXPECT_NEAR(resid[1], deriv[1], 1e-12);
        EXPECT_NEAR(resid[2], deriv[2], 1e-12);
        expectGovernorEquationConsistency(*governor, input, state);
    }
}

TEST(GovernorModelTests, Tg2MatchesLeadLagAndLimitBranches)
{
    governors::GovernorTG2 governor;
    governor.set("r", 0.05);
    governor.set("t1", 0.2);
    governor.set("t2", 2.0);
    governor.set("pmin", 0.0);
    governor.set("pmax", 1.0);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.6}, fieldSet);
    EXPECT_NEAR(governor.getStates()[0], 0.6, 1e-14);
    EXPECT_NEAR(governor.getStates()[1], 0.0, 1e-14);
    inputs = {0.99, fieldSet[govpSetInLocation]};
    std::vector<double> state{0.62, 0.04};
    std::vector<double> zero(2, 0.0);
    std::vector<double> deriv(2, 0.0);
    std::vector<double> resid(2, 0.0);
    governor.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, deriv.data(), cLocalSolverMode);
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    EXPECT_NEAR(deriv[1], ((20.0 * 0.01) - 0.04) / 2.0, 1e-12);
    EXPECT_NEAR(resid[0], 0.6 + 0.04 + (0.1 * (0.2 - 0.04)) - 0.62, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
    governor.set("deadbandenabled", 1.0);
    governor.set("dbl", -0.02);
    governor.set("dbu", 0.02);
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    EXPECT_NEAR(resid[0], 0.6 + 0.04 + (0.1 * (0.0 - 0.04)) - 0.62, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
    inputs[govOmegaInLocation] = 0.97;
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    EXPECT_NEAR(resid[0], 0.6 + 0.04 + (0.1 * (0.6 - 0.04)) - 0.62, 1e-12);
    governor.set("pmax", 0.64);
    EXPECT_NEAR(governor.get("pmax"), 0.64, 1e-14);
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    EXPECT_NEAR(resid[0], 0.64 - 0.62, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
}

TEST(GovernorModelTests, HygovDbAppliesDeadbandOnlyToController)
{
    governors::GovernorHygovDB governor;
    configureHygov(governor);
    governor.set("dbl", -0.02);
    governor.set("dbu", 0.02);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.4}, fieldSet);
    inputs = {1.01, fieldSet[govpSetInLocation]};
    std::vector<double> state{0.4, 0.01, 0.4, 0.41, 0.42};
    std::vector<double> zero(5, 0.0);
    std::vector<double> deriv(5, 0.0);
    std::vector<double> resid(5, 0.0);
    governor.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, deriv.data(), cLocalSolverMode);
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    const double desiredGate = (0.01 / 0.3) + 0.4;
    EXPECT_NEAR(deriv[1], (fieldSet[1] - (0.05 * desiredGate) - 0.01) / 0.05, 1e-12);
    EXPECT_NEAR(deriv[2], 0.01, 1e-12);
    EXPECT_NEAR(deriv[3], (desiredGate - 0.41) / 0.5, 1e-12);
    const double head = (0.42 / 0.41) * (0.42 / 0.41);
    EXPECT_NEAR(resid[0], (1.2 * head * (0.42 - 0.08)) - (0.2 * 0.41 * 0.01) - 0.4, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
    inputs[govOmegaInLocation] = 1.03;
    governor.derivative(inputs, emptyStateData, deriv.data(), cLocalSolverMode);
    EXPECT_NEAR(deriv[1], (fieldSet[1] - 0.01 - (0.05 * desiredGate) - 0.01) / 0.05, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
}

TEST(GovernorModelTests, Hygov4MatchesWashoutServoAndWaterEquations)
{
    governors::GovernorHygov4 governor;
    governor.set("rperm", 0.1);
    governor.set("rtemp", 0.2);
    governor.set("uo", 0.3);
    governor.set("uc", -0.3);
    governor.set("pmax", 1.0);
    governor.set("pmin", 0.0);
    governor.set("tp", 0.4);
    governor.set("tg", 0.5);
    governor.set("tr", 2.0);
    governor.set("tw", 1.25);
    governor.set("at", 1.2);
    governor.set("dturb", 0.2);
    governor.set("hdam", 1.0);
    governor.set("qnl", 0.08);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.4}, fieldSet);
    const double initialFlow = 0.08 + (0.4 / 1.2);
    EXPECT_NEAR(governor.getStates()[1], initialFlow, 1e-12);
    EXPECT_NEAR(fieldSet[1], 0.1 * initialFlow, 1e-12);
    inputs = {1.01, 0.4};
    std::vector<double> state{0.41, 0.42, 0.40, 0.03, 0.43};
    std::vector<double> zero(5, 0.0);
    std::vector<double> deriv(5, 0.0);
    std::vector<double> resid(5, 0.0);
    governor.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, deriv.data(), cLocalSolverMode);
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    const double head = (0.43 / 0.42) * (0.43 / 0.42);
    EXPECT_NEAR(deriv[1], 0.03 / 0.5, 1e-12);
    EXPECT_NEAR(deriv[2], (0.42 - 0.40) / 2.0, 1e-12);
    EXPECT_NEAR(deriv[3],
                (fieldSet[1] - (0.1 * 0.42) - (0.2 * (0.42 - 0.40)) - 0.01 - 0.03) / 0.4,
                1e-12);
    EXPECT_NEAR(deriv[4], (1.0 - head) / 1.25, 1e-12);
    EXPECT_NEAR(resid[0], (1.2 * head * (0.43 - 0.08)) - (0.2 * 0.42 * 0.01) - 0.41, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
    state[3] = 0.3;
    governor.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    governor.derivative(inputs, emptyStateData, deriv.data(), cLocalSolverMode);
    EXPECT_NEAR(deriv[1], 0.3, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
}

TEST(GovernorModelTests, Tg2SpeedStepMatchesAnalyticTrajectory)
{
    governors::GovernorTG2 governor;
    governor.set("r", 0.05);
    governor.set("t1", 0.2);
    governor.set("t2", 2.0);
    governor.set("pmin", 0.0);
    governor.set("pmax", 2.0);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.6}, fieldSet);
    inputs = {0.99, fieldSet[1]};
    for (int step = 1; step <= 1000; ++step) {
        governor.timestep(step * 0.001, inputs, cLocalSolverMode);
    }
    const double lag = 0.2 * (1.0 - std::exp(-0.5));
    const auto& state = governor.getStates();
    EXPECT_NEAR(state[1], lag, 5e-5);
    EXPECT_NEAR(state[0], 0.6 + lag + (0.1 * (0.2 - lag)), 5e-5);
}

TEST(GovernorModelTests, Tgov1ZeroTurbineLagUsesValveDirectly)
{
    governors::GovernorTgov1N governor;
    governor.set("r", 0.05);
    governor.set("t1", 0.1);
    governor.set("t2", 0.2);
    governor.set("t3", 0.0);
    governor.set("pmax", 1.0);
    governor.set("pmin", 0.0);
    governor.dynInitializeA(0.0, 0);
    IOdata inputs{1.01, 0.5};
    IOdata fieldSet(2, 0.0);
    governor.dynInitializeB(inputs, {0.5}, fieldSet);
    std::vector<double> state{0.55, 0.6, 0.4};
    std::vector<double> zero(3, 0.0);
    std::vector<double> resid(3, 0.0);
    governor.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    governor.residual(inputs, emptyStateData, resid.data(), cLocalSolverMode);
    EXPECT_NEAR(resid[0], 0.6 - 0.55, 1e-12);
    EXPECT_NEAR(resid[2], 0.0, 1e-12);
    expectGovernorEquationConsistency(governor, inputs, state);
}

TEST(GovernorModelTests, GovernorVariantFactoryClonesPreserveModelAndParameters)
{
    for (const std::string_view name :
         {"tg2", "tgov1db", "tgov1n", "tgov1ndb", "hygovdb", "hygov4"}) {
        SCOPED_TRACE(std::string(name));
        std::unique_ptr<CoreObject> original(
            CoreObjectFactory::instance()->createObject("governor", name));
        ASSERT_NE(original, nullptr);
        auto* governor = dynamic_cast<Governor*>(original.get());
        ASSERT_NE(governor, nullptr);
        const std::string_view parameter = name == "hygov4" ? "rperm" : "r";
        governor->set(parameter, 0.07);
        std::unique_ptr<CoreObject> cloned(original->clone());
        ASSERT_NE(cloned, nullptr);
        const CoreObject* originalObject = original.get();
        const CoreObject* clonedObject = cloned.get();
        EXPECT_EQ(typeid(*originalObject), typeid(*clonedObject));
        auto* copied = dynamic_cast<Governor*>(cloned.get());
        ASSERT_NE(copied, nullptr);
        EXPECT_DOUBLE_EQ(copied->get(parameter), 0.07);
    }
}

TEST(GovernorModelTests, Ggov1FactoryTracksInactiveLoadLimiterAndValidatesDelay)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("governor", "ggov1"));
    auto* governor = dynamic_cast<governors::GovernorGgov1*>(object.get());
    ASSERT_NE(governor, nullptr);
    governor->set("rselect", 1.0);
    governor->set("fswitch", 0.0);
    governor->set("vmax", 2.0);
    governor->set("vmin", 0.0);
    // LDREF is the temperature/load limit rather than the present dispatch.
    // The inactive load PI controller must track the fuel request, otherwise
    // its integrator winds up even in a no-disturbance initialization.
    governor->set("ldref", 1.2);
    governor->dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.8, 0.8};
    IOdata fieldSet(3, 0.0);
    governor->dynInitializeB(inputs, {0.8}, fieldSet);
    const auto& initialized = governor->getStates();
    ASSERT_EQ(initialized.size(), 11U);
    // Leave the inactive temperature selector just above the dispatch so the
    // low-value selector starts in a smooth regime for Jacobian evaluation.
    EXPECT_NEAR(initialized[9], 0.200001, 1e-12);
    std::vector<double> residual(governor->getStates().size(), 0.0);
    governor->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    EXPECT_DOUBLE_EQ(fieldSet[govpSetInLocation], 0.8);

    // The permissive initialization policy raises the effective valve limit
    // when the dispatched fuel flow is slightly above Vmax.  The temperature
    // controller must use that same adjusted boundary rather than a separate
    // literal 1.0 cap.
    governors::GovernorGgov1 aboveNominalFuel;
    aboveNominalFuel.set("vmax", 1.0);
    aboveNominalFuel.set("vmin", 0.0);
    aboveNominalFuel.set("ldref", 1.3);
    aboveNominalFuel.dynInitializeA(0.0, 0);
    IOdata highInputs{1.0, 1.3, 1.3};
    IOdata highFieldSet(3, 0.0);
    aboveNominalFuel.dynInitializeB(highInputs, {1.3}, highFieldSet);
    std::vector<double> highResidual(aboveNominalFuel.getStates().size(), 0.0);
    aboveNominalFuel.residual(highInputs, emptyStateData, highResidual.data(), cLocalSolverMode);
    for (double value : highResidual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }

    std::unique_ptr<CoreObject> cloned(governor->clone());
    ASSERT_NE(dynamic_cast<governors::GovernorGgov1*>(cloned.get()), nullptr);

    governors::GovernorGgov1 delayed;
    delayed.set("teng", 0.25);
    EXPECT_THROW(delayed.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST_F(GovernorTests, Ggov1CouplesElectricalPowerAndHasAnalyticJacobian)
{
    gds = readSimXMLFile(std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml"));
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* machine = generator->find("genmodel");
    ASSERT_NE(machine, nullptr);
    // A nonzero stator resistance distinguishes the GGOV1 terminal-power
    // input from the machine's air-gap electrical-torque signal.
    machine->set("r", 0.02);
    auto* governor = new governors::GovernorGgov1();
    governor->set("vmax", 2.0);
    governor->set("vmin", 0.0);
    governor->set("kturb", 2.0);
    governor->set("ldref", 1.2);
    governor->set("kiload", 0.0);
    governor->set("fswitch", 0.0);
    governor->set("rselect", -2.0);
    generator->add(governor);
    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    // Move the temperature limiter away from its initialization tie with the
    // normal governor branch before checking the piecewise analytic Jacobian.
    auto state = gds->getState(cDaeSolverMode);
    std::vector<double> stateDerivative(state.size(), 0.0);
    const auto& offsets = governor->getOffsets(cDaeSolverMode);
    state[offsets.diffOffset + 8] += 0.1;
    gds->setState(0.0, state.data(), stateDerivative.data(), cDaeSolverMode);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
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
    EXPECT_GT(roots[1], 0.0);
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

TEST_F(GovernorTests, GPWSCCCouplesToDynamicGeneratorAndHasAnalyticJacobian)
{
    const std::string fileName = std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml");

    GridDynSimulation::resetObjectCounters();
    gds = readSimXMLFile(fileName);
    auto* generator = dynamic_cast<DynamicGenerator*>(gds->findByUserID("gen", 2));
    ASSERT_NE(generator, nullptr);

    auto* governor = new governors::GovernorGPWSCC();
    configureGPWSCC(*governor);
    // Keep the fixture's 1.2 pu dispatch within the entered gate range and
    // stay inside the speed-deadband branch for a smooth system Jacobian.
    governor->set("gmax", 2.0);
    governor->set("db1", 0.01);
    generator->add(governor);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);
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

TEST_F(GovernorTests, PartitionedGovernorEquationSweep)
{
    using Parameter = std::pair<std::string, double>;
    struct GovernorCase {
        std::string_view name;
        std::vector<Parameter> parameters;
    };
    const std::array cases{
        GovernorCase{.name = "tgov1",
                     .parameters = {{"r", 0.05},
                                    {"t1", 0.05},
                                    {"pmax", 2.0},
                                    {"pmin", 0.0},
                                    {"t2", 1.0},
                                    {"t3", 2.1},
                                    {"dt", 0.1}}},
        GovernorCase{.name = "gast",
                     .parameters = {{"r", 0.05},
                                    {"t1", 0.4},
                                    {"t2", 0.1},
                                    {"t3", 3.0},
                                    {"at", 1.5},
                                    {"kt", 2.0},
                                    {"vmax", 1.5},
                                    {"vmin", -0.05},
                                    {"dt", 0.1}}},
        GovernorCase{.name = "hydro",
                     .parameters = {{"k", 5.0},
                                    {"t1", 0.25},
                                    {"t2", 0.0},
                                    {"t3", 0.1},
                                    {"tw", 0.04},
                                    {"pmax", 2.0},
                                    {"pmin", 0.0}}},
        GovernorCase{.name = "hygov",
                     .parameters = {{"r", 0.05},
                                    {"temporarydroop", 0.3},
                                    {"tr", 5.0},
                                    {"tf", 0.05},
                                    {"tg", 0.5},
                                    {"velm", 0.2},
                                    {"gmax", 1.5},
                                    {"gmin", 0.0},
                                    {"tw", 1.25},
                                    {"at", 1.2},
                                    {"dturb", 0.2},
                                    {"qnl", 0.08}}},
        GovernorCase{.name = "ieeeg1",
                     .parameters = {{"k", 20.0}, {"t1", 0.2},   {"t2", 0.05},  {"t3", 0.1},
                                    {"uo", 0.3}, {"uc", -0.25}, {"pmax", 2.0}, {"pmin", 0.0},
                                    {"t4", 0.4}, {"k1", 0.3},   {"k2", 0.0},   {"t5", 0.0},
                                    {"k3", 0.2}, {"k4", 0.0},   {"t6", 0.5},   {"k5", 0.1},
                                    {"k6", 0.0}, {"t7", 0.2},   {"k7", 0.1},   {"k8", 0.0}}},
        GovernorCase{.name = "ggov1",
                     .parameters = {{"vmax", 2.0},
                                    {"vmin", 0.0},
                                    {"kturb", 2.0},
                                    {"ldref", 1.2},
                                    {"kiload", 0.0},
                                    {"fswitch", 0.0},
                                    {"rselect", -2.0}}},
        GovernorCase{.name = "gpwscc",
                     .parameters = {{"mwcap", 100.0},
                                    {"mvabase", 100.0},
                                    {"gmax", 2.0},
                                    {"gmin", 0.0},
                                    {"r", 0.055},
                                    {"td", 0.04},
                                    {"tf", 0.04},
                                    {"tp", 0.13},
                                    {"velopen", 0.3},
                                    {"velclose", -0.3},
                                    {"kp", 4.0},
                                    {"kd", 1.5},
                                    {"ki", 2.0},
                                    {"kg", 15.0},
                                    {"tturb", 1.0},
                                    {"aturb", 0.8},
                                    {"bturb", 1.0},
                                    {"tt", 2.0},
                                    {"db1", 0.01}}},
    };

    for (const auto& governorCase : cases) {
        SCOPED_TRACE(governorCase.name);
        GridDynSimulation::resetObjectCounters();
        gds = readSimXMLFile(std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml"));
        ASSERT_NE(gds, nullptr);
        gds->set("dynamicsolvermethod", "partitioned");
        gds->set("defdyndiff", "basicode");
        gds->set("timestep", 0.005);

        auto* generator = dynamic_cast<DynamicGenerator*>(gds->findByUserID("gen", 2));
        ASSERT_NE(generator, nullptr);
        auto factory = CoreObjectFactory::instance();
        std::unique_ptr<CoreObject> object(factory->createObject("governor", governorCase.name));
        ASSERT_NE(object, nullptr) << "Could not create governor " << governorCase.name;
        for (const auto& [parameter, value] : governorCase.parameters) {
            object->set(parameter, value);
        }
        generator->add(object.release());

        {
            SCOPED_TRACE("dynamic initialization");
            ASSERT_EQ(gds->dynInitialize(), 0) << "dynInitialize for " << governorCase.name;
        }
        {
            SCOPED_TRACE("partitioned startup");
            ASSERT_EQ(gds->run(0.005), 0) << "partitioned startup for " << governorCase.name;
        }
        const auto algMode = gds->getSolverMode("dynalg");
        const auto diffMode = gds->getSolverMode("dyndiff");
        ASSERT_GT(gds->stateSize(algMode), 0U);
        ASSERT_GT(gds->stateSize(diffMode), 0U);

        {
            SCOPED_TRACE("partitioned diagnostics");
            {
                SCOPED_TRACE("algebraic residual");
                EXPECT_EQ(runResidualCheck(gds, algMode, false), 0);
            }
            {
                SCOPED_TRACE("differential residual");
                EXPECT_EQ(runResidualCheck(gds, diffMode, false), 0);
            }
            {
                SCOPED_TRACE("algebraic update");
                EXPECT_EQ(runAlgebraicCheck(gds, algMode, false), 0);
            }
            {
                SCOPED_TRACE("derivative");
                EXPECT_EQ(runDerivativeCheck(gds, diffMode, false), 0);
            }
            {
                SCOPED_TRACE("algebraic Jacobian");
                EXPECT_EQ(runJacobianCheck(gds, algMode, false), 0);
            }
            {
                SCOPED_TRACE("differential Jacobian");
                EXPECT_EQ(runJacobianCheck(gds, diffMode, false), 0);
            }
        }

        ASSERT_EQ(gds->run(0.05), 0) << "short integration for " << governorCase.name;
        for (const auto mode : {algMode, diffMode}) {
            const auto state = gds->getState(mode);
            EXPECT_TRUE(std::all_of(state.begin(), state.end(), [](double value) {
                return std::isfinite(value) && std::abs(value) < 100.0;
            })) << "non-finite or unbounded partition state";
        }
    }
}
