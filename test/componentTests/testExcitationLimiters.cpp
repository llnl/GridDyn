/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/ExcitationLimiter.h"
#include "griddyn/exciters/ExciterESAC6A.h"
#include "griddyn/exciters/ExciterESST1A.h"
#include "griddyn/exciters/ExciterEXAC1.h"
#include "griddyn/exciters/ExciterEXST1.h"
#include "griddyn/exciters/ExciterSCRX.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/limiters/ExcitationLimiterOEL3C.h"
#include "griddyn/limiters/ExcitationLimiterOEL4C.h"
#include "griddyn/limiters/ExcitationLimiterUEL1.h"
#include "griddyn/limiters/ExcitationLimiterUEL2C.h"
#include "utilities/MatrixDataSparse.hpp"
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <utility>
#include <vector>

using namespace griddyn;  // NOLINT
namespace {
IOdata limiterInputs()
{
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterFieldCurrentInLocation] = 1.2;
    inputs[limiterIdInLocation] = 0.5;
    inputs[limiterIqInLocation] = 0.6;
    inputs[limiterVdInLocation] = 1.0;
    inputs[limiterFieldVoltageInLocation] = 1.3;
    inputs[limiterStabilizingFeedbackInLocation] = 0.1;
    inputs[limiterExciterFieldCurrentInLocation] = 1.1;
    inputs[limiterExciterFeedbackInLocation] = 0.1;
    return inputs;
}

void configureCurve(limiters::ExcitationLimiterUEL2C& model)
{
    model.set("p0", 0.0);
    model.set("q0", -0.3);
    model.set("p1", 1.0);
    model.set("q1", -0.2);
    model.set("vuimax", 3.0);
    model.set("vulmax1", 3.0);
    model.set("vulmax2", 3.0);
}

void checkLimiterJacobian(ExcitationLimiter& model,
                          const IOdata& inputs,
                          std::vector<double> states)
{
    model.setOffset(0, cDaeSolverMode);
    const auto count = states.size();
    std::vector<double> rates(count, 0.0);
    IOlocs inputLocs(excitationLimiterInputCount);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        inputLocs[index] = static_cast<index_t>(count) + index;
    }
    constexpr double step = 1e-6;
    constexpr double jacobianFactor = 1.7;
    StateData data(0.2, states.data(), rates.data());
    data.stateSize = static_cast<count_t>(count);
    data.cj = jacobianFactor;
    model.rootCheck(inputs, data, cDaeSolverMode, CheckLevel::FULL_CHECK);
    MatrixDataSparse<double> jacobian;
    model.jacobianElements(inputs, data, jacobian, inputLocs, cDaeSolverMode);
    for (std::size_t column = 0; column < count + inputs.size(); ++column) {
        auto plusStates = states;
        auto minusStates = states;
        auto plusInputs = inputs;
        auto minusInputs = inputs;
        auto plusRates = rates;
        auto minusRates = rates;
        if (column < count) {
            plusStates[column] += step;
            minusStates[column] -= step;
            if (column > 0) {
                plusRates[column] += jacobianFactor * step;
                minusRates[column] -= jacobianFactor * step;
            }
        } else {
            plusInputs[column - count] += step;
            minusInputs[column - count] -= step;
        }
        std::vector<double> plusResidual(count);
        std::vector<double> minusResidual(count);
        StateData plusData(0.2, plusStates.data(), plusRates.data());
        StateData minusData(0.2, minusStates.data(), minusRates.data());
        plusData.stateSize = minusData.stateSize = static_cast<count_t>(count);
        model.residual(plusInputs, plusData, plusResidual.data(), cDaeSolverMode);
        model.residual(minusInputs, minusData, minusResidual.data(), cDaeSolverMode);
        for (std::size_t row = 0; row < count; ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (plusResidual[row] - minusResidual[row]) / (2.0 * step),
                        2e-6)
                << "row " << row << " column " << column;
        }
    }
}
}  // namespace

TEST(WeccLimiterTests, Uel1UsesExcitationFeedbackAndLimitsTheCombinedPiOutput)
{
    limiters::ExcitationLimiterUEL1 model;
    model.set("kur", 0.1);
    model.set("kuc", 0.0);
    model.set("kuf", 0.25);
    model.set("kul", 0.5);
    model.set("kui", 1.0);
    model.set("tu2", 0.0);
    model.dynInitializeA(0.0, 0);
    auto inputs = limiterInputs();
    IOdata fieldSet;
    model.dynInitializeB(inputs, {}, fieldSet);
    const double expected = 0.5 * (std::hypot(0.5, 0.6) - 0.1 - (0.25 * 0.1));
    EXPECT_NEAR(model.getOutput(), expected, 1e-12);
    model.set("vuimax", 0.2);
    model.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(model.getOutput(), 0.2);
    std::array<double, 4> rates{};
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(rates[1], 0.0);
    // A sufficiently large feedback clears pickup and immediately releases PI saturation.
    inputs[limiterStabilizingFeedbackInLocation] = 4.0;
    model.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(model.getOutput(), 0.0);
}

