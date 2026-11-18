/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterIEEET3.h"

#include "ExciterSignalHelper.h"
#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t regulatorState = 1;
    constexpr index_t feedbackState = 2;
    constexpr index_t fieldState = 3;
    constexpr double initializationTolerance = 1e-7;

    double upperRegulatorLimit(double limit)
    {
        return (limit == 0.0) ? 999.0 : limit;
    }

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

const ModelDescription& ExciterIEEET3::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterIEEET3>(
        "ExciterIEEET3",
        &Exciter::getModelDescription(),
        {parameter({"tr"}, &ExciterIEEET3::Tr, units::second, "Voltage measurement time constant"),
         parameter({"vbmax"}, &ExciterIEEET3::Vbmax, units::one, "Vbmax upper limit"),
         parameter({"ke"}, &ExciterIEEET3::Ke, units::one, "Ke gain or coefficient"),
         parameter({"te"}, &ExciterIEEET3::Te, units::second, "Exciter time constant"),
         parameter({"kf"}, &ExciterIEEET3::Kf, units::one, "Kf gain or coefficient"),
         parameter({"tf"}, &ExciterIEEET3::Tf, units::second, "Feedback time constant"),
         parameter({"kp"}, &ExciterIEEET3::Kp, units::one, "Kp gain or coefficient"),
         parameter({"ki"}, &ExciterIEEET3::Ki, units::one, "Ki gain or coefficient"),
         localState("regulator_limited",
                    &ExciterIEEET3::opFlags,
                    ExciterIEEET3::REGULATOR_LIMITED,
                    "Retained regulator_limited mode"),
         localState("regulator_limit_high",
                    &ExciterIEEET3::opFlags,
                    ExciterIEEET3::REGULATOR_LIMIT_HIGH,
                    "Retained regulator_limit_high mode"),
         state({"efd", "field"}, units::one, 0, "Field voltage"),
         state({"vmeas", "vc"}, units::one, 1, "Measured terminal voltage"),
         state({"vr", "regulator"}, units::one, 2, "Regulator state"),
         state({"wf", "feedback"}, units::one, 3, "Washout state"),
         state({"efstate"}, units::one, 4, "efstate solver state")});
    return description;
}

ExciterIEEET3::ExciterIEEET3(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 5.0;
    Ta = 0.04;
    Vrmax = 7.3;
    Vrmin = -7.3;
}

CoreObject* ExciterIEEET3::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterIEEET3, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Vbmax = Vbmax;
    result->Ke = Ke;
    result->Te = Te;
    result->Kf = Kf;
    result->Tf = Tf;
    result->Kp = Kp;
    result->Ki = Ki;
    return result;
}

