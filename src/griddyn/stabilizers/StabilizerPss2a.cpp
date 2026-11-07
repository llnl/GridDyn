/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerPss2a.h"

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

StabilizerPss2a::StabilizerPss2a(const std::string& objName): Stabilizer(objName)
{
    m_inputSize = pssInputCount;
    m_outputSize = 1;
    rampFilterState.fill(kNullLocation);
}

const ModelDescription& StabilizerPss2a::getModelDescription() const
{
    static const auto description = ModelDescription::define<StabilizerPss2a>(
        "StabilizerPss2a",
        &Stabilizer::getModelDescription(),
        {parameter({"mode", "mode1"},
                   &StabilizerPss2a::mode1,
                   units::one,
                   "Input signal selector",
                   supportedMode),
         parameter({"mode2"},
                   &StabilizerPss2a::mode2,
                   units::one,
                   "Input signal selector",
                   supportedMode),
         parameter({"busr", "busr1"},
                   &StabilizerPss2a::remoteBus1,
                   units::one,
                   "Remote bus selector",
                   [](int value) { return value == 0; }),
         parameter({"busr2"},
                   &StabilizerPss2a::remoteBus2,
                   units::one,
                   "Remote bus selector",
                   [](int value) { return value == 0; }),
         parameter({"tw1"},
                   &StabilizerPss2a::Tw1,
                   units::second,
                   "Tw1 time constant",
                   parameterValidators::nonnegative),
         parameter({"tw2"},
                   &StabilizerPss2a::Tw2,
                   units::second,
                   "Tw2 time constant",
                   parameterValidators::nonnegative),
         parameter({"t6"},
                   &StabilizerPss2a::T6,
                   units::second,
                   "T6 time constant",
                   parameterValidators::nonnegative),
         parameter({"tw3"},
                   &StabilizerPss2a::Tw3,
                   units::second,
                   "Tw3 time constant",
                   parameterValidators::nonnegative),
         parameter({"tw4"},
                   &StabilizerPss2a::Tw4,
                   units::second,
                   "Tw4 time constant",
                   parameterValidators::nonnegative),
         parameter({"t7"},
                   &StabilizerPss2a::T7,
                   units::second,
                   "T7 time constant",
                   parameterValidators::nonnegative),
         parameter({"ks1"}, &StabilizerPss2a::Ks1, units::one, "Ks1 gain"),
         parameter({"ks2"}, &StabilizerPss2a::Ks2, units::one, "Ks2 gain"),
         parameter({"ks3"}, &StabilizerPss2a::Ks3, units::one, "Ks3 gain"),
         parameter({"t8"},
                   &StabilizerPss2a::T8,
                   units::second,
                   "T8 time constant",
                   parameterValidators::nonnegative),
         parameter({"t9"},
                   &StabilizerPss2a::T9,
                   units::second,
                   "T9 time constant",
                   parameterValidators::positive),
         parameter({"t1"},
                   &StabilizerPss2a::T1,
                   units::second,
                   "T1 time constant",
                   parameterValidators::nonnegative),
         parameter({"t2"},
                   &StabilizerPss2a::T2,
                   units::second,
                   "T2 time constant",
                   parameterValidators::nonnegative),
         parameter({"t3"},
                   &StabilizerPss2a::T3,
                   units::second,
                   "T3 time constant",
                   parameterValidators::nonnegative),
         parameter({"t4"},
                   &StabilizerPss2a::T4,
                   units::second,
                   "T4 time constant",
                   parameterValidators::nonnegative),
         parameter({"vstmax", "vmax"}, &StabilizerPss2a::Vstmax, units::one, "Vstmax signal limit"),
         parameter({"vstmin", "vmin"}, &StabilizerPss2a::Vstmin, units::one, "Vstmin signal limit"),
         localState("initialPmech", &StabilizerPss2a::initialPmech, "Initialized reference"),
         localState(
             "output_limited", &StabilizerPss2a::opFlags, OUTPUT_LIMITED, "Output limiter active"),
         localState("output_limit_high",
                    &StabilizerPss2a::opFlags,
                    OUTPUT_LIMIT_HIGH,
                    "Upper output limit active"),
         state("vs", units::one, "Stabilizer solver variable vs"),
         state("w1a", units::one, "Stabilizer solver variable w1a"),
         state("w1b", units::one, "Stabilizer solver variable w1b"),
         state("l1", units::one, "Stabilizer solver variable l1"),
         state("w2a", units::one, "Stabilizer solver variable w2a"),
         state("w2b", units::one, "Stabilizer solver variable w2b"),
         state("l2", units::one, "Stabilizer solver variable l2"),
         state("rtf", units::one, "Stabilizer solver variable rtf"),
         state("ll1", units::one, "Stabilizer solver variable ll1"),
         state("ll2", units::one, "Stabilizer solver variable ll2"),
         state("ll3", units::one, "Stabilizer solver variable ll3"),
         state("rtf1", units::one, "Stabilizer solver variable rtf1"),
         state("rtf2", units::one, "Stabilizer solver variable rtf2"),
         state("rtf3", units::one, "Stabilizer solver variable rtf3"),
         state("rtf4", units::one, "Stabilizer solver variable rtf4"),
         state("rtf5", units::one, "Stabilizer solver variable rtf5"),
         state("rtf6", units::one, "Stabilizer solver variable rtf6"),
         state("rtf7", units::one, "Stabilizer solver variable rtf7")});
    return description;
}

