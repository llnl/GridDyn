/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterEXST1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::exciters {
// The block equations are clearer in their published transfer-function form
// than with precedence-only parentheses around every product and quotient.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t leadLagState = 1;
    constexpr index_t regulatorState = 2;
    constexpr index_t washoutState = 3;
    constexpr double limitTolerance = 1e-7;
}  // namespace

ExciterEXST1::ExciterEXST1(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 80.0;
    Ta = 0.05;
    Vrmax = 8.0;
    Vrmin = -3.0;
}

CoreObject* ExciterEXST1::clone(CoreObject* obj) const
{
    auto* exciterClone = cloneBase<ExciterEXST1, Exciter>(this, obj);
    if (exciterClone == nullptr) {
        return obj;
    }
    exciterClone->Tr = Tr;
    exciterClone->Vimax = Vimax;
    exciterClone->Vimin = Vimin;
    exciterClone->Tc = Tc;
    exciterClone->Tb = Tb;
    exciterClone->Kc = Kc;
    exciterClone->Kf = Kf;
    exciterClone->Tf = Tf;
    return exciterClone;
}

void ExciterEXST1::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Tr) || !std::isfinite(Vimax) || !std::isfinite(Vimin) ||
        !std::isfinite(Tc) || !std::isfinite(Tb) || !std::isfinite(Ka) || !std::isfinite(Ta) ||
        !std::isfinite(Vrmax) || !std::isfinite(Vrmin) || !std::isfinite(Kc) ||
        !std::isfinite(Kf) || !std::isfinite(Tf) || (Tr <= 0.0) || (Tb <= 0.0) || (Ta <= 0.0) ||
        (Tf <= 0.0) || (Ka <= 0.0) || (Vimax < Vimin) || (Vrmax < Vrmin)) {
        throw InvalidParameterValue("EXST1 gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 4;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = 26;
}

void ExciterEXST1::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    if (!std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterXadIfdInLocation]) ||
        (std::abs(inputs[exciterXadIfdInLocation]) > 1e20)) {
        throw InvalidParameterValue("EXST1 requires compatible synchronous-machine signals");
    }
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0]) ||
        (std::abs(desiredOutput[0]) > 1e20)) {
        throw InvalidParameterValue("EXST1 initial field voltage");
    }

    const double fieldVoltage = desiredOutput[0];
    const double initialInput = fieldVoltage / Ka;
    if ((initialInput < Vimin - limitTolerance) || (initialInput > Vimax + limitTolerance)) {
        throw InvalidParameterValue("EXST1 initial input outside limits");
    }
    const double initialUpperBound = Vrmax - Kc * inputs[exciterXadIfdInLocation];
    const double initialLowerBound = Vrmin - Kc * inputs[exciterXadIfdInLocation];
    if ((fieldVoltage < initialLowerBound - limitTolerance) ||
        (fieldVoltage > initialUpperBound + limitTolerance)) {
        throw InvalidParameterValue("EXST1 initial regulator output outside limits");
    }

    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[leadLagState] = initialInput;
    state[regulatorState] = fieldVoltage;
    state[washoutState] = fieldVoltage;

    const double setpointInput = inputs[exciterVsetInLocation] - 1.0;
    vBias = state[voltageMeasurementState] + initialInput - Vref - setpointInput -
        inputs[exciterVssInLocation];
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
}

double ExciterEXST1::referenceInput(const IOdata& inputs) const
{
    return Vref + vBias + inputs[exciterVsetInLocation] - 1.0 + inputs[exciterVssInLocation];
}

double ExciterEXST1::washoutOutput(const double state[]) const
{
    return (Kf / Tf) * (state[regulatorState] - state[washoutState]);
}

double ExciterEXST1::unlimitedInput(const IOdata& inputs, const double state[]) const
{
    return referenceInput(inputs) - state[voltageMeasurementState] - washoutOutput(state);
}

double ExciterEXST1::limitedInput(const IOdata& inputs, const double state[]) const
{
    return std::clamp(unlimitedInput(inputs, state),
                      static_cast<double>(Vimin),
                      static_cast<double>(Vimax));
}

double ExciterEXST1::leadLagOutput(const IOdata& inputs, const double state[]) const
{
    const double leadRatio = Tc / Tb;
    return state[leadLagState] + leadRatio * (limitedInput(inputs, state) - state[leadLagState]);
}

int ExciterEXST1::inputLimitStatus(const IOdata& inputs, const double state[]) const
{
    const double input = unlimitedInput(inputs, state);
    if (input >= Vimax) {
        return 1;
    }
    return (input <= Vimin) ? -1 : 0;
}

