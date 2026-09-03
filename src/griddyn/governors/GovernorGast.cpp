/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorGast.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::governors {
namespace {
    constexpr index_t pmechState = 0;
    constexpr index_t valveState = 0;
    constexpr index_t turbineState = 1;
    constexpr index_t temperatureState = 2;
}  // namespace

GovernorGast::GovernorGast(const std::string& objName): Governor(objName)
{
    m_inputSize = 3;
    T1 = 0.4;
    T2 = 0.1;
    T3 = 3.0;
    Pmax = 1.0;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
    opFlags.set(USES_POWER_LIMITS);
}

CoreObject* GovernorGast::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorGast, Governor>(this, obj);
    if (out != nullptr) {
        out->R = R;
        out->AT = AT;
        out->KT = KT;
        out->Dt = Dt;
        out->effectiveT1 = effectiveT1;
        out->effectiveT2 = effectiveT2;
        out->effectiveT3 = effectiveT3;
        out->responseMaximum = responseMaximum;
        out->responseMinimum = responseMinimum;
    }
    return (out == nullptr) ? obj : out;
}

void GovernorGast::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 10> parameters{R, T1, T2, T3, AT, KT, Pmax, Pmin, Dt, Pset};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (R <= 0.0) || (T1 < 0.0) || (T2 < 0.0) || (T3 < 0.0) || (AT < 0.0) || (KT < 0.0) ||
        (Dt < 0.0) || (Pmax < Pmin)) {
        throw InvalidParameterValue("GAST gains, time constants, or valve limits");
    }
    effectiveT1 = std::max(static_cast<double>(T1), minimumTimeConstant);
    effectiveT2 = std::max(static_cast<double>(T2), minimumTimeConstant);
    effectiveT3 = std::max(static_cast<double>(T3), minimumTimeConstant);
    responseMaximum = Pmax;
    responseMinimum = Pmin;
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 3;
    local.jacSize = 12;
    prevTime = time0;
}

void GovernorGast::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[pmechState])) {
        throw InvalidParameterValue("GAST initial mechanical power");
    }
    if (inputs.size() <= govOmegaInLocation || !std::isfinite(inputs[govOmegaInLocation])) {
        throw InvalidParameterValue("GAST initial speed");
    }
    const double initialPower = desiredOutput[pmechState];
    const double speedDeviation = inputs[govOmegaInLocation] - 1.0;
    const double initialFlow = initialPower + (Dt * speedDeviation);
    if (!std::isfinite(initialFlow)) {
        throw InvalidParameterValue("GAST initial fuel flow");
    }
    const double initialTemperatureRequest = AT + (KT * (AT - initialFlow));
    if (!std::isfinite(initialTemperatureRequest) ||
        (initialTemperatureRequest < initialFlow - 1e-9)) {
        throw InvalidParameterValue("GAST initial operating point is temperature limited");
    }
    Pset = initialFlow + (speedDeviation / R);
    // Match the GridKit and ANDES anti-windup realization: an initial point
    // outside the entered response limits is retained and may move only back
    // toward the entered range. This avoids an initialization discontinuity.
    responseMaximum = std::max(static_cast<double>(Pmax), initialFlow);
    responseMinimum = std::min(static_cast<double>(Pmin), initialFlow);
    m_state[0] = initialPower;
    m_state[1] = initialFlow;
    m_state[2] = initialFlow;
    m_state[3] = initialFlow;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

double GovernorGast::speedDroopRequest(const IOdata& inputs) const
{
    return inputs[govpSetInLocation] - (inputs[govOmegaInLocation] - 1.0) / R;
}

double GovernorGast::temperatureRequest(const double state[]) const
{
    return AT + (KT * (AT - state[temperatureState]));
}

double GovernorGast::valveDerivative(const IOdata& inputs, const double state[]) const
{
    const double request = std::min(speedDroopRequest(inputs), temperatureRequest(state));
    const double derivative = (request - state[valveState]) / effectiveT1;
    if (((state[valveState] >= responseMaximum) && (derivative > 0.0)) ||
        ((state[valveState] <= responseMinimum) && (derivative < 0.0))) {
        return 0.0;
    }
    return derivative;
}

double GovernorGast::mechanicalPower(const IOdata& inputs, const double state[]) const
{
    return state[turbineState] - (Dt * (inputs[govOmegaInLocation] - 1.0));
}

void GovernorGast::residual(const IOdata& inputs,
                            const StateData& stateData,
                            double resid[],
                            const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[pmechState] =
            mechanicalPower(inputs, loc.diffStateLoc) - loc.algStateLoc[pmechState];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t kk = 0; kk < loc.diffSize; ++kk) {
        loc.destDiffLoc[kk] -= loc.dstateLoc[kk];
    }
}