TEST(WeccLimiterTests, Uel1FeedbackLeadLagsAndSignedLimitsHaveAnalyticJacobian)
{
    limiters::ExcitationLimiterUEL1 model;
    model.set("kur", 0.1);
    model.set("kuc", 0.0);
    model.set("kuf", 0.3);
    model.set("kul", 0.5);
    model.set("kui", 0.4);
    model.set("tu1", 0.02);
    model.set("tu2", 0.1);
    model.set("tu3", 0.03);
    model.set("tu4", 0.2);
    model.set("vuimin", -0.5);
    model.set("vulmin", -0.5);
    model.dynInitializeA(0.0, 0);
    auto inputs = limiterInputs();
    IOdata fieldSet;
    model.dynInitializeB(inputs, {}, fieldSet);
    auto states = model.getStates();
    states[1] = 0.02;
    states[2] = 0.15;
    states[3] = 0.1;
    checkLimiterJacobian(model, inputs, states);
    inputs[limiterStabilizingFeedbackInLocation] = kNullVal;
    EXPECT_THROW(model.dynInitializeB(inputs, {}, fieldSet), InvalidParameterValue);
}

TEST(WeccLimiterTests, Oel3cSelectableMeasurementsPickupSwitchAndRecovery)
{
    for (const auto selector : {0, 1, 2}) {
        SCOPED_TRACE(selector);
        limiters::ExcitationLimiterOEL3C model;
        model.set("oelinput", selector);
        model.set("itfpu", 1.0);
        model.set("kscale", 1.0);
        model.set("tf", 0.0);
        model.set("k1", 2.0);
        model.set("koel", 1.0);
        model.set("toel", 1.0);
        model.set("kpoel", 0.5);
        model.dynInitializeA(0.0, 0);
        auto inputs = limiterInputs();
        constexpr std::array measurementLocations{limiterFieldCurrentInLocation,
                                                  limiterFieldVoltageInLocation,
                                                  limiterExciterFieldCurrentInLocation};
        const auto input = measurementLocations[selector];
        inputs[input] = 1.2;
        IOdata fieldSet;
        model.dynInitializeB(inputs, {}, fieldSet);
        EXPECT_DOUBLE_EQ(model.getOutput(), 0.0);  // SW1 is A at VIOEL=0.
        model.timestep(0.1, inputs, cLocalSolverMode);
        EXPECT_NEAR(model.getOutput(), 0.044 + (0.5 * 0.44), 1e-12);
        inputs[input] = 0.8;
        model.timestep(0.2, inputs, cLocalSolverMode);
        EXPECT_DOUBLE_EQ(model.getOutput(), 0.0);
        EXPECT_NEAR(model.getStates()[2], -0.008, 1e-12);
        model.timestep(0.3, inputs, cLocalSolverMode);
        EXPECT_GT(model.getStates()[2], 0.0);  // Integrator resets; SW1 returns to A.
    }
}

TEST(WeccLimiterTests, Oel3cPowerLawScalingAndAllInputModesHaveAnalyticJacobian)
{
    for (const auto selector : {0, 1, 2}) {
        limiters::ExcitationLimiterOEL3C model;
        model.set("oelinput", selector);
        model.set("kscale", 1.1);
        model.set("itfpu", 1.05);
        model.set("k1", 2.0);
        model.set("kpoel", 0.4);
        model.dynInitializeA(0.0, 0);
        auto inputs = limiterInputs();
        IOdata fieldSet;
        model.dynInitializeB(inputs, {}, fieldSet);
        auto states = model.getStates();
        states[1] = 1.1;
        states[2] = -0.1;
        checkLimiterJacobian(model, inputs, states);
    }
}

