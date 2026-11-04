/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterESST1A.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t firstLeadLagState = 1;
    constexpr index_t secondLeadLagState = 2;
    constexpr index_t amplifierState = 3;
    constexpr index_t feedbackState = 4;

}  // namespace

const ModelDescription& ExciterESST1A::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterESST1A>(
        "ExciterESST1A",
        &Exciter::getModelDescription(),
        {parameter({"tr"}, &ExciterESST1A::Tr, units::second, "Voltage measurement time constant"),
         parameter({"vimax"}, &ExciterESST1A::Vimax, units::one, "Vimax upper limit"),
         parameter({"vimin"}, &ExciterESST1A::Vimin, units::one, "Vimin lower limit"),
         parameter({"tb"}, &ExciterESST1A::Tb, units::second, "Lead-lag denominator time constant"),
         parameter({"tc"}, &ExciterESST1A::Tc, units::second, "Lead-lag numerator time constant"),
         parameter({"tb1"}, &ExciterESST1A::Tb1, units::second, "Tb1 time constant"),
         parameter({"tc1"}, &ExciterESST1A::Tc1, units::second, "Tc1 time constant"),
         parameter({"vamax"}, &ExciterESST1A::Vamax, units::one, "Vamax upper limit"),
         parameter({"vamin"}, &ExciterESST1A::Vamin, units::one, "Vamin lower limit"),
         parameter({"ilr"}, &ExciterESST1A::Ilr, units::one, "Ilr model setting"),
         parameter({"klr"}, &ExciterESST1A::Klr, units::one, "Klr gain or coefficient"),
         parameter({"kc"}, &ExciterESST1A::Kc, units::one, "Kc gain or coefficient"),
         parameter({"kf"}, &ExciterESST1A::Kf, units::one, "Kf gain or coefficient"),
         parameter({"tf"}, &ExciterESST1A::Tf, units::second, "Feedback time constant"),
         parameter({"uel"}, &ExciterESST1A::uelSelector, units::one, "uelSelector model setting"),
         parameter(
             {"vos", "vosc"}, &ExciterESST1A::vosSelector, units::one, "vosSelector model setting"),
         localState("amplifier_limited",
                    &ExciterESST1A::opFlags,
                    ExciterESST1A::AMPLIFIER_LIMITED,
                    "Retained amplifier_limited mode"),
         localState("amplifier_limit_high",
                    &ExciterESST1A::opFlags,
                    ExciterESST1A::AMPLIFIER_LIMIT_HIGH,
                    "Retained amplifier_limit_high mode"),
         state("efd", units::one, "Field voltage"),
         state("vmeas", units::one, "Measured terminal voltage"),
         state("ll1", units::one, "ll1 solver state"),
         state("ll2", units::one, "ll2 solver state"),
         state("va", units::one, "Amplifier state"),
         state("rf", units::one, "rf solver state")});
    return description;
}

void ExciterESST1A::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ExciterESST1A::getModelDescription();
    getParamString<ExciterESST1A, Exciter>(this,
                                           pstr,
                                           description.numericNames(),
                                           description.stringNames(),
                                           description.flagNames(),
                                           pstype);
}

ExciterESST1A::ExciterESST1A(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 400.0;
    Ta = 0.02;
    Vrmax = 9.0;
    Vrmin = -5.43;
}

CoreObject* ExciterESST1A::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterESST1A, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Vimax = Vimax;
    result->Vimin = Vimin;
    result->Tb = Tb;
    result->Tc = Tc;
    result->Tb1 = Tb1;
    result->Tc1 = Tc1;
    result->Vamax = Vamax;
    result->Vamin = Vamin;
    result->Ilr = Ilr;
    result->Klr = Klr;
    result->Kc = Kc;
    result->Kf = Kf;
    result->Tf = Tf;
    result->uelSelector = uelSelector;
    result->vosSelector = vosSelector;
    return result;
}

