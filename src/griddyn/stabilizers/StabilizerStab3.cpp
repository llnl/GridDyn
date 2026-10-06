/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerStab3.h"

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
    constexpr double inputValidityLimit = 1e20;
}

StabilizerStab3::StabilizerStab3(const std::string& objName): Stabilizer(objName)
{
    m_inputSize = pssInputCount;
    m_outputSize = 1;
}

CoreObject* StabilizerStab3::clone(CoreObject* obj) const
{
    auto* stabilizerClone = cloneBase<StabilizerStab3, Stabilizer>(this, obj);
    if (stabilizerClone == nullptr) {
        return obj;
    }
    stabilizerClone->Tt = Tt;
    stabilizerClone->Tx1 = Tx1;
    stabilizerClone->Tx2 = Tx2;
    stabilizerClone->Kx = Kx;
    stabilizerClone->Vlim = Vlim;
    return stabilizerClone;
}

void StabilizerStab3::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 6> parameters{Tt, Tx1, Tx2, Kx, Vlim, initialElectricalPower};
    if (!std::all_of(parameters.begin(),
                     parameters.end(),
                     [](double value) { return std::isfinite(value); }) ||
        (Tt < 0.0) || (Tx1 < 0.0) || (Tx2 <= 0.0) || (Vlim < 0.0)) {
        throw InvalidParameterValue("STAB3 time constants, gain, or limit");
    }

    transducerState = kNullLocation;
    lowPassState = kNullLocation;
    derivativeState = kNullLocation;
    index_t stateCount = 0;
    if (Tt > 0.0) {
        transducerState = stateCount++;
    }
    if (Tx1 > 0.0) {
        lowPassState = stateCount++;
    }
    derivativeState = stateCount++;

    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = (8 * stateCount) + 4;
}

void StabilizerStab3::dynObjectInitializeB(const IOdata& inputs,
                                           const IOdata& /*desiredOutput*/,
                                           IOdata& /*fieldSet*/)
{
    if ((inputs.size() < pssInputCount) || !std::isfinite(inputs[pssElectricalPowerInLocation]) ||
        (std::abs(inputs[pssElectricalPowerInLocation]) >= inputValidityLimit)) {
        throw InvalidParameterValue("STAB3 controller inputs");
    }
    initialElectricalPower = inputs[pssElectricalPowerInLocation];
    double* state = m_state.data() + 1;
    if (transducerState != kNullLocation) {
        state[transducerState] = initialElectricalPower;
    }
    if (lowPassState != kNullLocation) {
        state[lowPassState] = 0.0;
    }
    state[derivativeState] = 0.0;
    m_state[0] = 0.0;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
}

StabilizerStab3::LinearValue StabilizerStab3::electricalPowerInput(const IOdata& inputs) const
{
    LinearValue input;
    input.value = inputs[pssElectricalPowerInLocation];
    input.inputGain = 1.0;
    return input;
}

StabilizerStab3::LinearValue StabilizerStab3::unlimitedOutput(const IOdata& inputs,
                                                              const double state[]) const
{
    const auto input = electricalPowerInput(inputs);
    LinearValue transducer;
    if (transducerState == kNullLocation) {
        transducer = input;
    } else {
        transducer.value = state[transducerState];
        transducer.stateGain[transducerState] = 1.0;
    }
    transducer.value -= initialElectricalPower;

    LinearValue lowPass;
    if (lowPassState == kNullLocation) {
        lowPass = transducer;
    } else {
        lowPass.value = state[lowPassState];
        lowPass.stateGain[lowPassState] = 1.0;
    }

    LinearValue output = lowPass;
    const double scale = -Kx / Tx2;
    output.value = scale * (lowPass.value - state[derivativeState]);
    for (double& gain : output.stateGain) {
        gain *= scale;
    }
    output.inputGain *= scale;
    output.stateGain[derivativeState] -= scale;
    return output;
}

double StabilizerStab3::output(const IOdata& inputs, const double state[]) const
{
    return std::clamp(unlimitedOutput(inputs, state).value, -Vlim, Vlim);
}

int StabilizerStab3::outputLimitStatus(const IOdata& inputs, const double state[]) const
{
    const double value = unlimitedOutput(inputs, state).value;
    if (value >= Vlim) {
        return 1;
    }
    return (value <= -Vlim) ? -1 : 0;
}

bool StabilizerStab3::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const int limitStatus = outputLimitStatus(inputs, state);
    const bool changed = (opFlags[OUTPUT_LIMITED] != (limitStatus != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (limitStatus > 0));
    opFlags.set(OUTPUT_LIMITED, limitStatus != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, limitStatus > 0);
    return changed;
}

