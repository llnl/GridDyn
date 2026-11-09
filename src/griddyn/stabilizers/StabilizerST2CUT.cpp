/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerST2CUT.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::stabilizers {
namespace {
    // Keep output-limit roots clear under 10x-relative-tolerance input noise,
    // including the cascade gain from frequency to stabilizer output.
    constexpr double limitRootTolerance = 1e-4;
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

const ModelDescription& StabilizerST2CUT::getModelDescription() const
{
    static const auto description = ModelDescription::define<StabilizerST2CUT>(
        "StabilizerST2CUT",
        &Stabilizer::getModelDescription(),
        {parameter({"mode", "mode1"},
                   &StabilizerST2CUT::mode1,
                   units::one,
                   "Input signal selector",
                   supportedMode),
         parameter({"mode2", "mode_2"},
                   &StabilizerST2CUT::mode2,
                   units::one,
                   "Input signal selector",
                   supportedMode),
         parameter({"busr", "busr1"},
                   &StabilizerST2CUT::remoteBus1,
                   units::one,
                   "Remote bus selector",
                   [](int value) { return value == 0; }),
         parameter({"busr2"},
                   &StabilizerST2CUT::remoteBus2,
                   units::one,
                   "Remote bus selector",
                   [](int value) { return value == 0; }),
         parameter({"k1"}, &StabilizerST2CUT::K1, units::one, "K1 gain"),
         parameter({"k2"}, &StabilizerST2CUT::K2, units::one, "K2 gain"),
         parameter({"t1"},
                   &StabilizerST2CUT::T1,
                   units::second,
                   "T1 time constant",
                   parameterValidators::positive),
         parameter({"t2"},
                   &StabilizerST2CUT::T2,
                   units::second,
                   "T2 time constant",
                   parameterValidators::positive),
         parameter({"t3"},
                   &StabilizerST2CUT::T3,
                   units::second,
                   "T3 time constant",
                   parameterValidators::nonnegative),
         parameter({"t4"},
                   &StabilizerST2CUT::T4,
                   units::second,
                   "T4 time constant",
                   parameterValidators::positive),
         parameter({"t5"},
                   &StabilizerST2CUT::T5,
                   units::second,
                   "T5 time constant",
                   parameterValidators::nonnegative),
         parameter({"t6"},
                   &StabilizerST2CUT::T6,
                   units::second,
                   "T6 time constant",
                   parameterValidators::positive),
         parameter({"t7"},
                   &StabilizerST2CUT::T7,
                   units::second,
                   "T7 time constant",
                   parameterValidators::nonnegative),
         parameter({"t8"},
                   &StabilizerST2CUT::T8,
                   units::second,
                   "T8 time constant",
                   parameterValidators::positive),
         parameter({"t9"},
                   &StabilizerST2CUT::T9,
                   units::second,
                   "T9 time constant",
                   parameterValidators::nonnegative),
         parameter({"t10"},
                   &StabilizerST2CUT::T10,
                   units::second,
                   "T10 time constant",
                   parameterValidators::positive),
         parameter({"lsmax", "vmax"}, &StabilizerST2CUT::Lsmax, units::one, "Lsmax signal limit"),
         parameter({"lsmin", "vmin"}, &StabilizerST2CUT::Lsmin, units::one, "Lsmin signal limit"),
         parameter({"vcu"}, &StabilizerST2CUT::Vcu, units::one, "Vcu gain"),
         parameter({"vcl"}, &StabilizerST2CUT::Vcl, units::one, "Vcl gain"),
         localState("initialVoltage", &StabilizerST2CUT::initialVoltage, "Initialized reference"),
         localState("initialPmech", &StabilizerST2CUT::initialPmech, "Initialized reference"),
         localState(
             "output_limited", &StabilizerST2CUT::opFlags, OUTPUT_LIMITED, "Output limiter active"),
         localState("output_limit_high",
                    &StabilizerST2CUT::opFlags,
                    OUTPUT_LIMIT_HIGH,
                    "Upper output limit active"),
         localState(
             "voltage_gated", &StabilizerST2CUT::opFlags, VOLTAGE_GATED, "Voltage gating history"),
         state({"vs", "vss"}, units::one, 0, "Stabilizer solver variable vs"),
         state({"l1"}, units::one, 1, "Stabilizer solver variable l1"),
         state({"l2"}, units::one, 2, "Stabilizer solver variable l2"),
         state({"wo"}, units::one, 3, "Stabilizer solver variable wo"),
         state({"ll1"}, units::one, 4, "Stabilizer solver variable ll1"),
         state({"ll2"}, units::one, 5, "Stabilizer solver variable ll2"),
         state({"ll3"}, units::one, 6, "Stabilizer solver variable ll3")});
    return description;
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
    updateLimitFlags(inputs, state);
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

bool StabilizerST2CUT::updateLimitFlags(const IOdata& inputs, const double state[])
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

    stateDerivative[transducer1State] = ((K1 * input1) - state[transducer1State]) / T1;
    stateDerivative[transducer2State] = ((K2 * input2) - state[transducer2State]) / T2;
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
        if (isAlgebraicOnly(sMode)) {
            return;
        }
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
    updateLimitFlags(inputs, state);
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
    roots[rootOffset] = Lsmax - unlimited + limitRootTolerance;
    roots[rootOffset + 1] = unlimited - Lsmin + limitRootTolerance;
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
    if (updateLimitFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode StabilizerST2CUT::rootCheck(const IOdata& inputs,
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

void StabilizerST2CUT::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "lsmax" || param == "vmax") {
        const double proposed = units::convert(val, unitType, units::one);
        if (!std::isfinite(proposed) || proposed < Lsmin) {
            throw InvalidParameterValue(param);
        }
        Lsmax = proposed;
        return;
    }
    if (param == "lsmin" || param == "vmin") {
        const double proposed = units::convert(val, unitType, units::one);
        if (!std::isfinite(proposed) || proposed > Lsmax) {
            throw InvalidParameterValue(param);
        }
        Lsmin = proposed;
        return;
    }
    if (param == "vcu") {
        const double proposed = units::convert(val, unitType, units::one);
        if (!std::isfinite(proposed)) {
            throw InvalidParameterValue(param);
        }
        const double mapped = proposed == 0.0 ? 999.0 : proposed;
        if (mapped < Vcl) {
            throw InvalidParameterValue(param);
        }
        Vcu = mapped;
        return;
    }
    if (param == "vcl") {
        const double proposed = units::convert(val, unitType, units::one);
        if (!std::isfinite(proposed)) {
            throw InvalidParameterValue(param);
        }
        const double mapped = proposed == 0.0 ? -999.0 : proposed;
        if (mapped > Vcu) {
            throw InvalidParameterValue(param);
        }
        Vcl = mapped;
        return;
    }
    if (!isSetHandled(
            StabilizerST2CUT::getModelDescription().trySet(*this, param, val, unitType))) {
        Stabilizer::set(param, val, unitType);
    }
}

}  // namespace griddyn::stabilizers