int ExciterEXST1::outputLimitStatus(const IOdata& inputs, const double state[]) const
{
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const double upperBound = Vrmax - Kc * fieldCurrent;
    const double lowerBound = Vrmin - Kc * fieldCurrent;

    // The frozen ANDES 2.0.0 model passes WF_y to HLR but applies the
    // resulting flags to LR_y. That can leave the regulator output unlimited
    // or clamp it because an unrelated washout excursion crossed a bound.
    // The PSS/E EXST1 block diagram places VRMAX/VRMIN around the regulator
    // output, so GridDyn intentionally evaluates the limiter using LR_y.
    const double regulatorOutput = state[regulatorState];
    if (regulatorOutput >= upperBound) {
        return 1;
    }
    return (regulatorOutput <= lowerBound) ? -1 : 0;
}

double ExciterEXST1::fieldVoltageTarget(const IOdata& inputs, const double state[]) const
{
    const int limitStatus = outputLimitStatus(inputs, state);
    if (limitStatus > 0) {
        return Vrmax - Kc * inputs[exciterXadIfdInLocation];
    }
    if (limitStatus < 0) {
        return Vrmin - Kc * inputs[exciterXadIfdInLocation];
    }
    return state[regulatorState];
}

void ExciterEXST1::residual(const IOdata& inputs,
                            const StateData& stateData,
                            double resid[],
                            const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] =
            fieldVoltageTarget(inputs, locations.diffStateLoc) - locations.algStateLoc[0];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t index = 0; index < locations.diffSize; ++index) {
        locations.destDiffLoc[index] -= locations.dstateLoc[index];
    }
}

void ExciterEXST1::derivative(const IOdata& inputs,
                              const StateData& stateData,
                              double deriv[],
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* stateDerivative = locations.destDiffLoc;

    stateDerivative[voltageMeasurementState] =
        (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    stateDerivative[leadLagState] = (limitedInput(inputs, state) - state[leadLagState]) / Tb;
    stateDerivative[regulatorState] =
        (Ka * leadLagOutput(inputs, state) - state[regulatorState]) / Ta;
    stateDerivative[washoutState] = (state[regulatorState] - state[washoutState]) / Tf;
}

void ExciterEXST1::jacobianElements(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t refAlg = locations.algOffset;
    const index_t refDiff = locations.diffOffset;
    const double* state = locations.diffStateLoc;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refAlg, refAlg, -1.0);
        if (outputLimitStatus(inputs, state) == 0) {
            matrixData.assign(refAlg, refDiff + regulatorState, 1.0);
        } else {
            matrixData.assignCheckCol(refAlg,
                                      inputLocs[exciterXadIfdInLocation],
                                      -static_cast<double>(Kc));
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    matrixData.assign(refDiff + voltageMeasurementState,
                      refDiff + voltageMeasurementState,
                      -1.0 / Tr - stateData.cj);
    matrixData.assignCheckCol(refDiff + voltageMeasurementState,
                              inputLocs[exciterVoltageInLocation],
                              1.0 / Tr);

    const bool inputLimited = inputLimitStatus(inputs, state) != 0;
    const double feedbackGain = Kf / Tf;
    matrixData.assign(refDiff + leadLagState, refDiff + leadLagState, -1.0 / Tb - stateData.cj);
    if (!inputLimited) {
        matrixData.assign(refDiff + leadLagState, refDiff + voltageMeasurementState, -1.0 / Tb);
        matrixData.assign(refDiff + leadLagState, refDiff + regulatorState, -feedbackGain / Tb);
        matrixData.assign(refDiff + leadLagState, refDiff + washoutState, feedbackGain / Tb);
        matrixData.assignCheckCol(refDiff + leadLagState,
                                  inputLocs[exciterVsetInLocation],
                                  1.0 / Tb);
        matrixData.assignCheckCol(refDiff + leadLagState,
                                  inputLocs[exciterVssInLocation],
                                  1.0 / Tb);
    }

    const double leadRatio = Tc / Tb;
    const double limitedInputGain = inputLimited ? 0.0 : Ka * leadRatio / Ta;
    matrixData.assign(refDiff + regulatorState,
                      refDiff + regulatorState,
                      -1.0 / Ta - limitedInputGain * feedbackGain - stateData.cj);
    matrixData.assign(refDiff + regulatorState,
                      refDiff + leadLagState,
                      Ka * (1.0 - leadRatio) / Ta);
    if (!inputLimited) {
        matrixData.assign(refDiff + regulatorState,
                          refDiff + voltageMeasurementState,
                          -limitedInputGain);
        matrixData.assign(refDiff + regulatorState,
                          refDiff + washoutState,
                          limitedInputGain * feedbackGain);
        matrixData.assignCheckCol(refDiff + regulatorState,
                                  inputLocs[exciterVsetInLocation],
                                  limitedInputGain);
        matrixData.assignCheckCol(refDiff + regulatorState,
                                  inputLocs[exciterVssInLocation],
                                  limitedInputGain);
    }

    matrixData.assign(refDiff + washoutState, refDiff + regulatorState, 1.0 / Tf);
    matrixData.assign(refDiff + washoutState, refDiff + washoutState, -1.0 / Tf - stateData.cj);
}

void ExciterEXST1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    double* state = m_state.data() + 1;
    const double* stateDerivative = m_dstate_dt.data() + 1;
    for (index_t index = 0; index < 4; ++index) {
        state[index] += timeStep * stateDerivative[index];
    }
    m_state[0] = fieldVoltageTarget(inputs, state);
    updateLimitFlags(inputs, state);
    prevTime = time;
}

