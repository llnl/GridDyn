/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterAC8B.h"

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
    constexpr index_t pidIntegralState = 1;
    constexpr index_t pidDerivativeState = 2;
    constexpr index_t regulatorState = 3;
    constexpr index_t exciterVoltageState = 4;
    constexpr double initializationTolerance = 1e-7;
    // PSS/E's legacy rectifier initialization uses rounded reciprocal-sqrt(3)
    // and sqrt(3) constants.  These intentionally remain model constants rather
    // than std::numbers::egamma or std::numbers::sqrt3.
    constexpr double rectifierReciprocalSqrtThree = 0.577;
    constexpr double rectifierSqrtThree = 1.732;

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

ExciterAC8B::ExciterAC8B(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 40.0;
    Ta = 0.04;
    Vrmax = 7.3;
    Vrmin = 1.0;
}

CoreObject* ExciterAC8B::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterAC8B, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Kpr = Kpr;
    result->Kir = Kir;
    result->Kdr = Kdr;
    result->Tdr = Tdr;
    result->Vpmax = Vpmax;
    result->Vpmin = Vpmin;
    result->Vemax = Vemax;
    result->Vemin = Vemin;
    result->Te = Te;
    result->Kc = Kc;
    result->Kd = Kd;
    result->Ke = Ke;
    result->E1 = E1;
    result->Se1 = Se1;
    result->E2 = E2;
    result->Se2 = Se2;
    result->saturation = saturation;
    return result;
}