void ExciterESST1A::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(Tr) || !std::isfinite(Vimax) || !std::isfinite(Vimin) ||
        !std::isfinite(Tb) || !std::isfinite(Tc) || !std::isfinite(Tb1) || !std::isfinite(Tc1) ||
        !std::isfinite(Vamax) || !std::isfinite(Vamin) || !std::isfinite(Ka) ||
        !std::isfinite(Ta) || !std::isfinite(Ilr) || !std::isfinite(Klr) || !std::isfinite(Vrmax) ||
        !std::isfinite(Vrmin) || !std::isfinite(Kf) || !std::isfinite(Tf) || !std::isfinite(Kc) ||
        (Tr < 0.0) || (Tb < 0.0) || (Tc < 0.0) || (Tb1 < 0.0) || (Tc1 < 0.0) || (Ka <= 0.0) ||
        (Ta <= 0.0) || (Tf <= 0.0) || (Vimax < Vimin) || (Vamax < Vamin) || (Vrmax < Vrmin) ||
        (Klr < 0.0) || (uelSelector < 1) || (uelSelector > 3) || (vosSelector < 1) ||
        (vosSelector > 2)) {
        throw InvalidParameterValue("ESST1A gains, limits, selectors, or time constants");
    }
    if (uelSelector != 1) {
        throw InvalidParameterValue("ESST1A UEL selector requires unavailable auxiliary input");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 5;
    offsets.local().local.jacSize = 36;
    offsets.local().local.algRoots = 1;
}

double ExciterESST1A::leadLagOneOutput(double input, double state) const
{
    return (Tb > 0.0) ? (((Tc * input) + ((Tb - Tc) * state)) / Tb) : input;
}

double ExciterESST1A::leadLagTwoOutput(double input, double state) const
{
    return (Tb1 > 0.0) ? (((Tc1 * input) + ((Tb1 - Tc1) * state)) / Tb1) : input;
}

ExciterESST1A::Signals ExciterESST1A::evaluate(const IOdata& inputs, const double state[]) const
{
    Signals result{};
    result.measuredVoltage =
        (Tr > 0.0) ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
    result.currentLimit = std::max(0.0, Klr * (inputs[exciterXadIfdInLocation] - Ilr));
    result.currentLimited = result.currentLimit > 0.0;
    result.fieldDrive = state[amplifierState] - result.currentLimit +
        ((vosSelector == 2) ? inputs[exciterVssInLocation] : 0.0);
    result.feedback = (Kf * result.fieldDrive / Tf) - state[feedbackState];
    result.input = Vref + vBias + ((vosSelector == 1) ? inputs[exciterVssInLocation] : 0.0) -
        result.measuredVoltage - result.feedback + inputs[exciterVsetInLocation] - 1.0;
    result.limitedInput =
        std::clamp(result.input, static_cast<double>(Vimin), static_cast<double>(Vimax));
    result.inputLimited = (result.input <= Vimin) || (result.input >= Vimax);
    result.leadLagOne = leadLagOneOutput(result.limitedInput, state[firstLeadLagState]);
    result.leadLagTwo = leadLagTwoOutput(result.leadLagOne, state[secondLeadLagState]);
    const double lower = Vrmin * inputs[exciterVoltageInLocation];
    const double upper =
        (Vrmax * inputs[exciterVoltageInLocation]) - (Kc * inputs[exciterXadIfdInLocation]);
    // Written explicitly instead of std::clamp so a transiently inverted pair
    // of variable bounds remains deterministic (the upper bound takes priority).
    result.outputUpperLimited = (upper < lower) || (result.fieldDrive >= upper);
    result.outputLimited = result.outputUpperLimited || (result.fieldDrive <= lower);
    result.output = result.fieldDrive;
    if (result.outputUpperLimited) {
        result.output = upper;
    } else if (result.fieldDrive <= lower) {
        result.output = lower;
    }
    result.amplifierDrive = (Ka * result.leadLagTwo) - state[amplifierState];
    return result;
}

