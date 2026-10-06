/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorIeeeG2.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::governors {
namespace {
    constexpr double minimumTimeConstant = 1e-12;
}

GovernorIeeeG2::GovernorIeeeG2(const std::string& objName): Governor(objName)
{
    K = 20.0;
    T1 = 50.0;
    T2 = 5.0;
    T3 = 1.0;
    T4 = 1.5;
    Pmax = 1.043;
    Pmin = 0.09;
    m_outputSize = 1;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
    opFlags.set(USES_POWER_LIMITS);
}

CoreObject* GovernorIeeeG2::clone(CoreObject* obj) const
{
    auto* governorClone = cloneBase<GovernorIeeeG2, Governor>(this, obj);
    if (governorClone != nullptr) {
        governorClone->T4 = T4;
    }
    return (governorClone != nullptr) ? governorClone : obj;
}

void GovernorIeeeG2::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if ((Pmax < Pmin) || (T1 < 0.0) || (T2 < 0.0) || (T3 < 0.0) || (T4 < 0.0)) {
        throw InvalidParameterValue("IEEEG2 time constants or power limits");
    }

    index_t nextState = 0;
    leadLagState = (T1 > minimumTimeConstant) ? nextState++ : kInvalidLocation;
    simpleLagState = (T3 > minimumTimeConstant) ? nextState++ : kInvalidLocation;
    waterState = (T4 > minimumTimeConstant) ? nextState++ : kInvalidLocation;

    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = nextState;
    local.algRoots = 0;
    local.diffRoots = 0;
    local.jacSize = 20;
    prevTime = time0;
}

void GovernorIeeeG2::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("IEEEG2 initial mechanical power");
    }
    const double initialPower = desiredOutput[0];
    if ((initialPower < Pmin) || !adjustInitialUpperLimit(initialPower, "IEEEG2 initial power")) {
        throw InvalidParameterValue("IEEEG2 initial power outside limits");
    }

    Pset = initialPower;
    double* state = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    if (leadLagState != kInvalidLocation) {
        state[leadLagState] = 0.0;
    }
    if (simpleLagState != kInvalidLocation) {
        state[simpleLagState] = 0.0;
    }
    if (waterState != kInvalidLocation) {
        state[waterState] = initialPower;
    }
    double* algebraic = m_state.data() + offsets.getAlgOffset(cLocalSolverMode);
    algebraic[0] = initialPower;
    fieldSet.resize(2);
    fieldSet[0] = Pset;
    fieldSet[1] = Pset;
    (void)inputs;
}

void GovernorIeeeG2::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}

void GovernorIeeeG2::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "t4") {
        T4 = val;
    } else {
        Governor::set(param, val, unitType);
    }
}

double GovernorIeeeG2::get(std::string_view param, units::unit unitType) const
{
    if (param == "t4") {
        return T4;
    }
    return Governor::get(param, unitType);
}

double GovernorIeeeG2::leadLagOutput(const IOdata& inputs, const double state[]) const
{
    const double speedDeviation = 1.0 - inputs[govOmegaInLocation];
    if (leadLagState == kInvalidLocation) {
        return K * speedDeviation;
    }
    const double leadLagStateValue = state[leadLagState];
    return K * (leadLagStateValue + (T2 / T1) * (speedDeviation - leadLagStateValue));
}

double GovernorIeeeG2::speedLagOutput(const IOdata& inputs, const double state[]) const
{
    return (simpleLagState == kInvalidLocation) ? leadLagOutput(inputs, state) :
                                                   state[simpleLagState];
}

double GovernorIeeeG2::limitedInput(const IOdata& inputs, const double state[]) const
{
    return std::clamp(speedLagOutput(inputs, state) + inputs[govpSetInLocation],
                      static_cast<double>(Pmin),
                      static_cast<double>(Pmax));
}

double GovernorIeeeG2::waterOutput(const IOdata& inputs, const double state[]) const
{
    const double limited = limitedInput(inputs, state);
    if (waterState == kInvalidLocation) {
        return limited;
    }
    // OpenIPSL LeadLag(K=1, T1=-T4, T2=0.5*T4): y=3*x-2*u.
    return state[waterState] + (-2.0) * (limited - state[waterState]);
}

double GovernorIeeeG2::limiterSlope(const IOdata& inputs, const double state[]) const
{
    const double raw = speedLagOutput(inputs, state) + inputs[govpSetInLocation];
    return (raw > Pmin) && (raw < Pmax) ? 1.0 : 0.0;
}

void GovernorIeeeG2::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] = waterOutput(inputs, locations.diffStateLoc) -
            locations.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t stateIndex = 0; stateIndex < locations.diffSize; ++stateIndex) {
            locations.destDiffLoc[stateIndex] -= locations.dstateLoc[stateIndex];
        }
    }
}