void ExciterAC8B::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 20> parameters{Tr,    Kpr,   Kir,   Kdr,   Tdr, Vpmax, Vpmin,
                                            Vrmax, Vrmin, Vemax, Vemin, Ta,  Ka,    Te,
                                            Kc,    Kd,    Ke,    E1,    Se1, E2};
    const bool disabledSaturation = (Se1 == 0.0) && (Se2 == 0.0);
    const bool invalidSaturation = !disabledSaturation &&
        ((E1 <= 0.0) || (E2 <= 0.0) || (E1 == E2) || (Se1 <= 0.0) || (Se2 <= 0.0));
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        !std::isfinite(Se2) || (Tr < 0.0) || (Tdr < 0.0) || (Ta <= 0.0) || (Te <= 0.0) ||
        (Ka <= 0.0) || (Vpmax < Vpmin) || (Vrmax < Vrmin) || (Vemax < Vemin) || invalidSaturation) {
        throw InvalidParameterValue("AC8B gains, time constants, limits, or saturation points");
    }
    saturation.setType(disabledSaturation ?
                           utilities::Saturation::SaturationType::NONE :
                           utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    if (!disabledSaturation) {
        saturation.setParam(E1, Se1, E2, Se2);
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.jacSize = 96;
    offsets.local().local.algRoots = 3;
}

double ExciterAC8B::solveExciterVoltage(double fieldVoltage, double fieldCurrent) const
{
    const double loading = Kc * fieldCurrent;
    const double middleMagnitude =
        std::sqrt(((fieldVoltage * fieldVoltage) + (loading * loading)) / 0.75);
    const double middleSign = (fieldVoltage != 0.0) ? fieldVoltage : loading;
    const std::array<double, 5> candidates{fieldVoltage,
                                           fieldVoltage + (rectifierReciprocalSqrtThree * loading),
                                           std::copysign(middleMagnitude, middleSign),
                                           (fieldVoltage / rectifierSqrtThree) + loading,
                                           loading};
    double bestVoltage = candidates.front();
    double bestMismatch = std::numeric_limits<double>::infinity();
    for (const double candidate : candidates) {
        const double normalizedCurrent = (std::abs(candidate) > 1e-14) ? loading / candidate : 0.0;
        const double output = candidate * detail::computeRectifierFactor(normalizedCurrent).factor;
        const double mismatch = std::abs(output - fieldVoltage);
        if (mismatch < bestMismatch) {
            bestMismatch = mismatch;
            bestVoltage = candidate;
        }
    }
    if (!std::isfinite(bestVoltage) ||
        (bestMismatch > 1e-9 * std::max(1.0, std::abs(fieldVoltage)))) {
        throw InvalidParameterValue("AC8B initial rectifier equation has no finite solution");
    }
    return bestVoltage;
}

double ExciterAC8B::solveFieldFeedbackLimit(double fieldCurrent, double initialVoltage) const
{
    const double target = Vemax - (Kd * fieldCurrent);
    double voltage = initialVoltage;
    if (!std::isfinite(voltage)) {
        voltage = (std::abs(Ke) > 1e-12) ? target / Ke : Vemax;
    }
    for (int iteration = 0; iteration < 40; ++iteration) {
        const auto sat = saturation.evaluate(voltage);
        const double mismatch = ((Ke + sat.value) * voltage) - target;
        if (std::abs(mismatch) <= 1e-11 * std::max(1.0, std::abs(target))) {
            return voltage;
        }
        const double slope = Ke + sat.value + (voltage * sat.derivative);
        if (!std::isfinite(slope) || (std::abs(slope) < 1e-12)) {
            break;
        }
        voltage -= mismatch / slope;
    }
    throw InvalidParameterValue("AC8B exciter upper-limit equation has no finite solution");
}

ExciterAC8B::Evaluation
    ExciterAC8B::evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const
{
    using detail::addSignals;
    using detail::algebraicSignal;
    using detail::clampSignal;
    using detail::constantSignal;
    using detail::divideSignals;
    using detail::inputSignal;
    using detail::multiplySignals;
    using detail::rectifierFactorSignal;
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

    Signal input = addSignals(constantSignal<stateCount>(Vref + vBias - 1.0),
                              inputSignal<stateCount>(inputs, exciterVsetInLocation));
    input = addSignals(input, inputSignal<stateCount>(inputs, exciterVssInLocation));
    input = subtractSignals(input, measuredVoltage);

    const Signal derivativeState =
        stateSignal<stateCount>(state[pidDerivativeState], pidDerivativeState);
    Signal derivativeOutput = constantSignal<stateCount>(0.0);
    if (Tdr > 0.0) {
        rates[pidDerivativeState] = scaleSignal(subtractSignals(input, derivativeState), 1.0 / Tdr);
        derivativeOutput = scaleSignal(subtractSignals(input, derivativeState), Kdr / Tdr);
    }

    const Signal pidIntegral = stateSignal<stateCount>(state[pidIntegralState], pidIntegralState);
    const Signal pidDrive =
        addSignals(addSignals(scaleSignal(input, Kpr), pidIntegral), derivativeOutput);
    const Signal pidOutput = clampSignal(pidDrive, Vpmin, Vpmax);
    const Signal integratorDrive = scaleSignal(input, Kir);
    if (!opFlags[PID_LIMITED]) {
        rates[pidIntegralState] = integratorDrive;
    }

    const Signal regulator = stateSignal<stateCount>(state[regulatorState], regulatorState);
    const Signal regulatorDrive = subtractSignals(scaleSignal(pidOutput, Ka), regulator);
    if (!opFlags[REGULATOR_LIMITED]) {
        rates[regulatorState] = scaleSignal(regulatorDrive, 1.0 / Ta);
    }

    const Signal exciterVoltage =
        stateSignal<stateCount>(state[exciterVoltageState], exciterVoltageState);
    const auto saturationData = saturation.evaluate(exciterVoltage.value);
    const Signal saturationSignal =
        detail::applyFunction(exciterVoltage, saturationData.value, saturationData.derivative);
    const Signal fieldCurrent = inputSignal<stateCount>(inputs, exciterXadIfdInLocation);
    const Signal saturationGain = addSignals(constantSignal<stateCount>(Ke), saturationSignal);
    const Signal fieldFeedback =
        addSignals(multiplySignals(saturationGain, exciterVoltage), scaleSignal(fieldCurrent, Kd));
    // VEMIN and VFEMAX bound the rotating-exciter state, not its control input.
    // Therefore T_E dV_E/dt = V_R - V_FE uses the unclipped regulator output.
    const Signal exciterDrive = subtractSignals(regulator, fieldFeedback);
    if (!opFlags[EXCITER_LIMITED]) {
        rates[exciterVoltageState] = scaleSignal(exciterDrive, 1.0 / Te);
    }

    const Signal normalizedCurrent = divideSignals(scaleSignal(fieldCurrent, Kc), exciterVoltage);
    const Signal fieldOutput =
        multiplySignals(rectifierFactorSignal(normalizedCurrent), exciterVoltage);
    const Signal algebraicResidual =
        subtractSignals(fieldOutput, algebraicSignal<stateCount>(fieldVoltage));

    evaluation.fieldOutput = fieldOutput.value;
    evaluation.pidDrive = pidDrive.value;
    evaluation.pidIntegratorDrive = integratorDrive.value;
    evaluation.regulatorDrive = regulatorDrive.value;
    evaluation.fieldFeedback = fieldFeedback.value;
    evaluation.exciterDrive = exciterDrive.value;
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

void ExciterAC8B::dynObjectInitializeB(const IOdata& inputs,
                                       const IOdata& desiredOutput,
                                       IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0]) ||
        (((Kc != 0.0) || (Kd != 0.0)) && !finiteMachineSignal(inputs[exciterXadIfdInLocation]))) {
        throw InvalidParameterValue("AC8B initial signals");
    }
    const double fieldVoltage = desiredOutput[0];
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const double exciterVoltage = solveExciterVoltage(fieldVoltage, fieldCurrent);
    const double fieldFeedback =
        (Ke + saturation(exciterVoltage)) * exciterVoltage + (Kd * fieldCurrent);
    const double pidOutput = fieldFeedback / Ka;
    const double rectifierMismatch =
        (exciterVoltage *
         detail::computeRectifierFactor(
             (std::abs(exciterVoltage) > 1e-14) ? Kc * fieldCurrent / exciterVoltage : 0.0)
             .factor) -
        fieldVoltage;
    if ((pidOutput < Vpmin - initializationTolerance) ||
        (pidOutput > Vpmax + initializationTolerance) ||
        (fieldFeedback < Vrmin - initializationTolerance) ||
        (fieldFeedback > Vrmax + initializationTolerance) ||
        (exciterVoltage < Vemin - initializationTolerance) ||
        (fieldFeedback > Vemax + initializationTolerance) ||
        (std::abs(rectifierMismatch) > initializationTolerance)) {
        throw InvalidParameterValue("AC8B initial regulator output outside limits");
    }

    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[pidDerivativeState] = 0.0;
    state[pidIntegralState] = pidOutput;
    state[regulatorState] = fieldFeedback;
    state[exciterVoltageState] = exciterVoltage;
    vBias = state[voltageMeasurementState] - Vref - (inputs[exciterVsetInLocation] - 1.0) -
        inputs[exciterVssInLocation];
    fieldSet.resize(std::max<std::size_t>(fieldSet.size(), 2U));
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state, true);
}