void ExciterESST1A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if ((inputs.size() < exciterInputCount) || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterXadIfdInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue(
            "ESST1A requires finite voltage, setpoint, field-current, and stabilizer signals");
    }
    const double fieldVoltage = desiredOutput[0];
    const double currentLimit = std::max(0.0, Klr * (inputs[exciterXadIfdInLocation] - Ilr));
    const double stabilizer = inputs[exciterVssInLocation];
    const double amplifierVoltage =
        fieldVoltage + currentLimit - ((vosSelector == 2) ? stabilizer : 0.0);
    const double initialInput = amplifierVoltage / Ka;
    const double originalAmplifierUpperLimit = Vamax;
    const double lower = Vrmin * inputs[exciterVoltageInLocation];
    const double upper =
        (Vrmax * inputs[exciterVoltageInLocation]) - (Kc * inputs[exciterXadIfdInLocation]);
    if ((upper < lower) || (fieldVoltage < lower - 1e-8) || (fieldVoltage > upper + 1e-8) ||
        (amplifierVoltage < Vamin - 1e-8) ||
        !adjustInitialUpperLimit(amplifierVoltage, Vamax, "ESST1A initial amplifier output")) {
        throw InvalidParameterValue("ESST1A initial field voltage outside limits");
    }
    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[firstLeadLagState] = initialInput;
    state[secondLeadLagState] = initialInput;
    state[amplifierState] = amplifierVoltage;
    state[feedbackState] = Kf * fieldVoltage / Tf;
    vBias = initialInput + state[voltageMeasurementState] - Vref -
        ((vosSelector == 1) ? stabilizer : 0.0) - inputs[exciterVsetInLocation] + 1.0;
    const double correction = std::max(0.0, Vamax - originalAmplifierUpperLimit);
    const double guard = 10.0 * limiterRootTolerance + std::abs(Ka) * 1e-5;
    double inputOverdrive = 0.0;
    if (amplifierVoltage >= Vamax) {
        inputOverdrive = std::max(0.2 * correction, guard) / Ka;
    } else if (amplifierVoltage <= Vamin) {
        inputOverdrive = -guard / Ka;
    }
    if ((inputOverdrive != 0.0) && (initialInput + inputOverdrive <= Vimax) &&
        (initialInput + inputOverdrive >= Vimin)) {
        // Both lead-lag states must follow the changed reference to keep
        // their initial derivatives zero while the amplifier stays limited.
        vBias += inputOverdrive;
        state[firstLeadLagState] += inputOverdrive;
        state[secondLeadLagState] += inputOverdrive;
        opFlags.set(AMPLIFIER_LIMITED);
        opFlags.set(AMPLIFIER_LIMIT_HIGH, inputOverdrive > 0.0);
    }
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

void ExciterESST1A::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const auto signals = evaluate(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] = signals.output - locations.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < 5; ++index) {
            locations.destDiffLoc[index] -= locations.dstateLoc[index];
        }
    }
}

void ExciterESST1A::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* dst = locations.destDiffLoc;
    const auto signals = evaluate(inputs, state);
    dst[voltageMeasurementState] =
        (Tr > 0.0) ? (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr : 0.0;
    dst[firstLeadLagState] =
        (Tb > 0.0) ? (signals.limitedInput - state[firstLeadLagState]) / Tb : 0.0;
    dst[secondLeadLagState] =
        (Tb1 > 0.0) ? (signals.leadLagOne - state[secondLeadLagState]) / Tb1 : 0.0;
    dst[amplifierState] = opFlags[AMPLIFIER_LIMITED] ? 0.0 : signals.amplifierDrive / Ta;
    dst[feedbackState] = (-state[feedbackState] + ((Kf * signals.fieldDrive) / Tf)) / Tf;
}

void ExciterESST1A::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto locations = offsets.getLocations(stateData, update, sMode, this);
        locations.destLoc[0] = evaluate(inputs, locations.diffStateLoc).output;
    }
}

