/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerIEEEST.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::stabilizers {
namespace {
    constexpr double inputValidityLimit = 1e20;
    constexpr index_t maxDifferentialStates = 7;
}  // namespace

StabilizerIEEEST::StabilizerIEEEST(const std::string& objName): Stabilizer(objName)
{
    m_inputSize = pssInputCount;
    m_outputSize = 1;
}

CoreObject* StabilizerIEEEST::clone(CoreObject* obj) const
{
    auto* stabilizerClone = cloneBase<StabilizerIEEEST, Stabilizer>(this, obj);
    if (stabilizerClone == nullptr) {
        return obj;
    }
    stabilizerClone->mode = mode;
    stabilizerClone->remoteBus = remoteBus;
    stabilizerClone->A1 = A1;
    stabilizerClone->A2 = A2;
    stabilizerClone->A3 = A3;
    stabilizerClone->A4 = A4;
    stabilizerClone->A5 = A5;
    stabilizerClone->A6 = A6;
    stabilizerClone->T1 = T1;
    stabilizerClone->T2 = T2;
    stabilizerClone->T3 = T3;
    stabilizerClone->T4 = T4;
    stabilizerClone->T5 = T5;
    stabilizerClone->T6 = T6;
    stabilizerClone->Ks = Ks;
    stabilizerClone->Lsmax = Lsmax;
    stabilizerClone->Lsmin = Lsmin;
    stabilizerClone->Vcu = Vcu;
    stabilizerClone->Vcl = Vcl;
    return stabilizerClone;
}

bool StabilizerIEEEST::supportedMode(int modeValue)
{
    return (modeValue == 0) || (modeValue == 1) || (modeValue == 3) || (modeValue == 4) ||
        (modeValue == 5);
}

void StabilizerIEEEST::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 17> parameters{
        A1, A2, A3, A4, A5, A6, T1, T2, T3, T4, T5, T6, Ks, Lsmax, Lsmin, Vcu, Vcl};
    if (!std::all_of(parameters.begin(),
                     parameters.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !supportedMode(mode) || (remoteBus != 0) || (A1 < 0.0) || (A2 < 0.0) || (A4 < 0.0) ||
        (T1 < 0.0) || (T2 < 0.0) || (T3 < 0.0) || (T4 < 0.0) || (T5 < 0.0) || (T6 <= 0.0) ||
        (Lsmax < Lsmin) || (Vcu < Vcl)) {
        throw InvalidParameterValue("IEEEST modes, time constants, or limits");
    }

    f1xState = kNullLocation;
    f1yState = kNullLocation;
    f2xState = kNullLocation;
    f2yState = kNullLocation;
    ll1State = kNullLocation;
    ll2State = kNullLocation;
    washoutState = kNullLocation;
    index_t stateCount = 0;
    if (A2 > 0.0) {
        f1xState = stateCount++;
        f1yState = stateCount++;
    } else if (A1 > 0.0) {
        f1yState = stateCount++;
    }
    if (A4 > 0.0) {
        f2xState = stateCount++;
        f2yState = stateCount++;
    }
    if (T2 > 0.0) {
        ll1State = stateCount++;
    }
    if (T4 > 0.0) {
        ll2State = stateCount++;
    }
    washoutState = stateCount++;

    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.algRoots = 4;
    offsets.local().local.jacSize = 8 * stateCount + 8;
}

void StabilizerIEEEST::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& /*desiredOutput*/,
                                            IOdata& /*fieldSet*/)
{
    if (inputs.size() < pssInputCount) {
        throw InvalidParameterValue("IEEEST controller inputs");
    }
    const auto validInput = [&inputs](index_t index) {
        return std::isfinite(inputs[index]) && (std::abs(inputs[index]) < inputValidityLimit);
    };
    const bool signalAvailable = (mode == 0) || ((mode == 1) && validInput(pssOmegaInLocation)) ||
        ((mode == 3) && validInput(pssElectricalPowerInLocation)) ||
        ((mode == 4) && validInput(pssPmechInLocation)) ||
        ((mode == 5) && validInput(pssVoltageInLocation));
    if (!validInput(pssVoltageInLocation) || !signalAvailable) {
        throw InvalidParameterValue("IEEEST controller inputs");
    }
    initialVoltage = inputs[pssVoltageInLocation];
    initialPmech = (mode == 4) ? inputs[pssPmechInLocation] : 0.0;
    const double input = selectedInput(inputs);
    double* state = m_state.data() + 1;
    if (f1xState != kNullLocation) {
        state[f1xState] = 0.0;
    }
    if (f1yState != kNullLocation) {
        state[f1yState] = input;
    }
    const auto filterOne = f1Output(state, input);
    if (f2xState != kNullLocation) {
        state[f2xState] = 0.0;
        state[f2yState] = filterOne.value;
    }
    const auto filterTwo = f2Output(state, filterOne);
    if (ll1State != kNullLocation) {
        state[ll1State] = filterTwo.value;
    }
    const auto firstLeadLag = leadLagOutput(state, filterTwo, ll1State, T1, T2);
    if (ll2State != kNullLocation) {
        state[ll2State] = firstLeadLag.value;
    }
    const auto secondLeadLag = leadLagOutput(state, firstLeadLag, ll2State, T3, T4);
    state[washoutState] = Ks * secondLeadLag.value;
    m_state[0] = 0.0;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
}