TEST(WeccLimiterTests, Uel2cFiltersAreInPublishedDiagramOrderAndGainTracksRawTerminalPower)
{
    limiters::ExcitationLimiterUEL2C model;
    configureCurve(model);
    model.set("k1", 2.0);
    model.set("k2", 2.0);
    model.set("tup", 0.2);
    model.set("tuq", 0.3);
    model.set("tuv", 0.4);
    model.set("tqref", 0.1);
    model.set("sw1", 2.0);
    model.set("tadj", 0.5);
    model.set("kuf", 0.2);
    model.set("kfb", 0.4);
    model.set("tul", 0.6);
    model.dynInitializeA(0.0, 0);
    auto inputs = limiterInputs();
    IOdata fieldSet;
    model.dynInitializeB(inputs, {}, fieldSet);
    inputs[limiterVdInLocation] = 0.8;
    inputs[limiterExciterFeedbackInLocation] = 0.2;
    std::array<double, 10> rates{};
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], (0.4 - 0.5) / 0.2, 1e-12);
    EXPECT_NEAR(rates[3], (-0.48 + 0.6) / 0.3, 1e-12);
    EXPECT_NEAR(rates[4], (0.8 - 1.0) / 0.4, 1e-12);
    EXPECT_NEAR(rates[5], 0.0, 1e-12);
    const double initialGain = 0.4 / std::hypot(0.4, 0.5);
    const double changedGain = 0.16 / std::hypot(0.16, 0.4);
    EXPECT_NEAR(rates[6], (changedGain - initialGain) / 0.5, 1e-12);
    EXPECT_NEAR(rates[7], (0.08 - 0.04) / 0.6, 1e-12);
}

TEST(WeccLimiterTests, Uel2cFullFeedbackFilterAndLeadLagPathsHaveAnalyticJacobian)
{
    limiters::ExcitationLimiterUEL2C model;
    configureCurve(model);
    for (const auto* name : {"tup", "tuq", "tuv", "tqref", "tadj", "tu2", "tu4", "tul"}) {
        model.set(name, 0.2);
    }
    model.set("tu1", 0.03);
    model.set("tu3", 0.04);
    model.set("k1", 1.0);
    model.set("k2", 2.0);
    model.set("sw1", 2.0);
    model.set("kuf", 0.4);
    model.set("kfb", 0.2);
    model.set("kul", 0.6);
    model.dynInitializeA(0.0, 0);
    auto inputs = limiterInputs();
    IOdata fieldSet;
    model.dynInitializeB(inputs, {}, fieldSet);
    auto states = model.getStates();
    states[1] = 0.03;
    states[4] = 0.8;  // Exercise Vbias's low-voltage branch.
    states[8] = 0.12;
    states[9] = 0.1;
    checkLimiterJacobian(model, inputs, states);
    model.set("sw1", 1.0);
    model.set("kfix", 0.4);
    checkLimiterJacobian(model, inputs, states);
}