void ExciterESST1A::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto algRow = locations.algOffset;
    const auto diffRow = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const auto signals = evaluate(inputs, state);
    const auto voltageLoc = inputLocs[exciterVoltageInLocation];
    const auto setpointLoc = inputLocs[exciterVsetInLocation];
    const auto fieldCurrentLoc = inputLocs[exciterXadIfdInLocation];
    const auto stabilizerLoc = inputLocs[exciterVssInLocation];
    const double rawFieldCurrentDerivative = signals.currentLimited ? -Klr : 0.0;
    const double rawStabilizerDerivative = (vosSelector == 2) ? 1.0 : 0.0;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(algRow, algRow, -1.0);
        if (!signals.outputLimited) {
            if (!isAlgebraicOnly(sMode)) {
                matrixData.assign(algRow, diffRow + amplifierState, 1.0);
            }
            matrixData.assignCheckCol(algRow, fieldCurrentLoc, rawFieldCurrentDerivative);
            matrixData.assignCheckCol(algRow, stabilizerLoc, rawStabilizerDerivative);
        } else if (signals.outputUpperLimited) {
            matrixData.assignCheckCol(algRow, voltageLoc, Vrmax);
            matrixData.assignCheckCol(algRow, fieldCurrentLoc, -Kc);
        } else {
            matrixData.assignCheckCol(algRow, voltageLoc, Vrmin);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    // Transducer (or its fixed bypass state).
    matrixData.assign(diffRow + voltageMeasurementState,
                      diffRow + voltageMeasurementState,
                      (Tr > 0.0) ? ((-1.0 / Tr) - stateData.cj) : -stateData.cj);
    if (Tr > 0.0) {
        matrixData.assignCheckCol(diffRow + voltageMeasurementState, voltageLoc, 1.0 / Tr);
    }

    // d(v_i)/d(.) after the input limiter. The rate feedback is based on the
    // pre-rectifier field drive, as in the IEEE derivative-feedback block.
    const double inputScale = signals.inputLimited ? 0.0 : 1.0;
    const double inputMeasuredStateDerivative = (Tr > 0.0) ? -inputScale : 0.0;
    const double inputVoltageDerivative = (Tr > 0.0) ? 0.0 : -inputScale;
    const double inputSetpointDerivative = inputScale;
    const double inputAmplifierDerivative = -inputScale * Kf / Tf;
    const double inputFeedbackDerivative = inputScale;
    const double inputFieldCurrentDerivative = -inputScale * Kf * rawFieldCurrentDerivative / Tf;
    const double inputStabilizerDerivative =
        inputScale * (((vosSelector == 1) ? 1.0 : 0.0) - ((Kf * rawStabilizerDerivative) / Tf));
    const double leadOneInputGain = (Tb > 0.0) ? Tc / Tb : 1.0;
    const double leadOneStateGain = (Tb > 0.0) ? (1.0 - (Tc / Tb)) : 0.0;
    const double leadTwoInputGain = (Tb1 > 0.0) ? Tc1 / Tb1 : 1.0;
    const double leadTwoStateGain = (Tb1 > 0.0) ? (1.0 - (Tc1 / Tb1)) : 0.0;

    matrixData.assign(diffRow + firstLeadLagState,
                      diffRow + firstLeadLagState,
                      (Tb > 0.0) ? ((-1.0 / Tb) - stateData.cj) : -stateData.cj);
    if (Tb > 0.0) {
        matrixData.assign(diffRow + firstLeadLagState,
                          diffRow + voltageMeasurementState,
                          inputMeasuredStateDerivative / Tb);
        matrixData.assign(diffRow + firstLeadLagState,
                          diffRow + amplifierState,
                          inputAmplifierDerivative / Tb);
        matrixData.assign(diffRow + firstLeadLagState,
                          diffRow + feedbackState,
                          inputFeedbackDerivative / Tb);
        matrixData.assignCheckCol(diffRow + firstLeadLagState,
                                  fieldCurrentLoc,
                                  inputFieldCurrentDerivative / Tb);
        matrixData.assignCheckCol(diffRow + firstLeadLagState,
                                  setpointLoc,
                                  inputSetpointDerivative / Tb);
        matrixData.assignCheckCol(diffRow + firstLeadLagState,
                                  stabilizerLoc,
                                  inputStabilizerDerivative / Tb);
        if (Tr <= 0.0) {
            matrixData.assignCheckCol(diffRow + firstLeadLagState,
                                      voltageLoc,
                                      inputVoltageDerivative / Tb);
        }
    }

    matrixData.assign(diffRow + secondLeadLagState,
                      diffRow + secondLeadLagState,
                      (Tb1 > 0.0) ? ((-1.0 / Tb1) - stateData.cj) : -stateData.cj);
    if (Tb1 > 0.0) {
        matrixData.assign(diffRow + secondLeadLagState,
                          diffRow + firstLeadLagState,
                          leadOneStateGain / Tb1);
        matrixData.assign(diffRow + secondLeadLagState,
                          diffRow + voltageMeasurementState,
                          leadOneInputGain * inputMeasuredStateDerivative / Tb1);
        matrixData.assign(diffRow + secondLeadLagState,
                          diffRow + amplifierState,
                          leadOneInputGain * inputAmplifierDerivative / Tb1);
        matrixData.assign(diffRow + secondLeadLagState,
                          diffRow + feedbackState,
                          leadOneInputGain * inputFeedbackDerivative / Tb1);
        matrixData.assignCheckCol(diffRow + secondLeadLagState,
                                  fieldCurrentLoc,
                                  leadOneInputGain * inputFieldCurrentDerivative / Tb1);
        matrixData.assignCheckCol(diffRow + secondLeadLagState,
                                  setpointLoc,
                                  leadOneInputGain * inputSetpointDerivative / Tb1);
        matrixData.assignCheckCol(diffRow + secondLeadLagState,
                                  stabilizerLoc,
                                  leadOneInputGain * inputStabilizerDerivative / Tb1);
        if (Tr <= 0.0) {
            matrixData.assignCheckCol(diffRow + secondLeadLagState,
                                      voltageLoc,
                                      leadOneInputGain * inputVoltageDerivative / Tb1);
        }
    }

    if (opFlags[AMPLIFIER_LIMITED]) {
        matrixData.assign(diffRow + amplifierState, diffRow + amplifierState, -stateData.cj);
    } else {
        matrixData.assign(diffRow + amplifierState,
                          diffRow + secondLeadLagState,
                          Ka * leadTwoStateGain / Ta);
        matrixData.assign(diffRow + amplifierState,
                          diffRow + firstLeadLagState,
                          Ka * leadTwoInputGain * leadOneStateGain / Ta);
        matrixData.assign(diffRow + amplifierState,
                          diffRow + voltageMeasurementState,
                          Ka * leadTwoInputGain * leadOneInputGain * inputMeasuredStateDerivative /
                              Ta);
        matrixData.assign(
            diffRow + amplifierState,
            diffRow + amplifierState,
            ((-1.0 / Ta) - stateData.cj) +
                ((Ka * leadTwoInputGain * leadOneInputGain * inputAmplifierDerivative) / Ta));
        matrixData.assign(diffRow + amplifierState,
                          diffRow + feedbackState,
                          Ka * leadTwoInputGain * leadOneInputGain * inputFeedbackDerivative / Ta);
        matrixData.assignCheckCol(diffRow + amplifierState,
                                  fieldCurrentLoc,
                                  Ka * leadTwoInputGain * leadOneInputGain *
                                      inputFieldCurrentDerivative / Ta);
        matrixData.assignCheckCol(diffRow + amplifierState,
                                  setpointLoc,
                                  Ka * leadTwoInputGain * leadOneInputGain *
                                      inputSetpointDerivative / Ta);
        matrixData.assignCheckCol(diffRow + amplifierState,
                                  stabilizerLoc,
                                  Ka * leadTwoInputGain * leadOneInputGain *
                                      inputStabilizerDerivative / Ta);
        if (Tr <= 0.0) {
            matrixData.assignCheckCol(diffRow + amplifierState,
                                      voltageLoc,
                                      Ka * leadTwoInputGain * leadOneInputGain *
                                          inputVoltageDerivative / Ta);
        }
    }

    matrixData.assign(diffRow + feedbackState, diffRow + feedbackState, (-1.0 / Tf) - stateData.cj);
    matrixData.assign(diffRow + feedbackState, diffRow + amplifierState, Kf / (Tf * Tf));
    matrixData.assignCheckCol(diffRow + feedbackState,
                              fieldCurrentLoc,
                              Kf * rawFieldCurrentDerivative / (Tf * Tf));
    matrixData.assignCheckCol(diffRow + feedbackState,
                              stabilizerLoc,
                              Kf * rawStabilizerDerivative / (Tf * Tf));
}