void GovernorGast::derivative(const IOdata& inputs,
                              const StateData& stateData,
                              double deriv[],
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    loc.destDiffLoc[valveState] = valveDerivative(inputs, state);
    loc.destDiffLoc[turbineState] = (state[valveState] - state[turbineState]) / effectiveT2;
    loc.destDiffLoc[temperatureState] =
        (state[turbineState] - state[temperatureState]) / effectiveT3;
}

void GovernorGast::algebraicUpdate(const IOdata& inputs,
                                   const StateData& stateData,
                                   double update[],
                                   const SolverMode& sMode,
                                   double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[pmechState] = mechanicalPower(inputs, loc.diffStateLoc);
    }
}

void GovernorGast::jacobianElements(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const index_t alg = loc.algOffset;
    const index_t diff = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg, alg, -1.0);
        matrixData.assign(alg, diff + turbineState, 1.0);
        if (inputLocs[govOmegaInLocation] != kNullLocation) {
            matrixData.assign(alg, inputLocs[govOmegaInLocation], -Dt);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double normal = speedDroopRequest(inputs);
    const double temperature = temperatureRequest(state);
    const double valveRate = valveDerivative(inputs, state);
    if (valveRate == 0.0 &&
        ((state[valveState] >= responseMaximum) || (state[valveState] <= responseMinimum))) {
        matrixData.assign(diff + valveState, diff + valveState, -stateData.cj);
    } else {
        matrixData.assign(diff + valveState,
                          diff + valveState,
                          (-1.0 / effectiveT1) - stateData.cj);
        if (normal <= temperature) {
            matrixData.assignCheckCol(diff + valveState,
                                      inputLocs[govpSetInLocation],
                                      1.0 / effectiveT1);
            if (inputLocs[govOmegaInLocation] != kNullLocation) {
                matrixData.assign(diff + valveState,
                                  inputLocs[govOmegaInLocation],
                                  -1.0 / (R * effectiveT1));
            }
        } else {
            matrixData.assign(diff + valveState, diff + temperatureState, -KT / effectiveT1);
        }
    }
    matrixData.assign(diff + turbineState, diff + valveState, 1.0 / effectiveT2);
    matrixData.assign(diff + turbineState,
                      diff + turbineState,
                      (-1.0 / effectiveT2) - stateData.cj);
    matrixData.assign(diff + temperatureState, diff + turbineState, 1.0 / effectiveT3);
    matrixData.assign(diff + temperatureState,
                      diff + temperatureState,
                      (-1.0 / effectiveT3) - stateData.cj);
}

void GovernorGast::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[diffOffset + valveState] =
        std::clamp(m_state[diffOffset + valveState] +
                       (timeStep * m_dstate_dt[diffOffset + valveState]),
                   responseMinimum,
                   responseMaximum);
    m_state[diffOffset + turbineState] += (timeStep * m_dstate_dt[diffOffset + turbineState]);
    m_state[diffOffset + temperatureState] +=
        (timeStep * m_dstate_dt[diffOffset + temperatureState]);
    m_state[0] = mechanicalPower(inputs, m_state.data() + 1);
    prevTime = time;
}

stringVec GovernorGast::localStateNames() const
{
    return {"pmech", "valve", "turbine", "temperature"};
}

index_t GovernorGast::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode);
    }
    const index_t diff = offsets.getDiffOffset(sMode);
    if ((field == "valve") || (field == "v")) {
        return diff;
    }
    if ((field == "turbine") || (field == "pm_turb")) {
        return (diff == kNullLocation) ? diff : diff + 1;
    }
    if ((field == "temperature") || (field == "temp")) {
        return (diff == kNullLocation) ? diff : diff + 2;
    }
    return kInvalidLocation;
}

void GovernorGast::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorGast::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("GAST parameters must be finite");
    }
    if (param == "r") {
        if (val <= 0.0) {
            throw InvalidParameterValue("GAST R must be positive");
        }
        R = val;
    } else if (param == "at") {
        AT = val;
    } else if ((param == "kt") || (param == "k_t")) {
        KT = val;
    } else if ((param == "dt") || (param == "dturb")) {
        Dt = val;
    } else if ((param == "vmax") || (param == "pmax")) {
        Pmax = val;
    } else if ((param == "vmin") || (param == "pmin")) {
        Pmin = val;
    } else if ((param == "t1") || (param == "t2") || (param == "t3")) {
        if (val < 0.0) {
            throw InvalidParameterValue("GAST time constants must be non-negative");
        }
        Governor::set(param, val, unitType);
    } else {
        Governor::set(param, val, unitType);
    }
}

double GovernorGast::get(std::string_view param, units::unit unitType) const
{
    if (param == "r") {
        return R;
    }
    if (param == "at") {
        return AT;
    }
    if ((param == "kt") || (param == "k_t")) {
        return KT;
    }
    if ((param == "dt") || (param == "dturb")) {
        return Dt;
    }
    if (param == "vmax") {
        return Pmax;
    }
    if (param == "vmin") {
        return Pmin;
    }
    return Governor::get(param, unitType);
}
}  // namespace griddyn::governors