void ExciterIEEET3::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 13> parameters{
        Tr, Ka, Ta, Vrmax, Vrmin, Vbmax, Ke, Te, Kf, Tf, Kp, Ki, Vref};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Tr < 0.0) || (Ka <= 0.0) || (Ta <= 0.0) || (upperRegulatorLimit(Vrmax) < Vrmin) ||
        (Vbmax <= 0.0) || (Te <= 0.0) || (Tf <= 0.0)) {
        throw InvalidParameterValue("IEEET3 gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.jacSize = 80;
    offsets.local().local.algRoots = 1;
}

double ExciterIEEET3::sourceVoltage(const IOdata& inputs) const
{
    return detail::computeRectifierData(
               inputs, Kp, Ki, 0.0, 0.0, 0.0, std::numeric_limits<double>::infinity())
        .voltage;
}

ExciterIEEET3::Evaluation
    ExciterIEEET3::evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const
{
    using detail::addSignals;
    using detail::algebraicSignal;
    using detail::clampSignal;
    using detail::constantSignal;
    using detail::inputSignal;
    using detail::multiplySignals;
    using detail::scaleSignal;
    using detail::sqrtSignal;
    using detail::stateSignal;
    using detail::subtractSignals;
    using Signal = detail::ExciterSignal<stateCount>;

    Evaluation evaluation;
    std::array<Signal, stateCount> rates{};
    const Signal measuredVoltage = (Tr > 0.0) ?
        stateSignal<stateCount>(state[voltageMeasurementState], voltageMeasurementState) :
        inputSignal<stateCount>(inputs, exciterVoltageInLocation);
    if (Tr > 0.0) {
        rates[voltageMeasurementState] =
            scaleSignal(subtractSignals(inputSignal<stateCount>(inputs, exciterVoltageInLocation),
                                        measuredVoltage),
                        1.0 / Tr);
    }

    const auto source = detail::computeRectifierData(
        inputs, Kp, Ki, 0.0, 0.0, 0.0, std::numeric_limits<double>::infinity());
    Signal sourceSignal = constantSignal<stateCount>(source.voltage);
    const std::array<index_t, 5> signalIndices{exciterIdInLocation,
                                               exciterIqInLocation,
                                               exciterVdInLocation,
                                               exciterVqInLocation,
                                               exciterXadIfdInLocation};
    for (index_t index = 0; index < 5; ++index) {
        sourceSignal.input[signalIndices[index]] = source.derivatives[index];
    }
    const Signal fieldCurrent = inputSignal<stateCount>(inputs, exciterXadIfdInLocation);
    const Signal v40Input = subtractSignals(multiplySignals(sourceSignal, sourceSignal),
                                            multiplySignals(scaleSignal(fieldCurrent, 0.78),
                                                            scaleSignal(fieldCurrent, 0.78)));
    const Signal v40 = sqrtSignal(v40Input);
    const Signal fieldStateSignal = stateSignal<stateCount>(state[fieldState], fieldState);
    const Signal feedbackStateSignal = stateSignal<stateCount>(state[feedbackState], feedbackState);
    const Signal feedback =
        scaleSignal(subtractSignals(fieldStateSignal, feedbackStateSignal), Kf / Tf);
    rates[feedbackState] =
        scaleSignal(subtractSignals(fieldStateSignal, feedbackStateSignal), 1.0 / Tf);

    Signal input = addSignals(constantSignal<stateCount>(Vref + vBias - 1.0),
                              inputSignal<stateCount>(inputs, exciterVsetInLocation));
    input = addSignals(input, inputSignal<stateCount>(inputs, exciterVssInLocation));
    input = subtractSignals(subtractSignals(input, measuredVoltage), feedback);
    const Signal regulator = stateSignal<stateCount>(state[regulatorState], regulatorState);
    const Signal regulatorDrive = subtractSignals(scaleSignal(input, Ka), regulator);
    if (!opFlags[REGULATOR_LIMITED]) {
        rates[regulatorState] = scaleSignal(regulatorDrive, 1.0 / Ta);
    }
    const Signal vbRaw = addSignals(regulator, v40);
    const Signal boundedSourceVoltage = clampSignal(vbRaw, 0.0, Vbmax);
    const Signal fieldDrive =
        subtractSignals(boundedSourceVoltage, scaleSignal(fieldStateSignal, Ke));
    rates[fieldState] = scaleSignal(fieldDrive, 1.0 / Te);
    const Signal algebraicResidual =
        subtractSignals(fieldStateSignal, algebraicSignal<stateCount>(fieldVoltage));

    evaluation.fieldOutput = state[fieldState];
    evaluation.regulatorDrive = regulatorDrive.value;
    evaluation.algebraicDerivative = algebraicResidual.algebraic;
    evaluation.fieldStateDerivatives = algebraicResidual.state;
    evaluation.fieldInputDerivatives = algebraicResidual.input;
    for (index_t row = 0; row < stateCount; ++row) {
        evaluation.rates[row] = rates[row].value;
        evaluation.rateAlgebraicDerivatives[row] = rates[row].algebraic;
        evaluation.rateStateDerivatives[row] = rates[row].state;
        evaluation.rateInputDerivatives[row] = rates[row].input;
    }
    return evaluation;
}

void ExciterIEEET3::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("IEEET3 initial voltage signals");
    }
    for (const index_t inputIndex : {exciterIdInLocation,
                                     exciterIqInLocation,
                                     exciterVdInLocation,
                                     exciterVqInLocation,
                                     exciterXadIfdInLocation}) {
        if (!finiteMachineSignal(inputs[inputIndex])) {
            throw InvalidParameterValue("IEEET3 requires synchronous-machine dq and field signals");
        }
    }
    const double fieldVoltage = desiredOutput[0];
    const double source = sourceVoltage(inputs);
    const double v40Argument =
        (source * source) - std::pow(0.78 * inputs[exciterXadIfdInLocation], 2.0);
    const double v40 = (v40Argument > 0.0) ? std::sqrt(v40Argument) : 0.0;
    const double regulator = (Ke * fieldVoltage) - v40;
    if ((regulator < Vrmin - initializationTolerance) ||
        (regulator > upperRegulatorLimit(Vrmax) + initializationTolerance)) {
        throw InvalidParameterValue("IEEET3 initial regulator output outside limits");
    }
    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[regulatorState] = regulator;
    state[feedbackState] = fieldVoltage;
    state[fieldState] = fieldVoltage;
    vBias = (regulator / Ka) + state[voltageMeasurementState] - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation];
    const double guard = 10.0 * limiterRootTolerance + std::abs(Ka) * 1e-5;
    if ((Vrmax != 0.0) && (regulator >= upperRegulatorLimit(Vrmax))) {
        vBias += guard / Ka;
    } else if (regulator <= Vrmin) {
        vBias -= guard / Ka;
    }
    fieldSet.resize(std::max<std::size_t>(fieldSet.size(), 2U));
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlag(inputs, state, true);
}