CoreObject* StabilizerPss2a::clone(CoreObject* obj) const
{
    auto* stabilizerClone = cloneBase<StabilizerPss2a, Stabilizer>(this, obj);
    if (stabilizerClone == nullptr) {
        return obj;
    }
    stabilizerClone->mode1 = mode1;
    stabilizerClone->remoteBus1 = remoteBus1;
    stabilizerClone->mode2 = mode2;
    stabilizerClone->remoteBus2 = remoteBus2;
    stabilizerClone->Tw1 = Tw1;
    stabilizerClone->Tw2 = Tw2;
    stabilizerClone->T6 = T6;
    stabilizerClone->Tw3 = Tw3;
    stabilizerClone->Tw4 = Tw4;
    stabilizerClone->T7 = T7;
    stabilizerClone->Ks2 = Ks2;
    stabilizerClone->Ks3 = Ks3;
    stabilizerClone->T8 = T8;
    stabilizerClone->T9 = T9;
    stabilizerClone->Ks1 = Ks1;
    stabilizerClone->T1 = T1;
    stabilizerClone->T2 = T2;
    stabilizerClone->T3 = T3;
    stabilizerClone->T4 = T4;
    stabilizerClone->Vstmax = Vstmax;
    stabilizerClone->Vstmin = Vstmin;
    stabilizerClone->rampM = rampM;
    stabilizerClone->rampN = rampN;
    stabilizerClone->T10 = T10;
    stabilizerClone->T11 = T11;
    return stabilizerClone;
}

bool StabilizerPss2a::supportedMode(int mode)
{
    return (mode >= 0) && (mode <= 5);
}

index_t StabilizerPss2a::rampStateCount() const
{
    return (rampM == 0 || rampN == 0) ? 0 : static_cast<index_t>(rampM + rampN);
}

