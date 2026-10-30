/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "StabilizerStab2a.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace griddyn::stabilizers {
StabilizerStab2a::StabilizerStab2a(const std::string& objName): Stabilizer(objName) {}

CoreObject* StabilizerStab2a::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<StabilizerStab2a, Stabilizer>(this, obj);
    if (copy == nullptr) {
        return obj;
    }
    copy->K2 = K2;
    copy->T2 = T2;
    copy->K3 = K3;
    copy->T3 = T3;
    copy->K4 = K4;
    copy->K5 = K5;
    copy->T5 = T5;
    copy->Hlim = Hlim;
    return copy;
}

std::array<double, 6> StabilizerStab2a::timeConstants() const
{
    return {T2, T2, T2, T3, T5, T5};
}

void StabilizerStab2a::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const std::array<double, 8> parameters{K2, T2, K3, T3, K4, K5, T5, Hlim};
    if (!std::all_of(parameters.begin(),
                     parameters.end(),
                     [](double value) { return std::isfinite(value); }) ||
        T2 < 0.0 || T3 < 0.0 || T5 < 0.0 || Hlim < 0.0) {
        throw InvalidParameterValue("STAB2A time constants, gains, or limit");
    }
    const auto times = timeConstants();
    index_t count = 0;
    for (std::size_t i = 0; i < filterStates.size(); ++i) {
        filterStates[i] = times[i] > 0.0 ? count++ : kNullLocation;
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = count;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = (8 * count) + 8;
}

void StabilizerStab2a::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& /*desiredOutput*/,
                                            IOdata& /*fieldSet*/)
{
    if (inputs.size() < pssInputCount || !std::isfinite(inputs[pssElectricalPowerInLocation]) ||
        std::abs(inputs[pssElectricalPowerInLocation]) >= 1e20) {
        throw InvalidParameterValue("STAB2A electrical-power input");
    }
    std::fill(m_state.begin(), m_state.end(), 0.0);
    if (filterStates[0] != kNullLocation) {
        // Every washout has zero steady output; only the first lag tracks PELEC.
        m_state[1 + filterStates[0]] = inputs[pssElectricalPowerInLocation];
    }
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(0.0);
}

StabilizerStab2a::Signals StabilizerStab2a::signals(const IOdata& inputs,
                                                    const double state[]) const
{
    auto scale = [](LinearValue value, double gain) {
        value.value *= gain;
        value.inputGain *= gain;
        for (auto& partial : value.stateGain) {
            partial *= gain;
        }
        return value;
    };
    auto add = [](LinearValue value, const LinearValue& other) {
        value.value += other.value;
        value.inputGain += other.inputGain;
        for (std::size_t i = 0; i < value.stateGain.size(); ++i) {
            value.stateGain[i] += other.stateGain[i];
        }
        return value;
    };
    auto lagOutput = [this, state](std::size_t filter, const LinearValue& target) {
        if (filterStates[filter] == kNullLocation) {
            return target;
        }
        LinearValue value;
        value.value = state[filterStates[filter]];
        value.stateGain[filterStates[filter]] = 1.0;
        return value;
    };
    Signals result;
    LinearValue value;
    value.value = inputs[pssElectricalPowerInLocation];
    value.inputGain = 1.0;
    for (std::size_t i = 0; i < 3; ++i) {
        result.targets[i] = value;
        // s*T2/(1+s*T2) is identically zero when T2 is zero.
        value = T2 == 0.0 ? LinearValue{} :
                            scale(add(value, scale(lagOutput(i, value), -1.0)), i == 0 ? -K2 : K2);
    }
    const auto washout = value;
    result.targets[3] = scale(washout, K3);
    const auto parallel = add(lagOutput(3, result.targets[3]), scale(washout, K4));
    result.targets[4] = scale(parallel, K5);
    result.targets[5] = scale(lagOutput(4, result.targets[4]), K5);
    result.output = lagOutput(5, result.targets[5]);
    return result;
}

int StabilizerStab2a::outputLimitStatus(double value) const
{
    return value >= Hlim ? 1 : (value <= -Hlim ? -1 : 0);
}

bool StabilizerStab2a::updateLimitFlags(double value)
{
    const int status = outputLimitStatus(value);
    const bool changed =
        (opFlags[OUTPUT_LIMITED] != (status != 0)) || (opFlags[OUTPUT_LIMIT_HIGH] != (status > 0));
    opFlags.set(OUTPUT_LIMITED, status != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, status > 0);
    return changed;
}

void StabilizerStab2a::residual(const IOdata& inputs,
                                const StateData& stateData,
                                double resid[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] =
            std::clamp(signals(inputs, locations.diffStateLoc).output.value, -Hlim, Hlim) -
            locations.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t i = 0; i < locations.diffSize; ++i) {
            locations.destDiffLoc[i] -= locations.dstateLoc[i];
        }
    }
}

void StabilizerStab2a::derivative(const IOdata& inputs,
                                  const StateData& stateData,
                                  double deriv[],
                                  const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const auto values = signals(inputs, locations.diffStateLoc);
    const auto times = timeConstants();
    for (std::size_t i = 0; i < filterStates.size(); ++i) {
        const auto index = filterStates[i];
        if (index != kNullLocation) {
            locations.destDiffLoc[index] =
                (values.targets[i].value - locations.diffStateLoc[index]) / times[i];
        }
    }
}