void StabilizerStab3::residual(const IOdata& inputs,
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

void StabilizerStab3::derivative(const IOdata& inputs,
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
    const double electricalPower = inputs[pssElectricalPowerInLocation];
    const double transducer =
        (transducerState == kNullLocation) ? electricalPower : state[transducerState];
    const double feedback = transducer - initialElectricalPower;
    const double lowPass = (lowPassState == kNullLocation) ? feedback : state[lowPassState];
    if (transducerState != kNullLocation) {
        stateDerivative[transducerState] = (electricalPower - state[transducerState]) / Tt;
    }
    if (lowPassState != kNullLocation) {
        stateDerivative[lowPassState] = (feedback - state[lowPassState]) / Tx1;
    }
    stateDerivative[derivativeState] = (lowPass - state[derivativeState]) / Tx2;
}

void StabilizerStab3::jacobianElements(const IOdata& inputs,
                                       const StateData& stateData,
                                       MatrixData<double>& matrixData,
                                       const IOlocs& inputLocs,
                                       const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t refAlg = locations.algOffset;
    const index_t refDiff = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const auto input = electricalPowerInput(inputs);
    LinearValue transducer;
    if (transducerState == kNullLocation) {
        transducer = input;
    } else {
        transducer.value = state[transducerState];
        transducer.stateGain[transducerState] = 1.0;
    }
    transducer.value -= initialElectricalPower;
    LinearValue lowPass;
    if (lowPassState == kNullLocation) {
        lowPass = transducer;
    } else {
        lowPass.value = state[lowPassState];
        lowPass.stateGain[lowPassState] = 1.0;
    }
    const auto finalOutput = unlimitedOutput(inputs, state);
    const auto addExpression = [&matrixData, &inputLocs, refDiff](index_t row,
                                                                  const LinearValue& value,
                                                                  double scale,
                                                                  bool includeStates) {
        if (includeStates) {
            for (index_t index = 0; index < value.stateGain.size(); ++index) {
                if (value.stateGain[index] != 0.0) {
                    matrixData.assign(row, refDiff + index, scale * value.stateGain[index]);
                }
            }
        }
        if (value.inputGain != 0.0) {
            matrixData.assignCheckCol(row,
                                      inputLocs[pssElectricalPowerInLocation],
                                      scale * value.inputGain);
        }
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
    if (transducerState != kNullLocation) {
        addExpression(refDiff + transducerState, input, 1.0 / Tt, true);
        matrixData.assign(refDiff + transducerState,
                          refDiff + transducerState,
                          (-1.0 / Tt) - stateData.cj);
    }
    if (lowPassState != kNullLocation) {
        addExpression(refDiff + lowPassState, transducer, 1.0 / Tx1, true);
        matrixData.assign(refDiff + lowPassState,
                          refDiff + lowPassState,
                          (-1.0 / Tx1) - stateData.cj);
    }
    addExpression(refDiff + derivativeState, lowPass, 1.0 / Tx2, true);
    matrixData.assign(refDiff + derivativeState,
                      refDiff + derivativeState,
                      (-1.0 / Tx2) - stateData.cj);
}

void StabilizerStab3::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
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

void StabilizerStab3::rootTest(const IOdata& inputs,
                               const StateData& stateData,
                               double roots[],
                               const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double unlimited = unlimitedOutput(inputs, locations.diffStateLoc).value;
    roots[rootOffset] = Vlim - unlimited;
    roots[rootOffset + 1] = unlimited + Vlim;
}

void StabilizerStab3::rootTrigger(CoreTime /*time*/,
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

ChangeCode StabilizerStab3::rootCheck(const IOdata& inputs,
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

stringVec StabilizerStab3::localStateNames() const
{
    stringVec names{"vs"};
    if (transducerState != kNullLocation) {
        names.emplace_back("transducer");
    }
    if (lowPassState != kNullLocation) {
        names.emplace_back("lowpass");
    }
    names.emplace_back("washout");
    return names;
}

index_t StabilizerStab3::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "vss") || (field == "vs")) {
        return getOutputLoc(sMode, 0);
    }
    return kInvalidLocation;
}

void StabilizerStab3::set(std::string_view param, std::string_view val)
{
    Stabilizer::set(param, val);
}

void StabilizerStab3::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("STAB3 parameters must be finite");
    }
    if (param == "tt") {
        if (val < 0.0) throw InvalidParameterValue("STAB3 TT must be nonnegative");
        Tt = val;
    } else if (param == "tx1") {
        if (val < 0.0) throw InvalidParameterValue("STAB3 TX1 must be nonnegative");
        Tx1 = val;
    } else if (param == "tx2") {
        if (val <= 0.0) throw InvalidParameterValue("STAB3 TX2 must be positive");
        Tx2 = val;
    } else if (param == "kx") {
        Kx = val;
    } else if ((param == "vlim") || (param == "vmax")) {
        if (val < 0.0) throw InvalidParameterValue("STAB3 VLIM must be nonnegative");
        Vlim = val;
    } else {
        Stabilizer::set(param, val, unitType);
    }
}

double StabilizerStab3::get(std::string_view param, units::unit unitType) const
{
    if (param == "tt") return Tt;
    if (param == "tx1") return Tx1;
    if (param == "tx2") return Tx2;
    if (param == "kx") return Kx;
    if ((param == "vlim") || (param == "vmax")) return Vlim;
    return Stabilizer::get(param, unitType);
}
}  // namespace griddyn::stabilizers