void ExciterIEEET3::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluation.fieldOutput - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] = evaluation.rates[index] - loc.dstateLoc[index];
        }
    }
}

void ExciterIEEET3::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    std::copy_n(evaluation.rates.begin(), loc.diffSize, loc.destDiffLoc);
}

void ExciterIEEET3::algebraicUpdate(const IOdata& /*inputs*/,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[0] = loc.diffStateLoc[fieldState];
    }
}

void ExciterIEEET3::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, evaluation.algebraicDerivative);
        if (!isAlgebraicOnly(sMode)) {
            for (index_t column = 0; column < stateCount; ++column) {
                matrixData.assign(loc.algOffset,
                                  loc.diffOffset + column,
                                  evaluation.fieldStateDerivatives[column]);
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t row = 0; row < stateCount; ++row) {
        matrixData.assign(loc.diffOffset + row,
                          loc.algOffset,
                          evaluation.rateAlgebraicDerivatives[row]);
        for (index_t column = 0; column < stateCount; ++column) {
            double value = evaluation.rateStateDerivatives[row][column];
            if (row == column) {
                value -= stateData.cj;
            }
            matrixData.assign(loc.diffOffset + row, loc.diffOffset + column, value);
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            matrixData.assignCheckCol(loc.diffOffset + row,
                                      inputLocs[input],
                                      evaluation.rateInputDerivatives[row][input]);
        }
    }
}

void ExciterIEEET3::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t index = 0; index < stateCount; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    updateLimitFlag(inputs, m_state.data() + 1, true);
    m_state[0] = m_state[fieldState + 1];
    prevTime = time;
}

bool ExciterIEEET3::updateLimitFlag(const IOdata& inputs, double state[], bool projectState)
{
    constexpr double tolerance = 1e-7;
    const double upper = upperRegulatorLimit(Vrmax);
    const auto evaluation = evaluate(inputs, m_state[0], state);
    bool limited = opFlags[REGULATOR_LIMITED];
    bool high = opFlags[REGULATOR_LIMIT_HIGH];
    const bool above = state[regulatorState] > upper + tolerance;
    const bool below = state[regulatorState] < Vrmin - tolerance;
    if (projectState) {
        state[regulatorState] =
            std::clamp(state[regulatorState], static_cast<double>(Vrmin), upper);
    }
    if (limited) {
        const bool release =
            high ? (evaluation.regulatorDrive < 0.0) : (evaluation.regulatorDrive > 0.0);
        if (release) {
            limited = false;
        }
    } else if (above ||
               ((state[regulatorState] >= upper - tolerance) &&
                (evaluation.regulatorDrive > 0.0))) {
        limited = true;
        high = true;
    } else if (below ||
               ((state[regulatorState] <= Vrmin + tolerance) &&
                (evaluation.regulatorDrive < 0.0))) {
        limited = true;
        high = false;
    }
    const bool changed = (limited != opFlags[REGULATOR_LIMITED]) ||
        (limited && (high != opFlags[REGULATOR_LIMIT_HIGH]));
    opFlags.set(REGULATOR_LIMITED, limited);
    if (limited) {
        opFlags.set(REGULATOR_LIMIT_HIGH, high);
    }
    return changed;
}

void ExciterIEEET3::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    const index_t root = offsets.getRootOffset(sMode);
    if (opFlags[REGULATOR_LIMITED]) {
        roots[root] = (opFlags[REGULATOR_LIMIT_HIGH] ? -evaluation.regulatorDrive :
                                                       evaluation.regulatorDrive) +
            limiterRootTolerance;
    } else {
        const double upper = upperRegulatorLimit(Vrmax);
        roots[root] = std::min(upper - loc.diffStateLoc[regulatorState],
                               loc.diffStateLoc[regulatorState] - Vrmin) +
            limiterRootTolerance;
        opFlags.set(REGULATOR_LIMIT_HIGH, loc.diffStateLoc[regulatorState] >= upper);
    }
}

void ExciterIEEET3::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    if ((rootMask[offsets.getRootOffset(sMode)] != 0) &&
        updateLimitFlag(inputs, m_state.data() + 1, true)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterIEEET3::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    if (updateLimitFlag(inputs, m_state.data() + 1, true)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