void ExciterAC8B::residual(const IOdata& inputs,
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

void ExciterAC8B::derivative(const IOdata& inputs,
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

void ExciterAC8B::algebraicUpdate(const IOdata& inputs,
                                  const StateData& stateData,
                                  double update[],
                                  const SolverMode& sMode,
                                  double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[0] = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc).fieldOutput;
    }
}

void ExciterAC8B::jacobianElements(const IOdata& inputs,
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
        for (index_t input = 0; input < exciterInputCount; ++input) {
            matrixData.assignCheckCol(loc.algOffset,
                                      inputLocs[input],
                                      evaluation.fieldInputDerivatives[input]);
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

void ExciterAC8B::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t index = 0; index < stateCount; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    updateLimitFlags(inputs, m_state.data() + 1, true);
    m_state[0] = evaluate(inputs, m_state[0], m_state.data() + 1).fieldOutput;
    prevTime = time;
}

bool ExciterAC8B::updateLimitFlags(const IOdata& inputs, double state[], bool projectStates)
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

    if (opFlags[PID_LIMITED]) {
        const bool release = opFlags[PID_LIMIT_HIGH] ?
            ((evaluation.pidDrive < Vpmax - tolerance) || (evaluation.pidIntegratorDrive < 0.0)) :
            ((evaluation.pidDrive > Vpmin + tolerance) || (evaluation.pidIntegratorDrive > 0.0));
        if (release) {
            setLimited(PID_LIMITED, PID_LIMIT_HIGH, false, false);
        }
    } else if ((evaluation.pidDrive >= Vpmax - tolerance) &&
               (evaluation.pidIntegratorDrive > 0.0)) {
        setLimited(PID_LIMITED, PID_LIMIT_HIGH, true, true);
    } else if ((evaluation.pidDrive <= Vpmin + tolerance) &&
               (evaluation.pidIntegratorDrive < 0.0)) {
        setLimited(PID_LIMITED, PID_LIMIT_HIGH, true, false);
    }

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

    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const bool exciterAbove = evaluation.fieldFeedback > Vemax + tolerance;
    const bool exciterBelow = state[exciterVoltageState] < Vemin - tolerance;
    if (projectStates) {
        state[exciterVoltageState] =
            std::max(state[exciterVoltageState], static_cast<double>(Vemin));
        const double voltage = state[exciterVoltageState];
        const double feedback = (Ke + saturation(voltage)) * voltage + (Kd * fieldCurrent);
        if (feedback > Vemax) {
            state[exciterVoltageState] = solveFieldFeedbackLimit(fieldCurrent, voltage);
        }
    }
    if (opFlags[EXCITER_LIMITED]) {
        const bool release = opFlags[EXCITER_LIMIT_HIGH] ?
            ((evaluation.fieldFeedback < Vemax - tolerance) || (evaluation.exciterDrive < 0.0)) :
            ((state[exciterVoltageState] > Vemin + tolerance) || (evaluation.exciterDrive > 0.0));
        if (release) {
            setLimited(EXCITER_LIMITED, EXCITER_LIMIT_HIGH, false, false);
        }
    } else if (exciterAbove ||
               ((evaluation.fieldFeedback >= Vemax - tolerance) &&
                (evaluation.exciterDrive > 0.0))) {
        setLimited(EXCITER_LIMITED, EXCITER_LIMIT_HIGH, true, true);
    } else if (exciterBelow ||
               ((state[exciterVoltageState] <= Vemin + tolerance) &&
                (evaluation.exciterDrive < 0.0))) {
        setLimited(EXCITER_LIMITED, EXCITER_LIMIT_HIGH, true, false);
    }
    return changed;
}