void GovernorIeeeG2::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    if (leadLagState != kInvalidLocation) {
        locations.destDiffLoc[leadLagState] =
            (1.0 - inputs[govOmegaInLocation] - state[leadLagState]) / T1;
    }
    if (simpleLagState != kInvalidLocation) {
        locations.destDiffLoc[simpleLagState] =
            (leadLagOutput(inputs, state) - state[simpleLagState]) / T3;
    }
    if (waterState != kInvalidLocation) {
        const double waterTime = 0.5 * T4;
        locations.destDiffLoc[waterState] =
            (limitedInput(inputs, state) - state[waterState]) / waterTime;
    }
}

void GovernorIeeeG2::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    locations.destLoc[0] = waterOutput(inputs, locations.diffStateLoc);
}

void GovernorIeeeG2::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t algOffset = locations.algOffset;
    const index_t diffOffset = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const double leadGain = (leadLagState == kInvalidLocation) ? K : K * (T2 / T1);
    const double leadStateGain = (leadLagState == kInvalidLocation) ? 0.0 : K * (1.0 - T2 / T1);
    const double lagGain = (simpleLagState == kInvalidLocation) ? 1.0 : 0.0;
    const double limitGain = limiterSlope(inputs, state);
    const double waterStateGain = (waterState == kInvalidLocation) ? 0.0 : 3.0;
    const double waterInputGain = (waterState == kInvalidLocation) ? 1.0 : -2.0;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(algOffset, algOffset, -1.0);
        if (waterState != kInvalidLocation) {
            matrixData.assign(algOffset, diffOffset + waterState, waterStateGain);
        } else if (simpleLagState != kInvalidLocation) {
            matrixData.assign(algOffset, diffOffset + simpleLagState, waterInputGain * limitGain);
        }
        if (!isAlgebraicOnly(sMode)) {
            if (simpleLagState != kInvalidLocation) {
                matrixData.assign(algOffset,
                                  diffOffset + simpleLagState,
                                  waterInputGain * limitGain);
            } else if (leadLagState == kInvalidLocation) {
                matrixData.assignCheckCol(algOffset,
                                          inputLocs[govOmegaInLocation],
                                          waterInputGain * limitGain * (-leadGain));
            } else {
                matrixData.assign(algOffset,
                                  diffOffset + leadLagState,
                                  waterInputGain * limitGain * lagGain * leadStateGain);
            }
            matrixData.assignCheckCol(algOffset,
                                      inputLocs[govpSetInLocation],
                                      waterInputGain * limitGain);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    if (leadLagState != kInvalidLocation) {
        matrixData.assign(diffOffset + leadLagState,
                          diffOffset + leadLagState,
                          (-1.0 / T1) - stateData.cj);
        matrixData.assignCheckCol(diffOffset + leadLagState,
                                  inputLocs[govOmegaInLocation],
                                  -1.0 / T1);
    }
    if (simpleLagState != kInvalidLocation) {
        matrixData.assign(diffOffset + simpleLagState,
                          diffOffset + simpleLagState,
                          (-1.0 / T3) - stateData.cj);
        if (leadLagState != kInvalidLocation) {
            matrixData.assign(diffOffset + simpleLagState,
                              diffOffset + leadLagState,
                              leadStateGain / T3);
        }
        matrixData.assignCheckCol(diffOffset + simpleLagState,
                                  inputLocs[govOmegaInLocation],
                                  -leadGain / T3);
    }
    if (waterState != kInvalidLocation) {
        const double waterTime = 0.5 * T4;
        matrixData.assign(diffOffset + waterState,
                          diffOffset + waterState,
                          (-1.0 / waterTime) - stateData.cj);
        if (simpleLagState != kInvalidLocation) {
            matrixData.assign(diffOffset + waterState,
                              diffOffset + simpleLagState,
                              limitGain / waterTime);
        } else if (leadLagState != kInvalidLocation) {
            matrixData.assign(diffOffset + waterState,
                              diffOffset + leadLagState,
                              limitGain * leadStateGain / waterTime);
        }
        matrixData.assignCheckCol(diffOffset + waterState,
                                  inputLocs[govOmegaInLocation],
                                  (simpleLagState == kInvalidLocation) ?
                                      (-limitGain * leadGain / waterTime) :
                                      0.0);
        matrixData.assignCheckCol(diffOffset + waterState,
                                  inputLocs[govpSetInLocation],
                                  limitGain / waterTime);
    }
}

stringVec GovernorIeeeG2::localStateNames() const
{
    stringVec names;
    if (leadLagState != kInvalidLocation) {
        names.emplace_back("lead_lag");
    }
    if (simpleLagState != kInvalidLocation) {
        names.emplace_back("simple_lag");
    }
    if (waterState != kInvalidLocation) {
        names.emplace_back("water_lag");
    }
    return names;
}

index_t GovernorIeeeG2::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech") || (field == "output")) {
        return getOutputLoc(sMode, 0);
    }
    return Governor::findIndex(field, sMode);
}

const std::vector<stringVec>& GovernorIeeeG2::outputNames() const
{
    static const std::vector<stringVec> names{{"pm", "pmech", "output"}};
    return names;
}
}  // namespace griddyn::governors