void StabilizerPss2a::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 20> parameters{Tw1, Tw2, T6,  Tw3,    Tw4,    T7,          Ks2,
                                            Ks3, T8,  T9,  Ks1,    T1,     T2,          T3,
                                            T4,  T10, T11, Vstmax, Vstmin, initialPmech};
    if (!std::all_of(parameters.begin(),
                     parameters.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !supportedMode(mode1) || !supportedMode(mode2) || (remoteBus1 != 0) || (remoteBus2 != 0) ||
        (Tw1 < 0.0) || (Tw2 < 0.0) || (T6 < 0.0) || (Tw3 < 0.0) || (Tw4 < 0.0) || (T7 < 0.0) ||
        ((rampStateCount() > 0) && (T9 <= 0.0)) || (T8 < 0.0) || (T2 < 0.0) || (T4 < 0.0) ||
        ((T2 == 0.0) && (T1 > 0.0)) || ((T4 == 0.0) && (T3 > 0.0)) || (T10 < 0.0) || (T11 < 0.0) ||
        ((T11 == 0.0) && (T10 > 0.0)) || (rampM < 0) || (rampN < 0) || (rampM > 8) || (rampN > 8) ||
        (rampM * rampN > 8) || (Vstmax < Vstmin)) {
        throw InvalidParameterValue("PSS2A/PSS2B modes, time constants, or limits");
    }

    branch1Derivative1State = kNullLocation;
    branch1Derivative2State = kNullLocation;
    branch1LagState = kNullLocation;
    branch2Derivative1State = kNullLocation;
    branch2Derivative2State = kNullLocation;
    branch2LagState = kNullLocation;
    rampFilterState.fill(kNullLocation);
    leadLag1State = kNullLocation;
    leadLag2State = kNullLocation;
    leadLag3State = kNullLocation;
    index_t stateCount = 0;
    if (Tw1 > 0.0) {
        branch1Derivative1State = stateCount++;
    }
    if (Tw2 > 0.0) {
        branch1Derivative2State = stateCount++;
    }
    if (T6 > 0.0) {
        branch1LagState = stateCount++;
    }
    if (Tw3 > 0.0) {
        branch2Derivative1State = stateCount++;
    }
    if (Tw4 > 0.0) {
        branch2Derivative2State = stateCount++;
    }
    if (T7 > 0.0) {
        branch2LagState = stateCount++;
    }
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        rampFilterState[stage] = stateCount++;
    }
    if (T2 > 0.0) {
        leadLag1State = stateCount++;
    }
    if (T4 > 0.0) {
        leadLag2State = stateCount++;
    }
    if (T11 > 0.0) {
        leadLag3State = stateCount++;
    }

    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = (8 * stateCount) + 8;
}

void StabilizerPss2a::dynObjectInitializeB(const IOdata& inputs,
                                           const IOdata& /*desiredOutput*/,
                                           IOdata& /*fieldSet*/)
{
    if (inputs.size() < pssInputCount) {
        throw InvalidParameterValue("PSS2A/PSS2B controller inputs");
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
    if (!signalAvailable(mode1) || !signalAvailable(mode2)) {
        throw InvalidParameterValue("PSS2A/PSS2B controller inputs");
    }
    initialPmech = ((mode1 == 4) || (mode2 == 4)) ? inputs[pssPmechInLocation] : 0.0;
    double* state = m_state.data() + 1;
    const auto input1 = selectedInput(inputs, mode1, true);
    const auto input2 = selectedInput(inputs, mode2, false);
    if (branch1Derivative1State != kNullLocation) {
        state[branch1Derivative1State] = input1.value;
    }
    if (branch1Derivative2State != kNullLocation) {
        // A zero first washout time is the algebraic DerivativeLag bypass
        // used by OpenIPSL, so the second stage sees the original input.
        state[branch1Derivative2State] = (Tw1 > 0.0) ? 0.0 : input1.value;
    }
    if (branch1LagState != kNullLocation) {
        state[branch1LagState] = 0.0;
    }
    if (branch2Derivative1State != kNullLocation) {
        state[branch2Derivative1State] = input2.value;
    }
    if (branch2Derivative2State != kNullLocation) {
        state[branch2Derivative2State] = (Tw3 > 0.0) ? 0.0 : input2.value;
    }
    if (branch2LagState != kNullLocation) {
        state[branch2LagState] = 0.0;
    }
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        state[rampFilterState[stage]] = 0.0;
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
    m_state[0] = 0.0;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
}

StabilizerPss2a::LinearValue
    StabilizerPss2a::selectedInput(const IOdata& inputs, int mode, bool first) const
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

StabilizerPss2a::LinearValue StabilizerPss2a::derivativeBranch(const LinearValue& input,
                                                               const double state[],
                                                               index_t firstState,
                                                               index_t secondState,
                                                               index_t lagState,
                                                               double firstTime,
                                                               double secondTime,
                                                               double lagGain)
{
    LinearValue first = input;
    if (firstTime > 0.0) {
        first.value -= state[firstState];
        first.stateGain[firstState] -= 1.0;
    }
    LinearValue second = first;
    if (secondTime > 0.0) {
        second.value -= state[secondState];
        second.stateGain[secondState] -= 1.0;
    }
    if (lagState == kNullLocation) {
        second.value *= lagGain;
        for (double& gain : second.stateGain) {
            gain *= lagGain;
        }
        second.input1Gain *= lagGain;
        second.input2Gain *= lagGain;
        return second;
    }
    LinearValue output;
    // SimpleLag's gain is part of its state equation.  For a dynamic lag
    // the state is already the block output; applying lagGain here would
    // incorrectly turn KS2 into KS2 squared.
    output.value = state[lagState];
    output.stateGain[lagState] = 1.0;
    return output;
}

StabilizerPss2a::LinearValue StabilizerPss2a::rampFilter(const LinearValue& input,
                                                         const double state[]) const
{
    // N lead-lags (1+s*T8)/(1+s*T9) followed by M pure lags give
    // (1+s*T8)^N/(1+s*T9)^(M+N). A zero M or N bypasses the cascade.
    LinearValue previous = input;
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        previous = leadLagOutput(previous,
                                 state,
                                 rampFilterState[stage],
                                 (stage < static_cast<index_t>(rampN)) ? T8 : 0.0,
                                 T9);
    }
    return previous;
}