void ExciterESST1A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double step = time - prevTime;
    for (index_t index = 0; index < 5; ++index) {
        m_state[index + 1] += step * m_dstate_dt[index + 1];
    }
    m_state[amplifierState + 1] = std::clamp(m_state[amplifierState + 1],
                                             static_cast<double>(Vamin),
                                             static_cast<double>(Vamax));
    const auto signals = evaluate(inputs, m_state.data() + 1);
    m_state[0] = signals.output;
    prevTime = time;
}

void ExciterESST1A::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const index_t root = offsets.getRootOffset(sMode);
    if (opFlags[AMPLIFIER_LIMITED]) {
        const double drive = evaluate(inputs, state).amplifierDrive;
        roots[root] = (opFlags[AMPLIFIER_LIMIT_HIGH] ? -drive : drive) + limiterRootTolerance;
    } else {
        roots[root] = std::min(Vamax - state[amplifierState], state[amplifierState] - Vamin) +
            limiterRootTolerance;
        opFlags.set(AMPLIFIER_LIMIT_HIGH, state[amplifierState] >= Vamax);
    }
}

void ExciterESST1A::rootTrigger(CoreTime /*time*/,
                                const IOdata& /*inputs*/,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    if (rootMask[offsets.getRootOffset(sMode)] == 0) {
        return;
    }
    if (opFlags[AMPLIFIER_LIMITED]) {
        opFlags.reset(AMPLIFIER_LIMITED);
        alert(this, JAC_COUNT_INCREASE);
    } else {
        opFlags.set(AMPLIFIER_LIMITED);
        alert(this, JAC_COUNT_DECREASE);
    }
}