double StabilizerIEEEST::selectedInput(const IOdata& inputs) const
{
    switch (mode) {
        case 0:
            return 0.0;
        case 1:
            return inputs[pssOmegaInLocation] - 1.0;
        case 3:
            return inputs[pssElectricalPowerInLocation];
        case 4:
            return inputs[pssPmechInLocation] - initialPmech;
        case 5:
            return inputs[pssVoltageInLocation];
        default:
            return 0.0;
    }
}

StabilizerIEEEST::LinearValue StabilizerIEEEST::f1Output(const double state[], double input) const
{
    LinearValue output;
    if (f1yState == kNullLocation) {
        output.value = input;
        output.inputGain = 1.0;
    } else {
        output.value = state[f1yState];
        output.stateGain[f1yState] = 1.0;
    }
    return output;
}

StabilizerIEEEST::LinearValue StabilizerIEEEST::f2Output(const double state[],
                                                         const LinearValue& input) const
{
    if (f2xState == kNullLocation) {
        return input;
    }
    const double scale = A6 / A4;
    LinearValue output = input;
    output.value = state[f2yState] + (A5 * state[f2xState]) +
        (scale * (input.value - state[f2yState] - (A3 * state[f2xState])));
    for (index_t index = 0; index < maxDifferentialStates; ++index) {
        output.stateGain[index] *= scale;
    }
    output.inputGain *= scale;
    output.stateGain[f2xState] += A5 - (A3 * scale);
    output.stateGain[f2yState] += 1.0 - scale;
    return output;
}

StabilizerIEEEST::LinearValue StabilizerIEEEST::leadLagOutput(const double state[],
                                                              const LinearValue& input,
                                                              index_t stateIndex,
                                                              double leadTime,
                                                              double lagTime)
{
    if (stateIndex == kNullLocation) {
        return input;
    }
    const double scale = leadTime / lagTime;
    LinearValue output = input;
    output.value = state[stateIndex] + (scale * (input.value - state[stateIndex]));
    for (index_t index = 0; index < maxDifferentialStates; ++index) {
        output.stateGain[index] *= scale;
    }
    output.inputGain *= scale;
    output.stateGain[stateIndex] += 1.0 - scale;
    return output;
}

StabilizerIEEEST::LinearValue StabilizerIEEEST::cascadeOutput(const double state[],
                                                              const IOdata& inputs) const
{
    LinearValue signal;
    signal.value = selectedInput(inputs);
    signal.inputGain = 1.0;
    const auto filterOne = f1Output(state, signal.value);
    const auto filterTwo = f2Output(state, filterOne);
    const auto firstLeadLag = leadLagOutput(state, filterTwo, ll1State, T1, T2);
    const auto secondLeadLag = leadLagOutput(state, firstLeadLag, ll2State, T3, T4);
    LinearValue output = secondLeadLag;
    const double input = Ks * secondLeadLag.value;
    if (T5 <= 0.0) {
        output.value = state[washoutState];
        output.stateGain.fill(0.0);
        output.stateGain[washoutState] = 1.0;
        output.inputGain = 0.0;
        return output;
    }
    const double scale = T5 / T6;
    output.value = scale * (input - state[washoutState]);
    for (index_t index = 0; index < maxDifferentialStates; ++index) {
        output.stateGain[index] *= Ks * scale;
    }
    output.inputGain *= Ks * scale;
    output.stateGain[washoutState] -= scale;
    return output;
}

bool StabilizerIEEEST::voltageEnabled(const IOdata& inputs) const
{
    return (inputs[pssVoltageInLocation] >= initialVoltage + Vcl) &&
        (inputs[pssVoltageInLocation] <= initialVoltage + Vcu);
}