StabilizerPss2a::LinearValue StabilizerPss2a::leadLagOutput(const LinearValue& input,
                                                            const double state[],
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

StabilizerPss2a::LinearValue StabilizerPss2a::outputExpression(const IOdata& inputs,
                                                               const double state[]) const
{
    const auto branch1 = derivativeBranch(selectedInput(inputs, mode1, true),
                                          state,
                                          branch1Derivative1State,
                                          branch1Derivative2State,
                                          branch1LagState,
                                          Tw1,
                                          Tw2,
                                          1.0);
    const auto branch2 = derivativeBranch(selectedInput(inputs, mode2, false),
                                          state,
                                          branch2Derivative1State,
                                          branch2Derivative2State,
                                          branch2LagState,
                                          Tw3,
                                          Tw4,
                                          Ks2);
    LinearValue summed = branch1;
    summed.value += Ks3 * branch2.value;
    for (std::size_t index = 0; index < summed.stateGain.size(); ++index) {
        summed.stateGain[index] += Ks3 * branch2.stateGain[index];
    }
    summed.input1Gain += Ks3 * branch2.input1Gain;
    summed.input2Gain += Ks3 * branch2.input2Gain;
    const auto ramp = rampFilter(summed, state);
    LinearValue difference = ramp;
    difference.value -= branch2.value;
    for (std::size_t index = 0; index < difference.stateGain.size(); ++index) {
        difference.stateGain[index] -= branch2.stateGain[index];
    }
    difference.input1Gain -= branch2.input1Gain;
    difference.input2Gain -= branch2.input2Gain;
    difference.value *= Ks1;
    for (double& gain : difference.stateGain) {
        gain *= Ks1;
    }
    difference.input1Gain *= Ks1;
    difference.input2Gain *= Ks1;
    const auto first = leadLagOutput(difference, state, leadLag1State, T1, T2);
    const auto second = leadLagOutput(first, state, leadLag2State, T3, T4);
    return leadLagOutput(second, state, leadLag3State, T10, T11);
}

double StabilizerPss2a::output(const IOdata& inputs, const double state[]) const
{
    return std::clamp(outputExpression(inputs, state).value, Vstmin, Vstmax);
}

int StabilizerPss2a::outputLimitStatus(const IOdata& inputs, const double state[]) const
{
    const double value = outputExpression(inputs, state).value;
    if (value >= Vstmax) {
        return 1;
    }
    return (value <= Vstmin) ? -1 : 0;
}

bool StabilizerPss2a::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const int limitStatus = outputLimitStatus(inputs, state);
    const bool changed = (opFlags[OUTPUT_LIMITED] != (limitStatus != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (limitStatus > 0));
    opFlags.set(OUTPUT_LIMITED, limitStatus != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, limitStatus > 0);
    return changed;
}

void StabilizerPss2a::residual(const IOdata& inputs,
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

void StabilizerPss2a::derivative(const IOdata& inputs,
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
    const double input1 = selectedInput(inputs, mode1, true).value;
    const double input2 = selectedInput(inputs, mode2, false).value;

    const double branch1First = (Tw1 > 0.0) ? input1 - state[branch1Derivative1State] : input1;
    const double branch1Second =
        (Tw2 > 0.0) ? branch1First - state[branch1Derivative2State] : branch1First;
    const double branch2First = (Tw3 > 0.0) ? input2 - state[branch2Derivative1State] : input2;
    const double branch2Second =
        (Tw4 > 0.0) ? branch2First - state[branch2Derivative2State] : branch2First;
    if (Tw1 > 0.0) {
        stateDerivative[branch1Derivative1State] = branch1First / Tw1;
    }
    if (Tw2 > 0.0) {
        stateDerivative[branch1Derivative2State] = branch1Second / Tw2;
    }
    if (Tw3 > 0.0) {
        stateDerivative[branch2Derivative1State] = branch2First / Tw3;
    }
    if (Tw4 > 0.0) {
        stateDerivative[branch2Derivative2State] = branch2Second / Tw4;
    }
    const double branch1 = (T6 > 0.0) ? state[branch1LagState] : branch1Second;
    const double branch2 = (T7 > 0.0) ? state[branch2LagState] : Ks2 * branch2Second;
    if (T6 > 0.0) {
        stateDerivative[branch1LagState] = (branch1Second - state[branch1LagState]) / T6;
    }
    if (T7 > 0.0) {
        stateDerivative[branch2LagState] = ((Ks2 * branch2Second) - state[branch2LagState]) / T7;
    }

    const double summed = branch1 + (Ks3 * branch2);
    double previous = summed;
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        stateDerivative[rampFilterState[stage]] = (previous - state[rampFilterState[stage]]) / T9;
        const double ratio = (stage < static_cast<index_t>(rampN)) ? T8 / T9 : 0.0;
        previous =
            state[rampFilterState[stage]] + (ratio * (previous - state[rampFilterState[stage]]));
    }
    const double preCompensator = Ks1 * (previous - branch2);
    const double firstLeadLag = (T2 > 0.0) ?
        state[leadLag1State] + ((T1 / T2) * (preCompensator - state[leadLag1State])) :
        preCompensator;
    if (T2 > 0.0) {
        stateDerivative[leadLag1State] = (preCompensator - state[leadLag1State]) / T2;
    }
    if (T4 > 0.0) {
        stateDerivative[leadLag2State] = (firstLeadLag - state[leadLag2State]) / T4;
    }
    const double secondLeadLag = (T4 > 0.0) ?
        state[leadLag2State] + ((T3 / T4) * (firstLeadLag - state[leadLag2State])) :
        firstLeadLag;
    if (T11 > 0.0) {
        stateDerivative[leadLag3State] = (secondLeadLag - state[leadLag3State]) / T11;
    }
}

void StabilizerPss2a::addLinearInput(MatrixData<double>& matrixData,
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

void StabilizerPss2a::jacobianElements(const IOdata& inputs,
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
    const auto branch1 = derivativeBranch(input1,
                                          state,
                                          branch1Derivative1State,
                                          branch1Derivative2State,
                                          branch1LagState,
                                          Tw1,
                                          Tw2,
                                          1.0);
    const auto branch2 = derivativeBranch(input2,
                                          state,
                                          branch2Derivative1State,
                                          branch2Derivative2State,
                                          branch2LagState,
                                          Tw3,
                                          Tw4,
                                          Ks2);
    LinearValue summed = branch1;
    summed.value += Ks3 * branch2.value;
    for (std::size_t index = 0; index < summed.stateGain.size(); ++index) {
        summed.stateGain[index] += Ks3 * branch2.stateGain[index];
    }
    summed.input1Gain += Ks3 * branch2.input1Gain;
    summed.input2Gain += Ks3 * branch2.input2Gain;
    const auto ramp = rampFilter(summed, state);
    LinearValue difference = ramp;
    difference.value -= branch2.value;
    for (std::size_t index = 0; index < difference.stateGain.size(); ++index) {
        difference.stateGain[index] -= branch2.stateGain[index];
    }
    difference.input1Gain -= branch2.input1Gain;
    difference.input2Gain -= branch2.input2Gain;
    difference.value *= Ks1;
    for (double& gain : difference.stateGain) {
        gain *= Ks1;
    }
    difference.input1Gain *= Ks1;
    difference.input2Gain *= Ks1;
    const auto firstLeadLag = leadLagOutput(difference, state, leadLag1State, T1, T2);
    const auto secondLeadLag = leadLagOutput(firstLeadLag, state, leadLag2State, T3, T4);
    const auto finalOutput = leadLagOutput(secondLeadLag, state, leadLag3State, T10, T11);
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
        if (outputLimitStatus(inputs, state) == 0) {
            addExpression(refAlg, finalOutput, 1.0, !isAlgebraicOnly(sMode));
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    if (Tw1 > 0.0) {
        addExpression(refDiff + branch1Derivative1State, input1, 1.0 / Tw1, true);
        matrixData.assign(refDiff + branch1Derivative1State,
                          refDiff + branch1Derivative1State,
                          (-1.0 / Tw1) - stateData.cj);
    }
    if (Tw2 > 0.0) {
        LinearValue first = input1;
        if (Tw1 > 0.0) {
            first.value -= state[branch1Derivative1State];
            first.stateGain[branch1Derivative1State] -= 1.0;
        }
        addExpression(refDiff + branch1Derivative2State, first, 1.0 / Tw2, true);
        matrixData.assign(refDiff + branch1Derivative2State,
                          refDiff + branch1Derivative2State,
                          (-1.0 / Tw2) - stateData.cj);
    }
    if (T6 > 0.0) {
        LinearValue second = input1;
        if (Tw1 > 0.0) {
            second.value -= state[branch1Derivative1State];
            second.stateGain[branch1Derivative1State] -= 1.0;
        }
        if (Tw2 > 0.0) {
            second.value -= state[branch1Derivative2State];
            second.stateGain[branch1Derivative2State] -= 1.0;
        }
        addExpression(refDiff + branch1LagState, second, 1.0 / T6, true);
        matrixData.assign(refDiff + branch1LagState,
                          refDiff + branch1LagState,
                          (-1.0 / T6) - stateData.cj);
    }
    if (Tw3 > 0.0) {
        addExpression(refDiff + branch2Derivative1State, input2, 1.0 / Tw3, true);
        matrixData.assign(refDiff + branch2Derivative1State,
                          refDiff + branch2Derivative1State,
                          (-1.0 / Tw3) - stateData.cj);
    }
    if (Tw4 > 0.0) {
        LinearValue first = input2;
        if (Tw3 > 0.0) {
            first.value -= state[branch2Derivative1State];
            first.stateGain[branch2Derivative1State] -= 1.0;
        }
        addExpression(refDiff + branch2Derivative2State, first, 1.0 / Tw4, true);
        matrixData.assign(refDiff + branch2Derivative2State,
                          refDiff + branch2Derivative2State,
                          (-1.0 / Tw4) - stateData.cj);
    }
    if (T7 > 0.0) {
        LinearValue second = input2;
        if (Tw3 > 0.0) {
            second.value -= state[branch2Derivative1State];
            second.stateGain[branch2Derivative1State] -= 1.0;
        }
        if (Tw4 > 0.0) {
            second.value -= state[branch2Derivative2State];
            second.stateGain[branch2Derivative2State] -= 1.0;
        }
        addExpression(refDiff + branch2LagState, second, Ks2 / T7, true);
        matrixData.assign(refDiff + branch2LagState,
                          refDiff + branch2LagState,
                          (-1.0 / T7) - stateData.cj);
    }

    LinearValue rampInput = summed;
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        const double lagTime = T9;
        addExpression(refDiff + rampFilterState[stage], rampInput, 1.0 / lagTime, true);
        matrixData.assign(refDiff + rampFilterState[stage],
                          refDiff + rampFilterState[stage],
                          (-1.0 / lagTime) - stateData.cj);
        rampInput = leadLagOutput(rampInput,
                                  state,
                                  rampFilterState[stage],
                                  (stage < static_cast<index_t>(rampN)) ? T8 : 0.0,
                                  T9);
    }
    if (T2 > 0.0) {
        addExpression(refDiff + leadLag1State, difference, 1.0 / T2, true);
        matrixData.assign(refDiff + leadLag1State,
                          refDiff + leadLag1State,
                          (-1.0 / T2) - stateData.cj);
    }
    if (T4 > 0.0) {
        addExpression(refDiff + leadLag2State, firstLeadLag, 1.0 / T4, true);
        matrixData.assign(refDiff + leadLag2State,
                          refDiff + leadLag2State,
                          (-1.0 / T4) - stateData.cj);
    }
    if (T11 > 0.0) {
        addExpression(refDiff + leadLag3State, secondLeadLag, 1.0 / T11, true);
        matrixData.assign(refDiff + leadLag3State,
                          refDiff + leadLag3State,
                          -(1.0 / T11) - stateData.cj);
    }
}

void StabilizerPss2a::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
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

void StabilizerPss2a::rootTest(const IOdata& inputs,
                               const StateData& stateData,
                               double roots[],
                               const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double unlimited = outputExpression(inputs, locations.diffStateLoc).value;
    roots[rootOffset] = Vstmax - unlimited + limitRootTolerance;
    roots[rootOffset + 1] = unlimited - Vstmin + limitRootTolerance;
}

void StabilizerPss2a::rootTrigger(CoreTime /*time*/,
                                  const IOdata& inputs,
                                  const std::vector<int>& rootMask,
                                  const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] == 0) && (rootMask[rootOffset + 1] == 0)) {
        return;
    }
    if (updateLimitFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode StabilizerPss2a::rootCheck(const IOdata& inputs,
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

stringVec StabilizerPss2a::localStateNames() const
{
    stringVec names{"vs"};
    if (branch1Derivative1State != kNullLocation) {
        names.emplace_back("w1a");
    }
    if (branch1Derivative2State != kNullLocation) {
        names.emplace_back("w1b");
    }
    if (branch1LagState != kNullLocation) {
        names.emplace_back("l1");
    }
    if (branch2Derivative1State != kNullLocation) {
        names.emplace_back("w2a");
    }
    if (branch2Derivative2State != kNullLocation) {
        names.emplace_back("w2b");
    }
    if (branch2LagState != kNullLocation) {
        names.emplace_back("l2");
    }
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        names.emplace_back(stage == 0 ? "rtf" : "rtf" + std::to_string(stage));
    }
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

index_t StabilizerPss2a::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "vss") || (field == "vs")) {
        return getOutputLoc(sMode, 0);
    }
    return kInvalidLocation;
}

void StabilizerPss2a::set(std::string_view param, std::string_view val)
{
    Stabilizer::set(param, val);
}

void StabilizerPss2a::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "vstmax" || param == "vmax") {
        const double proposed = units::convert(val, unitType, units::one);
        if (!std::isfinite(proposed) || proposed < Vstmin) {
            throw InvalidParameterValue(param);
        }
        Vstmax = proposed;
        return;
    }
    if (param == "vstmin" || param == "vmin") {
        const double proposed = units::convert(val, unitType, units::one);
        if (!std::isfinite(proposed) || proposed > Vstmax) {
            throw InvalidParameterValue(param);
        }
        Vstmin = proposed;
        return;
    }
    if (!isSetHandled(
            StabilizerPss2a::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        Stabilizer::set(param, val, unitType);
    }
}

double StabilizerPss2a::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            StabilizerPss2a::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return Stabilizer::get(param, unitType);
}
}  // namespace griddyn::stabilizers