void ExciterAC8B::rootTest(const IOdata& inputs,
                           const StateData& stateData,
                           double roots[],
                           const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    const index_t root = offsets.getRootOffset(sMode);
    if (opFlags[PID_LIMITED]) {
        roots[root] = opFlags[PID_LIMIT_HIGH] ?
            std::max(Vpmax - evaluation.pidDrive, -evaluation.pidIntegratorDrive) :
            std::max(evaluation.pidDrive - Vpmin, evaluation.pidIntegratorDrive);
    } else {
        roots[root] = std::min(Vpmax - evaluation.pidDrive, evaluation.pidDrive - Vpmin);
        opFlags.set(PID_LIMIT_HIGH, evaluation.pidDrive >= Vpmax);
    }
    if (opFlags[REGULATOR_LIMITED]) {
        roots[root + 1] =
            opFlags[REGULATOR_LIMIT_HIGH] ? -evaluation.regulatorDrive : evaluation.regulatorDrive;
    } else {
        roots[root + 1] = std::min(Vrmax - loc.diffStateLoc[regulatorState],
                                   loc.diffStateLoc[regulatorState] - Vrmin);
        opFlags.set(REGULATOR_LIMIT_HIGH, loc.diffStateLoc[regulatorState] >= Vrmax);
    }
    if (opFlags[EXCITER_LIMITED]) {
        roots[root + 2] = opFlags[EXCITER_LIMIT_HIGH] ?
            std::max(Vemax - evaluation.fieldFeedback, -evaluation.exciterDrive) :
            std::max(loc.diffStateLoc[exciterVoltageState] - Vemin, evaluation.exciterDrive);
    } else {
        roots[root + 2] = std::min(Vemax - evaluation.fieldFeedback,
                                   loc.diffStateLoc[exciterVoltageState] - Vemin);
        opFlags.set(EXCITER_LIMIT_HIGH, evaluation.fieldFeedback >= Vemax);
    }
}

