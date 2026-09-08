/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterESST2A.h"

#include "ExciterSignalHelper.h"
#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
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

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

ExciterESST2A::ExciterESST2A(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 240.0;
    Ta = 0.01;
    Vrmax = 4.5;
    Vrmin = -4.5;
}

CoreObject* ExciterESST2A::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterESST2A, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Kp = Kp;
    result->Ki = Ki;
    result->Kc = Kc;
    result->Kf = Kf;
    result->Tf = Tf;
    result->Ke = Ke;
    result->Te = Te;
    result->Efdmax = Efdmax;
    return result;
}

void ExciterESST2A::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 14> parameters{
        Tr, Vrmax, Vrmin, Ka, Ta, Kp, Ki, Kc, Kf, Tf, Ke, Te, Efdmax, Vref};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Tr < 0.0) || (Ka <= 0.0) || (Ta <= 0.0) || (Vrmax < Vrmin) || (Tf <= 0.0) || (Te <= 0.0) ||
        (Efdmax < 0.0)) {
        throw InvalidParameterValue("ESST2A gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.jacSize = 80;
    offsets.local().local.algRoots = 2;
}

double ExciterESST2A::rectifierVoltage(const IOdata& inputs) const
{
    if ((Kp <= 0.0) && (Ki <= 0.0)) {
        return 1.0;
    }
    return detail::computeRectifierData(
               inputs, Kp, Ki, Kc, 0.0, 0.0, std::numeric_limits<double>::infinity())
        .voltage;
}

ExciterESST2A::Evaluation
    ExciterESST2A::evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const
{
    using detail::addSignals;
    using detail::algebraicSignal;
    using detail::clampSignal;
    using detail::constantSignal;
    using detail::inputSignal;
    using detail::scaleSignal;
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

    Signal rectifier = constantSignal<stateCount>(1.0);
    if ((Kp > 0.0) || (Ki > 0.0)) {
        const auto data = detail::computeRectifierData(
            inputs, Kp, Ki, Kc, 0.0, 0.0, std::numeric_limits<double>::infinity());
        rectifier.value = data.voltage;
        const std::array<index_t, 5> signalIndices{exciterIdInLocation,
                                                   exciterIqInLocation,
                                                   exciterVdInLocation,
                                                   exciterVqInLocation,
                                                   exciterXadIfdInLocation};
        for (index_t index = 0; index < 5; ++index) {
            rectifier.input[signalIndices[index]] = data.derivatives[index];
        }
    }

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

    const Signal fieldDrive = subtractSignals(detail::multiplySignals(rectifier, regulator),
                                              scaleSignal(fieldStateSignal, Ke));
    if (!opFlags[FIELD_LIMITED]) {
        rates[fieldState] = scaleSignal(fieldDrive, 1.0 / Te);
    }
    const Signal algebraicResidual =
        subtractSignals(fieldStateSignal, algebraicSignal<stateCount>(fieldVoltage));

    evaluation.fieldOutput = state[fieldState];
    evaluation.regulatorDrive = regulatorDrive.value;
    evaluation.fieldDrive = fieldDrive.value;
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

void ExciterESST2A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("ESST2A initial voltage signals");
    }
    if ((Kp > 0.0) || (Ki > 0.0)) {
        for (const index_t inputIndex : {exciterIdInLocation,
                                         exciterIqInLocation,
                                         exciterVdInLocation,
                                         exciterVqInLocation,
                                         exciterXadIfdInLocation}) {
            if (!finiteMachineSignal(inputs[inputIndex])) {
                throw InvalidParameterValue(
                    "ESST2A requires synchronous-machine dq and field signals");
            }
        }
    }
    const double fieldVoltage = desiredOutput[0];
    const double rectifier = rectifierVoltage(inputs);
    if (std::abs(rectifier) <= 1e-12) {
        throw InvalidParameterValue("ESST2A initial rectifier voltage");
    }
    const double regulator = Ke * fieldVoltage / rectifier;
    if ((regulator < Vrmin - initializationTolerance) ||
        (regulator > Vrmax + initializationTolerance) ||
        (fieldVoltage < -initializationTolerance) ||
        (fieldVoltage > Efdmax + initializationTolerance)) {
        throw InvalidParameterValue("ESST2A initial state outside limits");
    }
    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[regulatorState] = regulator;
    state[feedbackState] = fieldVoltage;
    state[fieldState] = fieldVoltage;
    vBias = (regulator / Ka) + state[voltageMeasurementState] - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation];
    fieldSet.resize(std::max<std::size_t>(fieldSet.size(), 2U));
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state, true);
}

