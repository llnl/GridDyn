/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerIee2st.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace griddyn::stabilizers {
namespace {
    // Keep output-limit roots clear under 10x-relative-tolerance input noise,
    // including the cascade gain from frequency to stabilizer output.
    constexpr double limitRootTolerance = 1e-4;
    constexpr double inputValidityLimit = 1e20;
}  // namespace

StabilizerIee2st::StabilizerIee2st(const std::string& objName): Stabilizer(objName)
{
    m_inputSize = pssInputCount;
    m_outputSize = 1;
}

const ModelDescription& StabilizerIee2st::getModelDescription() const
{
    static const auto description = ModelDescription::define<StabilizerIee2st>(
        "StabilizerIee2st",
        &Stabilizer::getModelDescription(),
        {parameter({"mode", "mode1"},
                   &StabilizerIee2st::mode1,
                   units::one,
                   "Input signal selector",
                   supportedMode),
         parameter({"mode2"},
                   &StabilizerIee2st::mode2,
                   units::one,
                   "Input signal selector",
                   supportedMode),
         parameter({"busr", "busr1"},
                   &StabilizerIee2st::remoteBus1,
                   units::one,
                   "Remote bus selector",
                   [](int value) { return value == 0; }),
         parameter({"busr2"},
                   &StabilizerIee2st::remoteBus2,
                   units::one,
                   "Remote bus selector",
                   [](int value) { return value == 0; }),
         parameter({"k1"}, &StabilizerIee2st::K1, units::one, "K1 gain"),
         parameter({"k2"}, &StabilizerIee2st::K2, units::one, "K2 gain"),
         parameter({"t1"},
                   &StabilizerIee2st::T1,
                   units::second,
                   "T1 time constant",
                   parameterValidators::nonnegative),
         parameter({"t2"},
                   &StabilizerIee2st::T2,
                   units::second,
                   "T2 time constant",
                   parameterValidators::nonnegative),
         parameter({"t3"},
                   &StabilizerIee2st::T3,
                   units::second,
                   "T3 time constant",
                   parameterValidators::nonnegative),
         parameter({"t4"},
                   &StabilizerIee2st::T4,
                   units::second,
                   "T4 time constant",
                   parameterValidators::positive),
         parameter({"t5"},
                   &StabilizerIee2st::T5,
                   units::second,
                   "T5 time constant",
                   parameterValidators::nonnegative),
         parameter({"t6"},
                   &StabilizerIee2st::T6,
                   units::second,
                   "T6 time constant",
                   parameterValidators::nonnegative),
         parameter({"t7"},
                   &StabilizerIee2st::T7,
                   units::second,
                   "T7 time constant",
                   parameterValidators::nonnegative),
         parameter({"t8"},
                   &StabilizerIee2st::T8,
                   units::second,
                   "T8 time constant",
                   parameterValidators::nonnegative),
         parameter({"t9"},
                   &StabilizerIee2st::T9,
                   units::second,
                   "T9 time constant",
                   parameterValidators::nonnegative),
         parameter({"t10"},
                   &StabilizerIee2st::T10,
                   units::second,
                   "T10 time constant",
                   parameterValidators::nonnegative),
         parameter({"lsmax", "vmax"}, &StabilizerIee2st::Lsmax, units::one, "Lsmax signal limit"),
         parameter({"lsmin", "vmin"}, &StabilizerIee2st::Lsmin, units::one, "Lsmin signal limit"),
         parameter({"vcu"}, &StabilizerIee2st::Vcu, units::one, "Vcu gain"),
         parameter({"vcl"}, &StabilizerIee2st::Vcl, units::one, "Vcl gain"),
         localState("initialVoltage", &StabilizerIee2st::initialVoltage, "Initialized reference"),
         localState("initialPmech", &StabilizerIee2st::initialPmech, "Initialized reference"),
         localState(
             "output_limited", &StabilizerIee2st::opFlags, OUTPUT_LIMITED, "Output limiter active"),
         localState("output_limit_high",
                    &StabilizerIee2st::opFlags,
                    OUTPUT_LIMIT_HIGH,
                    "Upper output limit active"),
         localState(
             "voltage_gated", &StabilizerIee2st::opFlags, VOLTAGE_GATED, "Voltage gating history"),
         state("vs", units::one, "Stabilizer solver variable vs"),
         state("f1", units::one, "Stabilizer solver variable f1"),
         state("f2", units::one, "Stabilizer solver variable f2"),
         state("wo", units::one, "Stabilizer solver variable wo"),
         state("ll1", units::one, "Stabilizer solver variable ll1"),
         state("ll2", units::one, "Stabilizer solver variable ll2"),
         state("ll3", units::one, "Stabilizer solver variable ll3")});
    return description;
}

CoreObject* StabilizerIee2st::clone(CoreObject* obj) const
{
    auto* stabilizerClone = cloneBase<StabilizerIee2st, Stabilizer>(this, obj);
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

bool StabilizerIee2st::supportedMode(int mode)
{
    return (mode >= 0) && (mode <= 5);
}

void StabilizerIee2st::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 18> parameters{K1,
                                            K2,
                                            T1,
                                            T2,
                                            T3,
                                            T4,
                                            T5,
                                            T6,
                                            T7,
                                            T8,
                                            T9,
                                            T10,
                                            Lsmax,
                                            Lsmin,
                                            Vcu,
                                            Vcl,
                                            initialVoltage,
                                            initialPmech};
    if (!std::all_of(parameters.begin(),
                     parameters.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !supportedMode(mode1) || !supportedMode(mode2) || (remoteBus1 != 0) || (remoteBus2 != 0) ||
        (T1 < 0.0) || (T2 < 0.0) || (T3 < 0.0) || (T4 <= 0.0) || (T5 < 0.0) || (T6 < 0.0) ||
        (T7 < 0.0) || (T8 < 0.0) || (T9 < 0.0) || (T10 < 0.0) || ((T6 == 0.0) && (T5 > 0.0)) ||
        ((T8 == 0.0) && (T7 > 0.0)) || ((T10 == 0.0) && (T9 > 0.0)) || (Lsmax < Lsmin) ||
        (Vcu < Vcl)) {
        throw InvalidParameterValue("IEE2ST modes, time constants, or limits");
    }

    filter1State = kNullLocation;
    filter2State = kNullLocation;
    washoutState = kNullLocation;
    leadLag1State = kNullLocation;
    leadLag2State = kNullLocation;
    leadLag3State = kNullLocation;
    index_t stateCount = 0;
    if (T1 > 0.0) {
        filter1State = stateCount++;
    }
    if (T2 > 0.0) {
        filter2State = stateCount++;
    }
    washoutState = stateCount++;
    if (T6 > 0.0) {
        leadLag1State = stateCount++;
    }
    if (T8 > 0.0) {
        leadLag2State = stateCount++;
    }
    if (T10 > 0.0) {
        leadLag3State = stateCount++;
    }

    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.algRoots = 4;
    offsets.local().local.jacSize = (8 * stateCount) + 8;
}

void StabilizerIee2st::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& /*desiredOutput*/,
                                            IOdata& /*fieldSet*/)
{
    if (inputs.size() < pssInputCount) {
        throw InvalidParameterValue("IEE2ST controller inputs");
    }
    const auto validInput = [&inputs](index_t index) {
        return std::isfinite(inputs[index]) && (std::abs(inputs[index]) < inputValidityLimit);
    };
    const auto signalAvailable = [&validInput](int mode) {
        switch (mode) {
            case 0:
                return true;
            case 1:
            case 2:
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
    if (!validInput(pssVoltageInLocation) || !signalAvailable(mode1) || !signalAvailable(mode2)) {
        throw InvalidParameterValue("IEE2ST controller inputs");
    }
    initialVoltage = inputs[pssVoltageInLocation];
    initialPmech = ((mode1 == 4) || (mode2 == 4)) ? inputs[pssPmechInLocation] : 0.0;
    const auto input1 = selectedInput(inputs, mode1, true);
    const auto input2 = selectedInput(inputs, mode2, false);
    double* state = m_state.data() + 1;
    if (filter1State != kNullLocation) {
        state[filter1State] = K1 * input1.value;
    }
    if (filter2State != kNullLocation) {
        state[filter2State] = K2 * input2.value;
    }
    if (leadLag1State != kNullLocation) {
        state[leadLag1State] = 0.0;
    }
    if (leadLag2State != kNullLocation) {
        state[leadLag2State] = 0.0;
    }
    if (leadLag3State != kNullLocation) {
        state[leadLag3State] = 0.0;
    }

    // The washout state is the filtered-input sum at initialization.  Build
    // it explicitly so this remains correct for both dynamic and bypassed
    // input filters.
    const double filter1 =
        (filter1State == kNullLocation) ? K1 * input1.value : state[filter1State];
    const double filter2 =
        (filter2State == kNullLocation) ? K2 * input2.value : state[filter2State];
    state[washoutState] = filter1 + filter2;
    m_state[0] = 0.0;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
}

StabilizerIee2st::LinearValue
    StabilizerIee2st::selectedInput(const IOdata& inputs, int mode, bool first) const
{
    LinearValue input;
    switch (mode) {
        case 1:
        case 2:
            input.value = inputs[pssOmegaInLocation] - 1.0;
            (first ? input.input1Gain : input.input2Gain) = 1.0;
            break;
        case 3:
            input.value = inputs[pssElectricalPowerInLocation];
            (first ? input.input1Gain : input.input2Gain) = 1.0;
            break;
        case 4:
            input.value = inputs[pssPmechInLocation] - initialPmech;
            (first ? input.input1Gain : input.input2Gain) = 1.0;
            break;
        case 5:
            input.value = inputs[pssVoltageInLocation];
            (first ? input.input1Gain : input.input2Gain) = 1.0;
            break;
        default:
            break;
    }
    return input;
}

StabilizerIee2st::LinearValue StabilizerIee2st::leadLagOutput(const double state[],
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
    for (double& gain : output.stateGain) {
        gain *= scale;
    }
    output.input1Gain *= scale;
    output.input2Gain *= scale;
    output.stateGain[stateIndex] += 1.0 - scale;
    return output;
}

StabilizerIee2st::LinearValue StabilizerIee2st::outputExpression(const IOdata& inputs,
                                                                 const double state[]) const
{
    const auto input1 = selectedInput(inputs, mode1, true);
    const auto input2 = selectedInput(inputs, mode2, false);
    LinearValue filter1;
    if (filter1State == kNullLocation) {
        filter1 = input1;
        filter1.value *= K1;
        filter1.input1Gain *= K1;
    } else {
        filter1.value = state[filter1State];
        filter1.stateGain[filter1State] = 1.0;
    }
    LinearValue filter2;
    if (filter2State == kNullLocation) {
        filter2 = input2;
        filter2.value *= K2;
        filter2.input2Gain *= K2;
    } else {
        filter2.value = state[filter2State];
        filter2.stateGain[filter2State] = 1.0;
    }
    LinearValue summed = filter1;
    summed.value += filter2.value;
    for (std::size_t index = 0; index < summed.stateGain.size(); ++index) {
        summed.stateGain[index] += filter2.stateGain[index];
    }
    summed.input1Gain += filter2.input1Gain;
    summed.input2Gain += filter2.input2Gain;

    // DerivativeLag is an algebraic pass-through when its numerator is
    // zero.  Its unused dynamic realization still advances below, matching
    // OpenIPSL, but it must not remove the signal from the PSS cascade.
    LinearValue washout = summed;
    if (T3 > 0.0) {
        const double scale = T3 / T4;
        washout.value = scale * (summed.value - state[washoutState]);
        for (double& gain : washout.stateGain) {
            gain *= scale;
        }
        washout.input1Gain *= scale;
        washout.input2Gain *= scale;
        washout.stateGain[washoutState] -= scale;
    }
    const auto firstLeadLag = leadLagOutput(state, washout, leadLag1State, T5, T6);
    const auto secondLeadLag = leadLagOutput(state, firstLeadLag, leadLag2State, T7, T8);
    return leadLagOutput(state, secondLeadLag, leadLag3State, T9, T10);
}

double StabilizerIee2st::output(const IOdata& inputs, const double state[]) const
{
    if (!voltageEnabled(inputs)) {
        return 0.0;
    }
    return std::clamp(outputExpression(inputs, state).value, Lsmin, Lsmax);
}

bool StabilizerIee2st::voltageEnabled(const IOdata& inputs) const
{
    return (inputs[pssVoltageInLocation] >= initialVoltage + Vcl) &&
        (inputs[pssVoltageInLocation] <= initialVoltage + Vcu);
}

int StabilizerIee2st::outputLimitStatus(const IOdata& inputs, const double state[]) const
{
    const double outputValue = outputExpression(inputs, state).value;
    if (outputValue >= Lsmax) {
        return 1;
    }
    return (outputValue <= Lsmin) ? -1 : 0;
}

bool StabilizerIee2st::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const int limitStatus = outputLimitStatus(inputs, state);
    const bool gated = !voltageEnabled(inputs);
    const bool changed = (opFlags[OUTPUT_LIMITED] != (limitStatus != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (limitStatus > 0)) || (opFlags[VOLTAGE_GATED] != gated);
    opFlags.set(OUTPUT_LIMITED, limitStatus != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, limitStatus > 0);
    opFlags.set(VOLTAGE_GATED, gated);
    return changed;
}

void StabilizerIee2st::residual(const IOdata& inputs,
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

void StabilizerIee2st::derivative(const IOdata& inputs,
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
    const auto input1 = selectedInput(inputs, mode1, true);
    const auto input2 = selectedInput(inputs, mode2, false);
    const double filter1 =
        (filter1State == kNullLocation) ? K1 * input1.value : state[filter1State];
    const double filter2 =
        (filter2State == kNullLocation) ? K2 * input2.value : state[filter2State];
    const double summed = filter1 + filter2;
    if (filter1State != kNullLocation) {
        stateDerivative[filter1State] = ((K1 * input1.value) - state[filter1State]) / T1;
    }
    if (filter2State != kNullLocation) {
        stateDerivative[filter2State] = ((K2 * input2.value) - state[filter2State]) / T2;
    }
    stateDerivative[washoutState] = (summed - state[washoutState]) / T4;
    const double washout = (T3 > 0.0) ? (T3 / T4) * (summed - state[washoutState]) : summed;
    const double first = (leadLag1State == kNullLocation) ?
        washout :
        state[leadLag1State] + ((T5 / T6) * (washout - state[leadLag1State]));
    const double second = (leadLag2State == kNullLocation) ?
        first :
        state[leadLag2State] + ((T7 / T8) * (first - state[leadLag2State]));
    if (leadLag1State != kNullLocation) {
        stateDerivative[leadLag1State] = (washout - state[leadLag1State]) / T6;
    }
    if (leadLag2State != kNullLocation) {
        stateDerivative[leadLag2State] = (first - state[leadLag2State]) / T8;
    }
    if (leadLag3State != kNullLocation) {
        stateDerivative[leadLag3State] = (second - state[leadLag3State]) / T10;
    }
}

void StabilizerIee2st::addLinearInput(MatrixData<double>& matrixData,
                                      index_t row,
                                      const LinearValue& value,
                                      double scale,
                                      const IOlocs& inputLocs) const
{
    const auto addSignal = [&matrixData, &inputLocs, row, scale](int mode, double gain) {
        if (gain == 0.0) {
            return;
        }
        index_t location = kInvalidLocation;
        if ((mode == 1) || (mode == 2)) {
            location = inputLocs[pssOmegaInLocation];
        } else if (mode == 3) {
            location = inputLocs[pssElectricalPowerInLocation];
        } else if (mode == 4) {
            location = inputLocs[pssPmechInLocation];
        } else if (mode == 5) {
            location = inputLocs[pssVoltageInLocation];
        }
        if (location != kInvalidLocation) {
            matrixData.assignCheckCol(row, location, scale * gain);
        }
    };
    addSignal(mode1, value.input1Gain);
    addSignal(mode2, value.input2Gain);
}

void StabilizerIee2st::jacobianElements(const IOdata& inputs,
                                        const StateData& stateData,
                                        MatrixData<double>& matrixData,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t refAlg = locations.algOffset;
    const index_t refDiff = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const auto input1 = selectedInput(inputs, mode1, true);
    const auto input2 = selectedInput(inputs, mode2, false);
    LinearValue filter1;
    if (filter1State == kNullLocation) {
        filter1 = input1;
        filter1.value *= K1;
        filter1.input1Gain *= K1;
    } else {
        filter1.stateGain[filter1State] = 1.0;
        filter1.value = state[filter1State];
    }
    LinearValue filter2;
    if (filter2State == kNullLocation) {
        filter2 = input2;
        filter2.value *= K2;
        filter2.input2Gain *= K2;
    } else {
        filter2.stateGain[filter2State] = 1.0;
        filter2.value = state[filter2State];
    }
    LinearValue summed = filter1;
    summed.value += filter2.value;
    for (std::size_t index = 0; index < summed.stateGain.size(); ++index) {
        summed.stateGain[index] += filter2.stateGain[index];
    }
    summed.input1Gain += filter2.input1Gain;
    summed.input2Gain += filter2.input2Gain;
    LinearValue washout = summed;
    if (T3 > 0.0) {
        const double scale = T3 / T4;
        washout.value = scale * (summed.value - state[washoutState]);
        for (double& gain : washout.stateGain) {
            gain *= scale;
        }
        washout.input1Gain *= scale;
        washout.input2Gain *= scale;
        washout.stateGain[washoutState] -= scale;
    }
    const auto firstLeadLag = leadLagOutput(state, washout, leadLag1State, T5, T6);
    const auto secondLeadLag = leadLagOutput(state, firstLeadLag, leadLag2State, T7, T8);
    const auto finalOutput = leadLagOutput(state, secondLeadLag, leadLag3State, T9, T10);

    const auto addExpression = [this, &matrixData, &inputLocs, refDiff](index_t row,
                                                                        const LinearValue& value,
                                                                        double scale,
                                                                        bool includeStates) {
        if (includeStates) {
            for (std::size_t index = 0; index < value.stateGain.size(); ++index) {
                if (value.stateGain[index] != 0.0) {
                    matrixData.assign(row,
                                      refDiff + static_cast<index_t>(index),
                                      scale * value.stateGain[index]);
                }
            }
        }
        addLinearInput(matrixData, row, value, scale, inputLocs);
    };

    if (hasAlgebraic(sMode)) {
        matrixData.assign(refAlg, refAlg, -1.0);
        if (voltageEnabled(inputs) && (outputLimitStatus(inputs, state) == 0)) {
            addExpression(refAlg, finalOutput, 1.0, !isAlgebraicOnly(sMode));
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    if (filter1State != kNullLocation) {
        addExpression(refDiff + filter1State, input1, K1 / T1, true);
        matrixData.assign(refDiff + filter1State,
                          refDiff + filter1State,
                          (-1.0 / T1) - stateData.cj);
    }
    if (filter2State != kNullLocation) {
        addExpression(refDiff + filter2State, input2, K2 / T2, true);
        matrixData.assign(refDiff + filter2State,
                          refDiff + filter2State,
                          (-1.0 / T2) - stateData.cj);
    }
    addExpression(refDiff + washoutState, summed, 1.0 / T4, true);
    matrixData.assign(refDiff + washoutState, refDiff + washoutState, (-1.0 / T4) - stateData.cj);

    if (leadLag1State != kNullLocation) {
        addExpression(refDiff + leadLag1State, washout, 1.0 / T6, true);
        matrixData.assign(refDiff + leadLag1State,
                          refDiff + leadLag1State,
                          (-1.0 / T6) - stateData.cj);
    }
    if (leadLag2State != kNullLocation) {
        addExpression(refDiff + leadLag2State, firstLeadLag, 1.0 / T8, true);
        matrixData.assign(refDiff + leadLag2State,
                          refDiff + leadLag2State,
                          (-1.0 / T8) - stateData.cj);
    }
    if (leadLag3State != kNullLocation) {
        addExpression(refDiff + leadLag3State, secondLeadLag, 1.0 / T10, true);
        matrixData.assign(refDiff + leadLag3State,
                          refDiff + leadLag3State,
                          (-1.0 / T10) - stateData.cj);
    }
}

void StabilizerIee2st::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    double* state = m_state.data() + 1;
    const double* stateDerivative = m_dstate_dt.data() + 1;
    for (index_t index = 0; index < offsets.local().local.diffSize; ++index) {
        state[index] += timeStep * stateDerivative[index];
    }
    m_state[0] = output(inputs, state);
    updateLimitFlags(inputs, state);
    prevTime = time;
}

void StabilizerIee2st::rootTest(const IOdata& inputs,
                                const StateData& stateData,
                                double roots[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double unlimited = outputExpression(inputs, locations.diffStateLoc).value;
    roots[rootOffset] = Lsmax - unlimited + limitRootTolerance;
    roots[rootOffset + 1] = unlimited - Lsmin + limitRootTolerance;
    roots[rootOffset + 2] = inputs[pssVoltageInLocation] - (initialVoltage + Vcl);
    roots[rootOffset + 3] = (initialVoltage + Vcu) - inputs[pssVoltageInLocation];
}

void StabilizerIee2st::rootTrigger(CoreTime /*time*/,
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

ChangeCode StabilizerIee2st::rootCheck(const IOdata& inputs,
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

stringVec StabilizerIee2st::localStateNames() const
{
    stringVec names{"vs"};
    if (filter1State != kNullLocation) {
        names.emplace_back("f1");
    }
    if (filter2State != kNullLocation) {
        names.emplace_back("f2");
    }
    names.emplace_back("wo");
    if (leadLag1State != kNullLocation) {
        names.emplace_back("ll1");
    }
    if (leadLag2State != kNullLocation) {
        names.emplace_back("ll2");
    }
    if (leadLag3State != kNullLocation) {
        names.emplace_back("ll3");
    }
    return names;
}

index_t StabilizerIee2st::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "vss") || (field == "vs")) {
        return getOutputLoc(sMode, 0);
    }
    return kInvalidLocation;
}

void StabilizerIee2st::set(std::string_view param, std::string_view val)
{
    Stabilizer::set(param, val);
}

void StabilizerIee2st::set(std::string_view param, double val, units::unit unitType)
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
            StabilizerIee2st::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        Stabilizer::set(param, val, unitType);
    }
}

}  // namespace griddyn::stabilizers