void ExciterAC8B::rootTrigger(CoreTime /*time*/,
                              const IOdata& inputs,
                              const std::vector<int>& rootMask,
                              const SolverMode& sMode)
{
    const index_t root = offsets.getRootOffset(sMode);
    if ((rootMask[root] != 0) || (rootMask[root + 1] != 0) || (rootMask[root + 2] != 0)) {
        updateLimitFlags(inputs, m_state.data() + 1, true);
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterAC8B::rootCheck(const IOdata& inputs,
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

stringVec ExciterAC8B::localStateNames() const
{
    return {"efd", "vmeas", "pidint", "pidder", "vr", "ve"};
}

index_t ExciterAC8B::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    if ((field == "vmeas") || (field == "vc")) {
        return offset + voltageMeasurementState;
    }
    if (field == "pidint") {
        return offset + pidIntegralState;
    }
    if (field == "pidder") {
        return offset + pidDerivativeState;
    }
    if ((field == "vr") || (field == "regulator")) {
        return offset + regulatorState;
    }
    if ((field == "ve") || (field == "exciter")) {
        return offset + exciterVoltageState;
    }
    return kInvalidLocation;
}

void ExciterAC8B::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterAC8B::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tr") {
        Tr = val;
    } else if ((param == "kpr") || (param == "kp_pid") || (param == "kp")) {
        Kpr = val;
    } else if ((param == "kir") || (param == "ki_pid") || (param == "ki")) {
        Kir = val;
    } else if ((param == "kdr") || (param == "kd_pid")) {
        Kdr = val;
    } else if ((param == "tdr") || (param == "td")) {
        Tdr = val;
    } else if ((param == "vpmax") || (param == "vpidmax")) {
        Vpmax = val;
    } else if ((param == "vpmin") || (param == "vpidmin")) {
        Vpmin = val;
    } else if ((param == "vfemax") || (param == "vemax")) {
        Vemax = val;
    } else if (param == "vemin") {
        Vemin = val;
    } else if (param == "te") {
        Te = val;
    } else if (param == "kc") {
        Kc = val;
    } else if (param == "kd") {
        Kd = val;
    } else if (param == "ke") {
        Ke = val;
    } else if (param == "e1") {
        E1 = val;
    } else if (param == "se1") {
        Se1 = val;
    } else if (param == "e2") {
        E2 = val;
    } else if (param == "se2") {
        Se2 = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterAC8B::get(std::string_view param, units::unit unitType) const
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
    if ((param == "kpr") || (param == "kp_pid") || (param == "kp")) {
        return Kpr;
    }
    if ((param == "kir") || (param == "ki_pid") || (param == "ki")) {
        return Kir;
    }
    if ((param == "kdr") || (param == "kd_pid")) {
        return Kdr;
    }
    if ((param == "tdr") || (param == "td")) {
        return Tdr;
    }
    if ((param == "vpmax") || (param == "vpidmax")) {
        return Vpmax;
    }
    if ((param == "vpmin") || (param == "vpidmin")) {
        return Vpmin;
    }
    if ((param == "vfemax") || (param == "vemax")) {
        return Vemax;
    }
    if (param == "vemin") {
        return Vemin;
    }
    if (param == "te") {
        return Te;
    }
    if (param == "kc") {
        return Kc;
    }
    if (param == "kd") {
        return Kd;
    }
    if (param == "ke") {
        return Ke;
    }
    if (param == "e1") {
        return E1;
    }
    if (param == "se1") {
        return Se1;
    }
    if (param == "e2") {
        return E2;
    }
    if (param == "se2") {
        return Se2;
    }
    return Exciter::get(param, unitType);
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