int StabilizerIEEEST::outputLimitStatus(const double state[], const IOdata& inputs) const
{
    const double outputValue = cascadeOutput(state, inputs).value;
    if (outputValue >= Lsmax) {
        return 1;
    }
    return (outputValue <= Lsmin) ? -1 : 0;
}

double StabilizerIEEEST::output(const double state[], const IOdata& inputs) const
{
    if (!voltageEnabled(inputs)) {
        return 0.0;
    }
    return std::clamp(cascadeOutput(state, inputs).value, Lsmin, Lsmax);
}

bool StabilizerIEEEST::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const int limitStatus = outputLimitStatus(state, inputs);
    const bool gated = !voltageEnabled(inputs);
    const bool changed = (opFlags[OUTPUT_LIMITED] != (limitStatus != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (limitStatus > 0)) || (opFlags[VOLTAGE_GATED] != gated);
    opFlags.set(OUTPUT_LIMITED, limitStatus != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, limitStatus > 0);
    opFlags.set(VOLTAGE_GATED, gated);
    return changed;
}

void StabilizerIEEEST::residual(const IOdata& inputs,
                                const StateData& stateData,
                                double resid[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] = output(locations.diffStateLoc, inputs) - locations.algStateLoc[0];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t index = 0; index < locations.diffSize; ++index) {
        locations.destDiffLoc[index] -= locations.dstateLoc[index];
    }
}

void StabilizerIEEEST::derivative(const IOdata& inputs,
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
    const double input = selectedInput(inputs);
    const auto filterOne = f1Output(state, input);
    if (f1xState != kNullLocation) {
        stateDerivative[f1xState] = (input - state[f1yState] - (A1 * state[f1xState])) / A2;
        stateDerivative[f1yState] = state[f1xState];
    } else if (f1yState != kNullLocation) {
        stateDerivative[f1yState] = (input - state[f1yState]) / A1;
    }
    const auto filterTwo = f2Output(state, filterOne);
    if (f2xState != kNullLocation) {
        stateDerivative[f2xState] =
            (filterOne.value - state[f2yState] - (A3 * state[f2xState])) / A4;
        stateDerivative[f2yState] = state[f2xState];
    }
    const auto firstLeadLag = leadLagOutput(state, filterTwo, ll1State, T1, T2);
    if (ll1State != kNullLocation) {
        stateDerivative[ll1State] = (filterTwo.value - state[ll1State]) / T2;
    }
    const auto secondLeadLag = leadLagOutput(state, firstLeadLag, ll2State, T3, T4);
    if (ll2State != kNullLocation) {
        stateDerivative[ll2State] = (firstLeadLag.value - state[ll2State]) / T4;
    }
    stateDerivative[washoutState] = ((Ks * secondLeadLag.value) - state[washoutState]) / T6;
}