void StabilizerStab2a::algebraicUpdate(const IOdata& inputs,
                                       const StateData& stateData,
                                       double update[],
                                       const SolverMode& sMode,
                                       double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto locations = offsets.getLocations(stateData, update, sMode, this);
        locations.destLoc[0] =
            std::clamp(signals(inputs, locations.diffStateLoc).output.value, -Hlim, Hlim);
    }
}

void StabilizerStab2a::jacobianElements(const IOdata& inputs,
                                        const StateData& stateData,
                                        MatrixData<double>& matrixData,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto values = signals(inputs, locations.diffStateLoc);
    const auto addPartials = [&](index_t row, const LinearValue& value, double scale) {
        if (hasDifferential(sMode)) {
            for (index_t i = 0; i < locations.diffSize; ++i) {
                if (value.stateGain[i] != 0.0) {
                    matrixData.assign(row, locations.diffOffset + i, scale * value.stateGain[i]);
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
        matrixData.assign(locations.algOffset, locations.algOffset, -1.0);
        if (outputLimitStatus(values.output.value) == 0) {
            addPartials(locations.algOffset, values.output, 1.0);
        }
    }
    if (hasDifferential(sMode)) {
        const auto times = timeConstants();
        for (std::size_t i = 0; i < filterStates.size(); ++i) {
            if (filterStates[i] != kNullLocation) {
                const auto row = locations.diffOffset + filterStates[i];
                addPartials(row, values.targets[i], 1.0 / times[i]);
                matrixData.assign(row, row, -1.0 / times[i] - stateData.cj);
            }
        }
    }
}

void StabilizerStab2a::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double step = time - prevTime;
    for (index_t i = 0; i < offsets.local().local.diffSize; ++i) {
        m_state[1 + i] += step * m_dstate_dt[1 + i];
    }
    const double raw = signals(inputs, m_state.data() + 1).output.value;
    m_state[0] = std::clamp(raw, -Hlim, Hlim);
    updateLimitFlags(raw);
    prevTime = time;
}

void StabilizerStab2a::rootTest(const IOdata& inputs,
                                const StateData& stateData,
                                double roots[],
                                const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto value = signals(inputs, locations.diffStateLoc).output;
    // Keep roots clear under small input noise, including bypass feedthrough gain.
    const double margin = 1e-4 * std::max(1.0, std::abs(value.inputGain));
    const auto rootOffset = offsets.getRootOffset(sMode);
    roots[rootOffset] = Hlim - value.value + margin;
    roots[rootOffset + 1] = value.value + Hlim + margin;
}

void StabilizerStab2a::rootTrigger(CoreTime /*time*/,
                                   const IOdata& inputs,
                                   const std::vector<int>& rootMask,
                                   const SolverMode& sMode)
{
    const auto rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] != 0 || rootMask[rootOffset + 1] != 0) &&
        updateLimitFlags(signals(inputs, m_state.data() + 1).output.value)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode StabilizerStab2a::rootCheck(const IOdata& inputs,
                                       const StateData& stateData,
                                       const SolverMode& sMode,
                                       CheckLevel /*level*/)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    if (updateLimitFlags(signals(inputs, locations.diffStateLoc).output.value)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec StabilizerStab2a::localStateNames() const
{
    constexpr std::array<std::string_view, 6> names{
        "washout1", "washout2", "washout3", "lowpass", "outputlag1", "outputlag2"};
    stringVec result{"vs"};
    for (std::size_t i = 0; i < filterStates.size(); ++i) {
        if (filterStates[i] != kNullLocation) {
            result.emplace_back(names[i]);
        }
    }
    return result;
}

index_t StabilizerStab2a::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if (field == "vss" || field == "vs") {
        return getOutputLoc(sMode, 0);
    }
    const auto names = localStateNames();
    const auto match = std::find(names.begin() + 1, names.end(), field);
    if (match != names.end() && hasDifferential(sMode)) {
        return offsets.getDiffOffset(sMode) + std::distance(names.begin() + 1, match);
    }
    return kInvalidLocation;
}

void StabilizerStab2a::set(std::string_view param, std::string_view val)
{
    Stabilizer::set(param, val);
}

void StabilizerStab2a::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("STAB2A parameters must be finite");
    }
    if ((param == "t2" || param == "t3" || param == "t5" || param == "hlim" || param == "h_lim") &&
        val < 0.0) {
        throw InvalidParameterValue("STAB2A time constants and HLIM must be nonnegative");
    }
    if (param == "k2") {
        K2 = val;
    } else if (param == "t2") {
        T2 = val;
    } else if (param == "k3") {
        K3 = val;
    } else if (param == "t3") {
        T3 = val;
    } else if (param == "k4") {
        K4 = val;
    } else if (param == "k5") {
        K5 = val;
    } else if (param == "t5") {
        T5 = val;
    } else if (param == "hlim" || param == "h_lim") {
        Hlim = val;
    } else {
        Stabilizer::set(param, val, unitType);
    }
}

double StabilizerStab2a::get(std::string_view param, units::unit unitType) const
{
    if (param == "k2") {
        return K2;
    }
    if (param == "t2") {
        return T2;
    }
    if (param == "k3") {
        return K3;
    }
    if (param == "t3") {
        return T3;
    }
    if (param == "k4") {
        return K4;
    }
    if (param == "k5") {
        return K5;
    }
    if (param == "t5") {
        return T5;
    }
    if (param == "hlim" || param == "h_lim") {
        return Hlim;
    }
    return Stabilizer::get(param, unitType);
}
}  // namespace griddyn::stabilizers