void ExciterESST2A::residual(const IOdata& inputs,
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

void ExciterESST2A::derivative(const IOdata& inputs,
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

void ExciterESST2A::algebraicUpdate(const IOdata& /*inputs*/,
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

void ExciterESST2A::jacobianElements(const IOdata& inputs,
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

void ExciterESST2A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t index = 0; index < stateCount; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    updateLimitFlags(inputs, m_state.data() + 1, true);
    m_state[0] = m_state[fieldState + 1];
    prevTime = time;
}

bool ExciterESST2A::updateLimitFlags(const IOdata& inputs, double state[], bool projectStates)
{
    constexpr double tolerance = 1e-7;
    const auto evaluation = evaluate(inputs, m_state[0], state);
    bool changed = false;
    const auto setLimited = [this,
                             &changed](int limitedFlag, int highFlag, bool limited, bool high) {
        if (opFlags[limitedFlag] != limited) {
            opFlags.set(limitedFlag, limited);
            changed = true;
        }
        if (limited && (opFlags[highFlag] != high)) {
            opFlags.set(highFlag, high);
            changed = true;
        }
    };

    const bool regulatorAbove = state[regulatorState] > Vrmax + tolerance;
    const bool regulatorBelow = state[regulatorState] < Vrmin - tolerance;
    if (projectStates) {
        state[regulatorState] = std::clamp(state[regulatorState],
                                           static_cast<double>(Vrmin),
                                           static_cast<double>(Vrmax));
    }
    if (opFlags[REGULATOR_LIMITED]) {
        const bool release = opFlags[REGULATOR_LIMIT_HIGH] ? (evaluation.regulatorDrive < 0.0) :
                                                             (evaluation.regulatorDrive > 0.0);
        if (release) {
            setLimited(REGULATOR_LIMITED, REGULATOR_LIMIT_HIGH, false, false);
        }
    } else if (regulatorAbove ||
               ((state[regulatorState] >= Vrmax - tolerance) &&
                (evaluation.regulatorDrive > 0.0))) {
        setLimited(REGULATOR_LIMITED, REGULATOR_LIMIT_HIGH, true, true);
    } else if (regulatorBelow ||
               ((state[regulatorState] <= Vrmin + tolerance) &&
                (evaluation.regulatorDrive < 0.0))) {
        setLimited(REGULATOR_LIMITED, REGULATOR_LIMIT_HIGH, true, false);
    }

    const bool fieldAbove = state[fieldState] > Efdmax + tolerance;
    const bool fieldBelow = state[fieldState] < -tolerance;
    if (projectStates) {
        state[fieldState] = std::clamp(state[fieldState], 0.0, static_cast<double>(Efdmax));
    }
    if (opFlags[FIELD_LIMITED]) {
        const bool release = opFlags[FIELD_LIMIT_HIGH] ? (evaluation.fieldDrive < 0.0) :
                                                         (evaluation.fieldDrive > 0.0);
        if (release) {
            setLimited(FIELD_LIMITED, FIELD_LIMIT_HIGH, false, false);
        }
    } else if (fieldAbove ||
               ((state[fieldState] >= Efdmax - tolerance) && (evaluation.fieldDrive > 0.0))) {
        setLimited(FIELD_LIMITED, FIELD_LIMIT_HIGH, true, true);
    } else if (fieldBelow || ((state[fieldState] <= tolerance) && (evaluation.fieldDrive < 0.0))) {
        setLimited(FIELD_LIMITED, FIELD_LIMIT_HIGH, true, false);
    }
    return changed;
}

void ExciterESST2A::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    const index_t root = offsets.getRootOffset(sMode);
    if (opFlags[REGULATOR_LIMITED]) {
        roots[root] =
            opFlags[REGULATOR_LIMIT_HIGH] ? -evaluation.regulatorDrive : evaluation.regulatorDrive;
    } else {
        roots[root] = std::min(Vrmax - loc.diffStateLoc[regulatorState],
                               loc.diffStateLoc[regulatorState] - Vrmin);
        opFlags.set(REGULATOR_LIMIT_HIGH, loc.diffStateLoc[regulatorState] >= Vrmax);
    }
    if (opFlags[FIELD_LIMITED]) {
        roots[root + 1] =
            opFlags[FIELD_LIMIT_HIGH] ? -evaluation.fieldDrive : evaluation.fieldDrive;
    } else {
        roots[root + 1] =
            std::min(Efdmax - loc.diffStateLoc[fieldState], loc.diffStateLoc[fieldState]);
        opFlags.set(FIELD_LIMIT_HIGH, loc.diffStateLoc[fieldState] >= Efdmax);
    }
}

void ExciterESST2A::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    const index_t root = offsets.getRootOffset(sMode);
    if (((rootMask[root] != 0) || (rootMask[root + 1] != 0)) &&
        updateLimitFlags(inputs, m_state.data() + 1, true)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterESST2A::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    if (updateLimitFlags(inputs, m_state.data() + 1, true)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterESST2A::localStateNames() const
{
    return {"efd", "vmeas", "va", "wf", "efstate"};
}

index_t ExciterESST2A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    if ((field == "vmeas") || (field == "vc")) {
        return offset + voltageMeasurementState;
    }
    if ((field == "va") || (field == "regulator")) {
        return offset + regulatorState;
    }
    if ((field == "wf") || (field == "feedback")) {
        return offset + feedbackState;
    }
    if (field == "efstate") {
        return offset + fieldState;
    }
    return kInvalidLocation;
}

void ExciterESST2A::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterESST2A::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tr") {
        Tr = val;
    } else if (param == "kp") {
        Kp = val;
    } else if (param == "ki") {
        Ki = val;
    } else if (param == "kc") {
        Kc = val;
    } else if (param == "kf") {
        Kf = val;
    } else if (param == "tf") {
        Tf = val;
    } else if (param == "ke") {
        Ke = val;
    } else if (param == "te") {
        Te = val;
    } else if ((param == "efdmax") || (param == "efmax")) {
        Efdmax = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterESST2A::get(std::string_view param, units::unit unitType) const
{
    if (param == "ka") {
        return Ka;
    }
    if (param == "ta") {
        return Ta;
    }
    if ((param == "vrmax") || (param == "urmax")) {
        return Vrmax;
    }
    if ((param == "vrmin") || (param == "urmin")) {
        return Vrmin;
    }
    if (param == "tr") {
        return Tr;
    }
    if (param == "kp") {
        return Kp;
    }
    if (param == "ki") {
        return Ki;
    }
    if (param == "kc") {
        return Kc;
    }
    if (param == "kf") {
        return Kf;
    }
    if (param == "tf") {
        return Tf;
    }
    if (param == "ke") {
        return Ke;
    }
    if (param == "te") {
        return Te;
    }
    if ((param == "efdmax") || (param == "efmax")) {
        return Efdmax;
    }
    return Exciter::get(param, unitType);
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