void StabilizerIEEEST::jacobianElements(const IOdata& inputs,
                                        const StateData& stateData,
                                        MatrixData<double>& matrixData,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t refAlg = locations.algOffset;
    const index_t refDiff = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const auto addInput = [&matrixData, &inputLocs, this](index_t row, double value) {
        if (mode == 1) {
            matrixData.assignCheckCol(row, inputLocs[pssOmegaInLocation], value);
        } else if (mode == 3) {
            matrixData.assignCheckCol(row, inputLocs[pssElectricalPowerInLocation], value);
        } else if (mode == 4) {
            matrixData.assignCheckCol(row, inputLocs[pssPmechInLocation], value);
        } else if (mode == 5) {
            matrixData.assignCheckCol(row, inputLocs[pssVoltageInLocation], value);
        }
    };
    const auto addLinear =
        [&matrixData, &addInput, refDiff](index_t row, const LinearValue& value, double scale) {
            for (index_t index = 0; index < maxDifferentialStates; ++index) {
                if (value.stateGain[index] != 0.0) {
                    matrixData.assign(row, refDiff + index, scale * value.stateGain[index]);
                }
            }
            if (value.inputGain != 0.0) {
                addInput(row, scale * value.inputGain);
            }
        };

    const auto outputValue = cascadeOutput(state, inputs);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(refAlg, refAlg, -1.0);
        if (voltageEnabled(inputs) && (outputLimitStatus(state, inputs) == 0)) {
            addLinear(refAlg, outputValue, 1.0);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    LinearValue signal;
    signal.value = selectedInput(inputs);
    signal.inputGain = 1.0;
    const auto filterOne = f1Output(state, signal.value);
    if (f1xState != kNullLocation) {
        addLinear(refDiff + f1xState, signal, 1.0 / A2);
        matrixData.assign(refDiff + f1xState, refDiff + f1xState, ((-A1 / A2) - stateData.cj));
        matrixData.assign(refDiff + f1xState, refDiff + f1yState, -1.0 / A2);
        matrixData.assign(refDiff + f1yState, refDiff + f1xState, 1.0);
        matrixData.assign(refDiff + f1yState, refDiff + f1yState, -stateData.cj);
    } else if (f1yState != kNullLocation) {
        addLinear(refDiff + f1yState, signal, 1.0 / A1);
        matrixData.assign(refDiff + f1yState, refDiff + f1yState, ((-1.0 / A1) - stateData.cj));
    }

    const auto filterTwo = f2Output(state, filterOne);
    if (f2xState != kNullLocation) {
        addLinear(refDiff + f2xState, filterOne, 1.0 / A4);
        matrixData.assign(refDiff + f2xState, refDiff + f2xState, ((-A3 / A4) - stateData.cj));
        matrixData.assign(refDiff + f2xState, refDiff + f2yState, -1.0 / A4);
        matrixData.assign(refDiff + f2yState, refDiff + f2xState, 1.0);
        matrixData.assign(refDiff + f2yState, refDiff + f2yState, -stateData.cj);
    }
    const auto firstLeadLag = leadLagOutput(state, filterTwo, ll1State, T1, T2);
    if (ll1State != kNullLocation) {
        addLinear(refDiff + ll1State, filterTwo, 1.0 / T2);
        matrixData.assign(refDiff + ll1State, refDiff + ll1State, ((-1.0 / T2) - stateData.cj));
    }
    const auto secondLeadLag = leadLagOutput(state, firstLeadLag, ll2State, T3, T4);
    if (ll2State != kNullLocation) {
        addLinear(refDiff + ll2State, firstLeadLag, 1.0 / T4);
        matrixData.assign(refDiff + ll2State, refDiff + ll2State, ((-1.0 / T4) - stateData.cj));
    }
    addLinear(refDiff + washoutState, secondLeadLag, Ks / T6);
    matrixData.assign(refDiff + washoutState, refDiff + washoutState, ((-1.0 / T6) - stateData.cj));
}

void StabilizerIEEEST::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t index = 0; index < offsets.local().local.diffSize; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    m_state[0] = output(m_state.data() + 1, inputs);
    updateLimitFlags(inputs, m_state.data() + 1);
    prevTime = time;
}

void StabilizerIEEEST::rootTest(const IOdata& inputs,
                                const StateData& stateData,
                                double roots[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double unlimited = cascadeOutput(locations.diffStateLoc, inputs).value;
    roots[rootOffset] = Lsmax - unlimited;
    roots[rootOffset + 1] = unlimited - Lsmin;
    roots[rootOffset + 2] = inputs[pssVoltageInLocation] - (initialVoltage + Vcl);
    roots[rootOffset + 3] = (initialVoltage + Vcu) - inputs[pssVoltageInLocation];
}

void StabilizerIEEEST::rootTrigger(CoreTime /*time*/,
                                   const IOdata& inputs,
                                   const std::vector<int>& rootMask,
                                   const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] == 0) && (rootMask[rootOffset + 1] == 0) &&
        (rootMask[rootOffset + 2] == 0) && (rootMask[rootOffset + 3] == 0)) {
        return;
    }
    if (updateLimitFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode StabilizerIEEEST::rootCheck(const IOdata& inputs,
                                       const StateData& /*stateData*/,
                                       const SolverMode& /*sMode*/,
                                       CheckLevel /*level*/)
{
    if (updateLimitFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec StabilizerIEEEST::localStateNames() const
{
    stringVec names{"vs"};
    if (f1xState != kNullLocation) {
        names.emplace_back("f1x");
    }
    if (f1yState != kNullLocation) {
        names.emplace_back("f1y");
    }
    if (f2xState != kNullLocation) {
        names.emplace_back("f2x");
        names.emplace_back("f2y");
    }
    if (ll1State != kNullLocation) {
        names.emplace_back("ll1");
    }
    if (ll2State != kNullLocation) {
        names.emplace_back("ll2");
    }
    names.emplace_back("wo");
    return names;
}

index_t StabilizerIEEEST::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "vss") || (field == "vs")) {
        return getOutputLoc(sMode, 0);
    }
    return kInvalidLocation;
}

void StabilizerIEEEST::set(std::string_view param, std::string_view val)
{
    Stabilizer::set(param, val);
}