bool ExciterEXST1::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const int inputStatus = inputLimitStatus(inputs, state);
    const int outputStatus = outputLimitStatus(inputs, state);
    const bool changed = (opFlags[INPUT_LIMITED] != (inputStatus != 0)) ||
        (opFlags[INPUT_LIMIT_HIGH] != (inputStatus > 0)) ||
        (opFlags[OUTPUT_LIMITED] != (outputStatus != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (outputStatus > 0));
    opFlags.set(INPUT_LIMITED, inputStatus != 0);
    opFlags.set(INPUT_LIMIT_HIGH, inputStatus > 0);
    opFlags.set(OUTPUT_LIMITED, outputStatus != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, outputStatus > 0);
    return changed;
}

void ExciterEXST1::rootTest(const IOdata& inputs,
                            const StateData& stateData,
                            double roots[],
                            const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double input = unlimitedInput(inputs, state);
    roots[rootOffset] = std::min(Vimax - input, input - Vimin);

    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const double upperBound = Vrmax - Kc * fieldCurrent;
    const double lowerBound = Vrmin - Kc * fieldCurrent;
    const double regulatorOutput = state[regulatorState];
    roots[rootOffset + 1] = std::min(upperBound - regulatorOutput, regulatorOutput - lowerBound);
}

void ExciterEXST1::rootTrigger(CoreTime /*time*/,
                               const IOdata& inputs,
                               const std::vector<int>& rootMask,
                               const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] == 0) && (rootMask[rootOffset + 1] == 0)) {
        return;
    }
    const double* state = m_state.data() + 1;
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterEXST1::rootCheck(const IOdata& inputs,
                                   const StateData& /*stateData*/,
                                   const SolverMode& /*sMode*/,
                                   CheckLevel /*level*/)
{
    const double* state = m_state.data() + 1;
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

void ExciterEXST1::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterEXST1::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tr") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXST1 TR must be positive and finite");
        }
        Tr = val;
    } else if (param == "vimax") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 VIMAX must be finite");
        }
        Vimax = val;
    } else if (param == "vimin") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 VIMIN must be finite");
        }
        Vimin = val;
    } else if (param == "tc") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 TC must be finite");
        }
        Tc = val;
    } else if (param == "tb") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXST1 TB must be positive and finite");
        }
        Tb = val;
    } else if (param == "ka") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXST1 KA must be positive and finite");
        }
        Ka = val;
    } else if (param == "ta") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXST1 TA must be positive and finite");
        }
        Ta = val;
    } else if ((param == "vrmax") || (param == "urmax")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 VRMAX must be finite");
        }
        Vrmax = val;
    } else if ((param == "vrmin") || (param == "urmin")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 VRMIN must be finite");
        }
        Vrmin = val;
    } else if (param == "kc") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 KC must be finite");
        }
        Kc = val;
    } else if (param == "kf") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("EXST1 KF must be finite");
        }
        Kf = val;
    } else if (param == "tf") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("EXST1 TF must be positive and finite");
        }
        Tf = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterEXST1::get(std::string_view param, units::unit unitType) const
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
    if (param == "vimax") {
        return Vimax;
    }
    if (param == "vimin") {
        return Vimin;
    }
    if (param == "tc") {
        return Tc;
    }
    if (param == "tb") {
        return Tb;
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
    return Exciter::get(param, unitType);
}

stringVec ExciterEXST1::localStateNames() const
{
    return {"efd", "vmeas", "ll", "vr", "wf"};
}

index_t ExciterEXST1::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const index_t diffOffset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") {
        return diffOffset + voltageMeasurementState;
    }
    if ((field == "ll") || (field == "leadlag")) {
        return diffOffset + leadLagState;
    }
    if ((field == "vr") || (field == "regulator")) {
        return diffOffset + regulatorState;
    }
    if ((field == "wf") || (field == "washout")) {
        return diffOffset + washoutState;
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
