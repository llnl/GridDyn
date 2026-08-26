/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerST2CUT.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::stabilizers {
namespace {
    constexpr index_t transducer1State = 0;
    constexpr index_t transducer2State = 1;
    constexpr index_t washoutState = 2;
    constexpr index_t leadLag1State = 3;
    constexpr index_t leadLag2State = 4;
    constexpr index_t leadLag3State = 5;
}  // namespace

StabilizerST2CUT::StabilizerST2CUT(const std::string& objName): Stabilizer(objName)
{
    m_inputSize = pssInputCount;
    m_outputSize = 1;
}

CoreObject* StabilizerST2CUT::clone(CoreObject* obj) const
{
    auto* stabilizerClone = cloneBase<StabilizerST2CUT, Stabilizer>(this, obj);
    if (stabilizerClone == nullptr) {
        return obj;
    }
    stabilizerClone->mode1 = mode1;
    stabilizerClone->remoteBus1 = remoteBus1;
    stabilizerClone->mode2 = mode2;
    stabilizerClone->remoteBus2 = remoteBus2;
    stabilizerClone->K1 = K1;
    stabilizerClone->K2 = K2;
    stabilizerClone->T1 = T1;
    stabilizerClone->T2 = T2;
    stabilizerClone->T3 = T3;
    stabilizerClone->T4 = T4;
    stabilizerClone->T5 = T5;
    stabilizerClone->T6 = T6;
    stabilizerClone->T7 = T7;
    stabilizerClone->T8 = T8;
    stabilizerClone->T9 = T9;
    stabilizerClone->T10 = T10;
    stabilizerClone->Lsmax = Lsmax;
    stabilizerClone->Lsmin = Lsmin;
    stabilizerClone->Vcu = Vcu;
    stabilizerClone->Vcl = Vcl;
    return stabilizerClone;
}

bool StabilizerST2CUT::supportedMode(int mode)
{
    return (mode == 0) || (mode == 1) || (mode == 3) || (mode == 4) || (mode == 5);
}

void StabilizerST2CUT::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 16> parameters{
        K1, K2, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, Lsmax, Lsmin, Vcu, Vcl};
    if (!std::all_of(parameters.begin(),
                     parameters.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !supportedMode(mode1) || !supportedMode(mode2) || (remoteBus1 != 0) || (remoteBus2 != 0) ||
        (T1 <= 0.0) || (T2 <= 0.0) || (T3 < 0.0) || (T4 <= 0.0) || (T6 <= 0.0) || (T8 <= 0.0) ||
        (T5 < 0.0) || (T7 < 0.0) || (T9 < 0.0) || (T10 <= 0.0) || (Lsmax < Lsmin) || (Vcu < Vcl)) {
        throw InvalidParameterValue("ST2CUT modes, time constants, limits, or remote-bus inputs");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 6;
    offsets.local().local.algRoots = 4;
    offsets.local().local.jacSize = 28;
}

void StabilizerST2CUT::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& /*desiredOutput*/,
                                            IOdata& /*fieldSet*/)
{
    const auto validInput = [&inputs](index_t index) {
        return std::isfinite(inputs[index]) && (std::abs(inputs[index]) < 1e20);
    };
    const auto selectedInputAvailable = [&validInput](int mode) {
        switch (mode) {
            case 0:
                return true;
            case 1:
                return validInput(pssOmegaInLocation);
            case 3:
                return validInput(pssElectricalPowerInLocation);
            case 4:
                return validInput(pssPmechInLocation);
            case 5:
                return validInput(pssVoltageInLocation);
            default:
                return false;
        }
    };
    if ((inputs.size() < pssInputCount) || !validInput(pssVoltageInLocation) ||
        !selectedInputAvailable(mode1) || !selectedInputAvailable(mode2)) {
        throw InvalidParameterValue("ST2CUT controller inputs");
    }
    initialVoltage = inputs[pssVoltageInLocation];
    initialPmech = ((mode1 == 4) || (mode2 == 4)) ? inputs[pssPmechInLocation] : 0.0;
    const double input1 = selectedInput(inputs, mode1);
    const double input2 = selectedInput(inputs, mode2);
    m_state[0] = 0.0;
    double* state = m_state.data() + 1;
    state[transducer1State] = K1 * input1;
    state[transducer2State] = K2 * input2;
    const double summedInput = state[transducer1State] + state[transducer2State];
    state[washoutState] = summedInput;
    state[leadLag1State] = 0.0;
    state[leadLag2State] = 0.0;
    state[leadLag3State] = 0.0;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateFlags(inputs, state);
}

double StabilizerST2CUT::selectedInput(const IOdata& inputs, int mode) const
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

double StabilizerST2CUT::washoutOutput(const double state[]) const
{
    const double summedInput = state[transducer1State] + state[transducer2State];
    if (T3 <= 0.0) {
        return state[washoutState];
    }
    return (T3 / T4) * (summedInput - state[washoutState]);
}

double StabilizerST2CUT::leadLagOutput(double input, double state, double leadTime, double lagTime)
{
    return state + ((leadTime / lagTime) * (input - state));
}

double StabilizerST2CUT::unlimitedOutput(const double state[]) const
{
    const double output1 = leadLagOutput(washoutOutput(state), state[leadLag1State], T5, T6);
    const double output2 = leadLagOutput(output1, state[leadLag2State], T7, T8);
    return leadLagOutput(output2, state[leadLag3State], T9, T10);
}

bool StabilizerST2CUT::voltageEnabled(const IOdata& inputs) const
{
    return (inputs[pssVoltageInLocation] >= initialVoltage + Vcl) &&
        (inputs[pssVoltageInLocation] <= initialVoltage + Vcu);
}

int StabilizerST2CUT::outputLimitStatus(const double state[]) const
{
    const double outputValue = unlimitedOutput(state);
    if (outputValue >= Lsmax) {
        return 1;
    }
    return (outputValue <= Lsmin) ? -1 : 0;
}

double StabilizerST2CUT::output(const IOdata& inputs, const double state[]) const
{
    if (!voltageEnabled(inputs)) {
        return 0.0;
    }
    return std::clamp(unlimitedOutput(state), Lsmin, Lsmax);
}

bool StabilizerST2CUT::updateFlags(const IOdata& inputs, const double state[])
{
    const int outputStatus = outputLimitStatus(state);
    const bool gated = !voltageEnabled(inputs);
    const bool changed = (opFlags[OUTPUT_LIMITED] != (outputStatus != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (outputStatus > 0)) || (opFlags[VOLTAGE_GATED] != gated);
    opFlags.set(OUTPUT_LIMITED, outputStatus != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, outputStatus > 0);
    opFlags.set(VOLTAGE_GATED, gated);
    return changed;
}

void StabilizerST2CUT::residual(const IOdata& inputs,
                                const StateData& stateData,
                                double resid[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] = output(inputs, locations.diffStateLoc) - locations.algStateLoc[0];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t index = 0; index < locations.diffSize; ++index) {
        locations.destDiffLoc[index] -= locations.dstateLoc[index];
    }
}

void StabilizerST2CUT::derivative(const IOdata& inputs,
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
    const double input1 = selectedInput(inputs, mode1);
    const double input2 = selectedInput(inputs, mode2);
    const double summedInput = state[transducer1State] + state[transducer2State];
    const double washout = washoutOutput(state);
    const double output1 = leadLagOutput(washout, state[leadLag1State], T5, T6);
    const double output2 = leadLagOutput(output1, state[leadLag2State], T7, T8);

    stateDerivative[transducer1State] = (K1 * input1 - state[transducer1State]) / T1;
    stateDerivative[transducer2State] = (K2 * input2 - state[transducer2State]) / T2;
    stateDerivative[washoutState] = (summedInput - state[washoutState]) / T4;
    stateDerivative[leadLag1State] = (washout - state[leadLag1State]) / T6;
    stateDerivative[leadLag2State] = (output1 - state[leadLag2State]) / T8;
    stateDerivative[leadLag3State] = (output2 - state[leadLag3State]) / T10;
}

void StabilizerST2CUT::jacobianElements(const IOdata& inputs,
                                        const StateData& stateData,
                                        MatrixData<double>& matrixData,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t refAlg = locations.algOffset;
    const index_t refDiff = locations.diffOffset;
    const double* state = locations.diffStateLoc;

    const double washoutScale = (T3 <= 0.0) ? 0.0 : T3 / T4;
    const double output1Gain = T5 / T6;
    const double output2Gain = T7 / T8;
    const double output3Gain = T9 / T10;
    const double ll1StateGain = 1.0 - output1Gain;
    const double ll2StateGain = 1.0 - output2Gain;
    const double ll3StateGain = 1.0 - output3Gain;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refAlg, refAlg, -1.0);
        if (voltageEnabled(inputs) && (outputLimitStatus(state) == 0)) {
            matrixData.assign(refAlg, refDiff + leadLag3State, ll3StateGain);
            matrixData.assign(refAlg, refDiff + leadLag2State, output3Gain * ll2StateGain);
            matrixData.assign(refAlg,
                              refDiff + leadLag1State,
                              output3Gain * output2Gain * ll1StateGain);
            if (T3 <= 0.0) {
                matrixData.assign(refAlg,
                                  refDiff + washoutState,
                                  output3Gain * output2Gain * output1Gain);
            } else {
                const double washoutGain = output3Gain * output2Gain * output1Gain * washoutScale;
                matrixData.assign(refAlg, refDiff + transducer1State, washoutGain);
                matrixData.assign(refAlg, refDiff + transducer2State, washoutGain);
                matrixData.assign(refAlg, refDiff + washoutState, -washoutGain);
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    const auto addSelectedInput = [&matrixData, &inputLocs](index_t row, int mode, double value) {
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

    matrixData.assign(refDiff + transducer1State,
                      refDiff + transducer1State,
                      ((-1.0 / T1) - stateData.cj));
    addSelectedInput(refDiff + transducer1State, mode1, K1 / T1);
    matrixData.assign(refDiff + transducer2State,
                      refDiff + transducer2State,
                      ((-1.0 / T2) - stateData.cj));
    addSelectedInput(refDiff + transducer2State, mode2, K2 / T2);
    matrixData.assign(refDiff + washoutState, refDiff + transducer1State, 1.0 / T4);
    matrixData.assign(refDiff + washoutState, refDiff + transducer2State, 1.0 / T4);
    matrixData.assign(refDiff + washoutState, refDiff + washoutState, ((-1.0 / T4) - stateData.cj));

    matrixData.assign(refDiff + leadLag1State,
                      refDiff + leadLag1State,
                      ((-1.0 / T6) - stateData.cj));
    if (T3 <= 0.0) {
        matrixData.assign(refDiff + leadLag1State, refDiff + washoutState, 1.0 / T6);
    } else {
        const double gain = washoutScale / T6;
        matrixData.assign(refDiff + leadLag1State, refDiff + transducer1State, gain);
        matrixData.assign(refDiff + leadLag1State, refDiff + transducer2State, gain);
        matrixData.assign(refDiff + leadLag1State, refDiff + washoutState, -gain);
    }

    matrixData.assign(refDiff + leadLag2State,
                      refDiff + leadLag2State,
                      ((-1.0 / T8) - stateData.cj));
    matrixData.assign(refDiff + leadLag2State, refDiff + leadLag1State, ll1StateGain / T8);
    if (T3 <= 0.0) {
        matrixData.assign(refDiff + leadLag2State, refDiff + washoutState, output1Gain / T8);
    } else {
        const double gain = output1Gain * washoutScale / T8;
        matrixData.assign(refDiff + leadLag2State, refDiff + transducer1State, gain);
        matrixData.assign(refDiff + leadLag2State, refDiff + transducer2State, gain);
        matrixData.assign(refDiff + leadLag2State, refDiff + washoutState, -gain);
    }

    matrixData.assign(refDiff + leadLag3State,
                      refDiff + leadLag3State,
                      ((-1.0 / T10) - stateData.cj));
    matrixData.assign(refDiff + leadLag3State, refDiff + leadLag2State, ll2StateGain / T10);
    matrixData.assign(refDiff + leadLag3State,
                      refDiff + leadLag1State,
                      output2Gain * ll1StateGain / T10);
    if (T3 <= 0.0) {
        matrixData.assign(refDiff + leadLag3State,
                          refDiff + washoutState,
                          output2Gain * output1Gain / T10);
    } else {
        const double gain = output2Gain * output1Gain * washoutScale / T10;
        matrixData.assign(refDiff + leadLag3State, refDiff + transducer1State, gain);
        matrixData.assign(refDiff + leadLag3State, refDiff + transducer2State, gain);
        matrixData.assign(refDiff + leadLag3State, refDiff + washoutState, -gain);
    }
}

void StabilizerST2CUT::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    double* state = m_state.data() + 1;
    const double* stateDerivative = m_dstate_dt.data() + 1;
    for (index_t index = 0; index < 6; ++index) {
        state[index] += timeStep * stateDerivative[index];
    }
    m_state[0] = output(inputs, state);
    updateFlags(inputs, state);
    prevTime = time;
}

void StabilizerST2CUT::rootTest(const IOdata& inputs,
                                const StateData& stateData,
                                double roots[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double unlimited = unlimitedOutput(locations.diffStateLoc);
    roots[rootOffset] = Lsmax - unlimited;
    roots[rootOffset + 1] = unlimited - Lsmin;
    roots[rootOffset + 2] = inputs[pssVoltageInLocation] - (initialVoltage + Vcl);
    roots[rootOffset + 3] = (initialVoltage + Vcu) - inputs[pssVoltageInLocation];
}

void StabilizerST2CUT::rootTrigger(CoreTime /*time*/,
                                   const IOdata& inputs,
                                   const std::vector<int>& rootMask,
                                   const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] == 0) && (rootMask[rootOffset + 1] == 0) &&
        (rootMask[rootOffset + 2] == 0) && (rootMask[rootOffset + 3] == 0)) {
        return;
    }
    if (updateFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode StabilizerST2CUT::rootCheck(const IOdata& inputs,
                                       const StateData& /*stateData*/,
                                       const SolverMode& /*sMode*/,
                                       CheckLevel /*level*/)
{
    if (updateFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec StabilizerST2CUT::localStateNames() const
{
    return {"vs", "l1", "l2", "wo", "ll1", "ll2", "ll3"};
}

index_t StabilizerST2CUT::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "vss") || (field == "vs")) {
        return getOutputLoc(sMode, 0);
    }
    return kInvalidLocation;
}

void StabilizerST2CUT::set(std::string_view param, std::string_view val)
{
    Stabilizer::set(param, val);
}

void StabilizerST2CUT::set(std::string_view param, double val, units::unit unitType)
{
    const auto setFinite = [val](const char* parameterName) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue(std::string("ST2CUT ") + parameterName + " must be finite");
        }
    };
    if ((param == "mode") || (param == "mode1")) {
        if (!std::isfinite(val) || (std::floor(val) != val) ||
            !supportedMode(static_cast<int>(val))) {
            throw InvalidParameterValue("ST2CUT MODE is unsupported");
        }
        mode1 = static_cast<int>(val);
    } else if ((param == "mode2") || (param == "mode_2")) {
        if (!std::isfinite(val) || (std::floor(val) != val) ||
            !supportedMode(static_cast<int>(val))) {
            throw InvalidParameterValue("ST2CUT MODE2 is unsupported");
        }
        mode2 = static_cast<int>(val);
    } else if ((param == "busr") || (param == "busr1")) {
        if (!std::isfinite(val) || (std::floor(val) != val) || (val != 0.0)) {
            throw InvalidParameterValue("ST2CUT remote BUSR is unsupported");
        }
        remoteBus1 = 0;
    } else if (param == "busr2") {
        if (!std::isfinite(val) || (std::floor(val) != val) || (val != 0.0)) {
            throw InvalidParameterValue("ST2CUT remote BUSR2 is unsupported");
        }
        remoteBus2 = 0;
    } else if (param == "k1") {
        setFinite("K1");
        K1 = val;
    } else if (param == "k2") {
        setFinite("K2");
        K2 = val;
    } else if (param == "t1") {
        setFinite("T1");
        if (val <= 0.0) {
            throw InvalidParameterValue("ST2CUT T1 must be positive");
        }
        T1 = val;
    } else if (param == "t2") {
        setFinite("T2");
        if (val <= 0.0) {
            throw InvalidParameterValue("ST2CUT T2 must be positive");
        }
        T2 = val;
    } else if (param == "t3") {
        setFinite("T3");
        if (val < 0.0) {
            throw InvalidParameterValue("ST2CUT T3 must be nonnegative");
        }
        T3 = val;
    } else if (param == "t4") {
        setFinite("T4");
        if (val <= 0.0) {
            throw InvalidParameterValue("ST2CUT T4 must be positive");
        }
        T4 = val;
    } else if (param == "t5") {
        setFinite("T5");
        if (val < 0.0) {
            throw InvalidParameterValue("ST2CUT T5 must be nonnegative");
        }
        T5 = val;
    } else if (param == "t6") {
        setFinite("T6");
        if (val <= 0.0) {
            throw InvalidParameterValue("ST2CUT T6 must be positive");
        }
        T6 = val;
    } else if (param == "t7") {
        setFinite("T7");
        if (val < 0.0) {
            throw InvalidParameterValue("ST2CUT T7 must be nonnegative");
        }
        T7 = val;
    } else if (param == "t8") {
        setFinite("T8");
        if (val <= 0.0) {
            throw InvalidParameterValue("ST2CUT T8 must be positive");
        }
        T8 = val;
    } else if (param == "t9") {
        setFinite("T9");
        if (val < 0.0) {
            throw InvalidParameterValue("ST2CUT T9 must be nonnegative");
        }
        T9 = val;
    } else if (param == "t10") {
        setFinite("T10");
        if (val <= 0.0) {
            throw InvalidParameterValue("ST2CUT T10 must be positive");
        }
        T10 = val;
    } else if ((param == "lsmax") || (param == "vmax")) {
        setFinite("LSMAX");
        if (val < Lsmin) {
            throw InvalidParameterValue("ST2CUT LSMAX must not be less than LSMIN");
        }
        Lsmax = val;
    } else if ((param == "lsmin") || (param == "vmin")) {
        setFinite("LSMIN");
        if (val > Lsmax) {
            throw InvalidParameterValue("ST2CUT LSMIN must not exceed LSMAX");
        }
        Lsmin = val;
    } else if (param == "vcu") {
        setFinite("VCU");
        const double mappedValue = (val == 0.0) ? 999.0 : val;
        if (mappedValue < Vcl) {
            throw InvalidParameterValue("ST2CUT VCU must not be less than VCL");
        }
        Vcu = mappedValue;
    } else if (param == "vcl") {
        setFinite("VCL");
        const double mappedValue = (val == 0.0) ? -999.0 : val;
        if (mappedValue > Vcu) {
            throw InvalidParameterValue("ST2CUT VCL must not exceed VCU");
        }
        Vcl = mappedValue;
    } else {
        Stabilizer::set(param, val, unitType);
    }
}

double StabilizerST2CUT::get(std::string_view param, units::unit unitType) const
{
    if ((param == "mode") || (param == "mode1")) {
        return mode1;
    }
    if (param == "mode2") {
        return mode2;
    }
    if ((param == "busr") || (param == "busr1")) {
        return remoteBus1;
    }
    if (param == "busr2") {
        return remoteBus2;
    }
    if (param == "k1") {
        return K1;
    }
    if (param == "k2") {
        return K2;
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
    if (param == "t7") {
        return T7;
    }
    if (param == "t8") {
        return T8;
    }
    if (param == "t9") {
        return T9;
    }
    if (param == "t10") {
        return T10;
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