void StabilizerIEEEST::set(std::string_view param, double val, units::unit unitType)
{
    const auto finite = [val](const char* parameterName) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue(std::string("IEEEST ") + parameterName + " must be finite");
        }
    };
    if (param == "mode") {
        if (!std::isfinite(val) || (std::floor(val) != val) ||
            !supportedMode(static_cast<int>(val))) {
            throw InvalidParameterValue("IEEEST MODE is unsupported");
        }
        mode = static_cast<int>(val);
    } else if (param == "busr") {
        if (!std::isfinite(val) || (std::floor(val) != val) || (val != 0.0)) {
            throw InvalidParameterValue("IEEEST remote BUSR is unsupported");
        }
        remoteBus = 0;
    } else if (param == "a1") {
        finite("A1");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST A1 must be nonnegative");
        }
        A1 = val;
    } else if (param == "a2") {
        finite("A2");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST A2 must be nonnegative");
        }
        A2 = val;
    } else if (param == "a3") {
        finite("A3");
        A3 = val;
    } else if (param == "a4") {
        finite("A4");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST A4 must be nonnegative");
        }
        A4 = val;
    } else if (param == "a5") {
        finite("A5");
        A5 = val;
    } else if (param == "a6") {
        finite("A6");
        A6 = val;
    } else if (param == "t1") {
        finite("T1");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST T1 must be nonnegative");
        }
        T1 = val;
    } else if (param == "t2") {
        finite("T2");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST T2 must be nonnegative");
        }
        T2 = val;
    } else if (param == "t3") {
        finite("T3");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST T3 must be nonnegative");
        }
        T3 = val;
    } else if (param == "t4") {
        finite("T4");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST T4 must be nonnegative");
        }
        T4 = val;
    } else if (param == "t5") {
        finite("T5");
        if (val < 0.0) {
            throw InvalidParameterValue("IEEEST T5 must be nonnegative");
        }
        T5 = val;
    } else if (param == "t6") {
        finite("T6");
        if (val <= 0.0) {
            throw InvalidParameterValue("IEEEST T6 must be positive");
        }
        T6 = val;
    } else if (param == "ks") {
        finite("KS");
        Ks = val;
    } else if ((param == "lsmax") || (param == "vmax")) {
        finite("LSMAX");
        if (val < Lsmin) {
            throw InvalidParameterValue("IEEEST LSMAX must not be less than LSMIN");
        }
        Lsmax = val;
    } else if ((param == "lsmin") || (param == "vmin")) {
        finite("LSMIN");
        if (val > Lsmax) {
            throw InvalidParameterValue("IEEEST LSMIN must not exceed LSMAX");
        }
        Lsmin = val;
    } else if (param == "vcu") {
        finite("VCU");
        const double mappedValue = (val == 0.0) ? 999.0 : val;
        if (mappedValue < Vcl) {
            throw InvalidParameterValue("IEEEST VCU must not be less than VCL");
        }
        Vcu = mappedValue;
    } else if (param == "vcl") {
        finite("VCL");
        const double mappedValue = (val == 0.0) ? -999.0 : val;
        if (mappedValue > Vcu) {
            throw InvalidParameterValue("IEEEST VCL must not exceed VCU");
        }
        Vcl = mappedValue;
    } else {
        Stabilizer::set(param, val, unitType);
    }
}

double StabilizerIEEEST::get(std::string_view param, units::unit unitType) const
{
    if (param == "mode") {
        return mode;
    }
    if (param == "busr") {
        return remoteBus;
    }
    if (param == "a1") {
        return A1;
    }
    if (param == "a2") {
        return A2;
    }
    if (param == "a3") {
        return A3;
    }
    if (param == "a4") {
        return A4;
    }
    if (param == "a5") {
        return A5;
    }
    if (param == "a6") {
        return A6;
    }
    if (param == "t1") {
        return T1;
    }
    if (param == "t2") {
        return T2;
    }
    if (param == "t3") {
        return T3;
    }
    if (param == "t4") {
        return T4;
    }
    if (param == "t5") {
        return T5;
    }
    if (param == "t6") {
        return T6;
    }
    if (param == "ks") {
        return Ks;
    }
    if ((param == "lsmax") || (param == "vmax")) {
        return Lsmax;
    }
    if ((param == "lsmin") || (param == "vmin")) {
        return Lsmin;
    }
    if (param == "vcu") {
        return Vcu;
    }
    if (param == "vcl") {
        return Vcl;
    }
    return Stabilizer::get(param, unitType);
}
}  // namespace griddyn::stabilizers