TEST(WeccLimiterTests, Uel2cSupportsTenCompletePairsAndRejectsInvalidCurvesAndSelectors)
{
    limiters::ExcitationLimiterUEL2C model;
    for (int index = 0; index < 10; ++index) {
        model.set("p" + std::to_string(index), index * 0.1);
        model.set("q" + std::to_string(index), -0.4 + (index * 0.02));
    }
    EXPECT_NO_THROW(model.dynInitializeA(0.0, 0));
    std::unique_ptr<CoreObject> clone(model.clone());
    auto* cloned = dynamic_cast<limiters::ExcitationLimiterUEL2C*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_NO_THROW(cloned->dynInitializeA(0.0, 0));
    EXPECT_THROW(model.set("sw1", 1.5), InvalidParameterValue);
    EXPECT_THROW(model.set("k1", 3.0), InvalidParameterValue);
    model.set("p8", 0.7);
    EXPECT_THROW(model.dynInitializeA(0.0, 0), InvalidParameterValue);
    limiters::ExcitationLimiterUEL2C incomplete;
    incomplete.set("p0", 0.0);
    incomplete.set("q0", -0.4);
    incomplete.set("p1", 1.0);
    EXPECT_THROW(incomplete.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(WeccLimiterTests, Uel2cBypassesAndVoltageCollapseRemainFinite)
{
    limiters::ExcitationLimiterUEL2C model;
    configureCurve(model);
    model.set("k1", 2.0);
    model.set("k2", 2.0);
    model.set("sw1", 2.0);
    model.dynInitializeA(0.0, 0);
    auto inputs = limiterInputs();
    inputs[limiterIdInLocation] = inputs[limiterIqInLocation] = inputs[limiterVdInLocation] = 0.0;
    IOdata fieldSet;
    EXPECT_NO_THROW(model.dynInitializeB(inputs, {}, fieldSet));
    EXPECT_TRUE(std::isfinite(model.getOutput()));
    EXPECT_DOUBLE_EQ(model.getOutput(), 0.0);
}

TEST(WeccLimiterTests, Oel4cPositiveErrorReleasesAccumulatedLimitationWithoutAnotherDelay)
{
    limiters::ExcitationLimiterOEL4C model;
    model.set("qref", 0.1);
    model.set("tdelay", 0.2);
    model.set("kp", 0.1);
    model.set("ki", 1.0);
    model.set("vmin", -1.0);
    model.dynInitializeA(0.0, 0);
    auto inputs = limiterInputs();
    inputs[limiterIqInLocation] = -0.4;
    IOdata fieldSet;
    model.dynInitializeB(inputs, {}, fieldSet);
    model.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(model.getOutput(), 0.0);
    model.timestep(0.2, inputs, cLocalSolverMode);
    EXPECT_NEAR(model.getOutput(), 0.06, 1e-12);
    inputs[limiterIqInLocation] = 0.0;
    model.timestep(0.3, inputs, cLocalSolverMode);
    EXPECT_NEAR(model.getOutput(), 0.01, 1e-12);
    checkLimiterJacobian(model, inputs, model.getStates());
    model.timestep(0.5, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(model.getOutput(), 0.0);
}

TEST(WeccLimiterTests, ExciterFeedbackExportsHaveConsistentStateAndInputDerivatives)
{
    std::vector<std::unique_ptr<Exciter>> models;
    models.emplace_back(std::make_unique<exciters::ExciterEXAC1>());
    models.emplace_back(std::make_unique<exciters::ExciterEXST1>());
    models.emplace_back(std::make_unique<exciters::ExciterESST1A>());
    models.emplace_back(std::make_unique<exciters::ExciterSCRX>());
    models.emplace_back(std::make_unique<exciters::ExciterESAC6A>());
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterOmegaInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 1.1;
    for (auto& model : models) {
        SCOPED_TRACE(model->getName());
        model->dynInitializeA(0.0, 0);
        IOdata fieldSet(2, 0.0);
        model->dynInitializeB(inputs, {2.0}, fieldSet);
        model->setOffset(0, cDaeSolverMode);
        const auto states = model->getStates();
        constexpr double step = 1e-6;
        for (const auto signal :
             {ExciterFeedbackSignal::STABILIZING, ExciterFeedbackSignal::FIELD_CURRENT}) {
            if (!model->providesLimiterFeedback(signal)) {
                continue;
            }
            StateData data(0.0, states.data());
            data.stateSize = static_cast<count_t>(states.size());
            const auto result = model->getLimiterFeedback(signal, inputs, data, cDaeSolverMode);
            for (std::size_t column = 0; column < states.size() + inputs.size(); ++column) {
                auto plusStates = states;
                auto minusStates = states;
                auto plusInputs = inputs;
                auto minusInputs = inputs;
                double analytic = 0.0;
                if (column < states.size()) {
                    plusStates[column] += step;
                    minusStates[column] -= step;
                    for (const auto& derivative : result.stateDerivatives) {
                        if (std::cmp_equal(derivative.location, column)) {
                            analytic += derivative.value;
                        }
                    }
                } else {
                    plusInputs[column - states.size()] += step;
                    minusInputs[column - states.size()] -= step;
                    analytic = result.inputDerivatives[column - states.size()];
                }
                StateData plusData(0.0, plusStates.data());
                StateData minusData(0.0, minusStates.data());
                plusData.stateSize = minusData.stateSize = static_cast<count_t>(states.size());
                const double plus =
                    model->getLimiterFeedback(signal, plusInputs, plusData, cDaeSolverMode).value;
                const double minus =
                    model->getLimiterFeedback(signal, minusInputs, minusData, cDaeSolverMode).value;
                EXPECT_NEAR(analytic, (plus - minus) / (2.0 * step), 1e-6) << "column " << column;
            }
        }
    }
}

TEST(WeccLimiterTests, HostRejectsUnavailableExciterFeedbackInsteadOfSubstitutingAnotherQuantity)
{
    DynamicGenerator host(DynamicGenerator::DynModel::SIMPLE);
    host.add(new exciters::ExciterSCRX());
    auto* limiter = new limiters::ExcitationLimiterUEL2C();
    configureCurve(*limiter);
    limiter->set("kfb", 0.2);
    host.add(limiter);
    EXPECT_THROW(host.dynInitializeA(0.0, 0), InvalidParameterValue);
}