ChangeCode ExciterESST1A::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    double* state = m_state.data() + 1;
    const double drive = evaluate(inputs, state).amplifierDrive;
    if (opFlags[AMPLIFIER_LIMITED]) {
        const bool release = opFlags[AMPLIFIER_LIMIT_HIGH] ? (drive < 0.0) : (drive > 0.0);
        if (!release) {
            return ChangeCode::NO_CHANGE;
        }
        opFlags.reset(AMPLIFIER_LIMITED);
        alert(this, JAC_COUNT_INCREASE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    constexpr double limitTolerance = 1e-7;
    if ((state[amplifierState] <= Vamax + limitTolerance) &&
        (state[amplifierState] >= Vamin - limitTolerance)) {
        return ChangeCode::NO_CHANGE;
    }
    opFlags.set(AMPLIFIER_LIMIT_HIGH, state[amplifierState] > Vamax);
    opFlags.set(AMPLIFIER_LIMITED);
    state[amplifierState] =
        std::clamp(state[amplifierState], static_cast<double>(Vamin), static_cast<double>(Vamax));
    m_dstate_dt[amplifierState + 1] = 0.0;
    alert(this, JAC_COUNT_DECREASE);
    return ChangeCode::JACOBIAN_CHANGE;
}

void ExciterESST1A::set(std::string_view param, std::string_view val)
{
    if (!ExciterESST1A::getModelDescription().trySetLocal(*this, param, val)) {
        Exciter::set(param, val);
    }
}

void ExciterESST1A::set(std::string_view param, double val, units::unit unitType)
{
    if (!ExciterESST1A::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        Exciter::set(param, val, unitType);
    }
}

double ExciterESST1A::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            ExciterESST1A::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return Exciter::get(param, unitType);
}

stringVec ExciterESST1A::localStateNames() const
{
    return {"efd", "vmeas", "ll1", "ll2", "va", "rf"};
}

index_t ExciterESST1A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") {
        return offset + voltageMeasurementState;
    }
    if (field == "ll1") {
        return offset + firstLeadLagState;
    }
    if (field == "ll2") {
        return offset + secondLeadLagState;
    }
    if (field == "va") {
        return offset + amplifierState;
    }
    if (field == "rf") {
        return offset + feedbackState;
    }
    return kInvalidLocation;
}

}  // namespace griddyn::exciters
