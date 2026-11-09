/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorTG2.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::governors {
GovernorTG2::GovernorTG2(const std::string& objName): Governor(objName)
{
    K = 20.0;
    T1 = 0.2;
    T2 = 10.0;
    Pmax = 999.0;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}
const ModelDescription& GovernorTG2::getModelDescription() const
{
    static const auto description = ModelDescription::define<GovernorTG2>(
        "GovernorTG2",
        &Governor::getModelDescription(),
        {parameter({"dbl"}, &GovernorTG2::dbL, units::one, "Lower deadband limit"),
         parameter({"dbu"}, &GovernorTG2::dbU, units::one, "Upper deadband limit"),
         parameter({"dbc"}, &GovernorTG2::dbC, units::one, "Deadband center"),
         flag({"deadbandenabled", "deadband"},
              &GovernorTG2::deadbandEnabled,
              "Enable speed deadband"),
         flag({"hardlimit"}, &GovernorTG2::hardLimitEnabled, "Enable hard output limit"),
         state({"pmech", "pm"}, units::puMW, 0, "Mechanical power"),
         state({"leadlag", "ll"}, units::one, 1, "Lead-lag state")});
    return description;
}
CoreObject* GovernorTG2::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorTG2, Governor>(this, obj);
    if (out != nullptr) {
        out->dbL = dbL;
        out->dbU = dbU;
        out->dbC = dbC;
        out->deadbandEnabled = deadbandEnabled;
        out->hardLimitEnabled = hardLimitEnabled;
    }
    return out;
}
void GovernorTG2::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(K) || K <= 0.0 || !std::isfinite(T1) || T1 < 0.0 || !std::isfinite(T2) ||
        T2 <= 0.0 || !std::isfinite(Pmax) || !std::isfinite(Pmin) || Pmax < Pmin ||
        !std::isfinite(dbL) || !std::isfinite(dbU) || !std::isfinite(dbC) || dbL > dbU) {
        throw InvalidParameterValue("TG2 parameters");
    }
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 1;
    local.algRoots = 0;
    local.diffRoots = 0;
    local.jacSize = 8;
    prevTime = time0;
}
void GovernorTG2::dynObjectInitializeB([[maybe_unused]] const IOdata& inputs,
                                       const IOdata& desired,
                                       IOdata& fieldSet)
{
    const double originalUpperLimit = Pmax;
    if (desired.empty() || !std::isfinite(desired[0]) ||
        (hardLimitEnabled &&
         (desired[0] < Pmin || !adjustInitialUpperLimit(desired[0], "TG2 initial power")))) {
        throw InvalidParameterValue("TG2 initial output");
    }
    Pset = desired[0];
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = Pset;
    m_state[offsets.getDiffOffset(cLocalSolverMode)] = 0.0;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset;
    if (hardLimitEnabled) {
        const double correction = std::max(0.0, Pmax - originalUpperLimit);
        const double margin = std::max(0.2 * correction, 1e-4);
        if (desired[0] >= Pmax) {
            initialSetpointOverdrive = margin;
        } else if (desired[0] <= Pmin) {
            initialSetpointOverdrive = -margin;
        }
    }
}
double GovernorTG2::speedInput(const IOdata& inputs) const
{
    const double speedDeviation = 1.0 - inputs[govOmegaInLocation];
    if (!deadbandEnabled) {
        return speedDeviation;
    }
    // ANDES DeadBandRT currently has no working return-direction flags:
    // outside the band it passes the speed deviation, inside it gives zero.
    if (speedDeviation < dbL || speedDeviation > dbU) {
        return speedDeviation;
    }
    return 0.0;
}
double GovernorTG2::speedSlope(const IOdata& inputs) const
{
    const double speedDeviation = 1.0 - inputs[govOmegaInLocation];
    return (!deadbandEnabled || speedDeviation < dbL || speedDeviation > dbU) ? -1.0 : 0.0;
}
double GovernorTG2::output(const IOdata& inputs, double state) const
{
    const double raw =
        effectivePowerSetpoint(inputs) + state + ((T1 / T2) * ((K * speedInput(inputs)) - state));
    return hardLimitEnabled ?
        std::clamp(raw, static_cast<double>(Pmin), static_cast<double>(Pmax)) :
        raw;
}
double GovernorTG2::outputSlope(const IOdata& inputs, double state) const
{
    const double raw =
        effectivePowerSetpoint(inputs) + state + ((T1 / T2) * ((K * speedInput(inputs)) - state));
    return (!hardLimitEnabled || (raw > Pmin && raw < Pmax)) ? 1.0 : 0.0;
}
void GovernorTG2::derivative(const IOdata& inputs,
                             const StateData& stateData,
                             double deriv[],
                             const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    loc.destDiffLoc[0] = ((K * speedInput(inputs)) - loc.diffStateLoc[0]) / T2;
}
void GovernorTG2::residual(const IOdata& inputs,
                           const StateData& stateData,
                           double resid[],
                           const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = output(inputs, loc.diffStateLoc[0]) - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        loc.destDiffLoc[0] -= loc.dstateLoc[0];
    }
}
void GovernorTG2::algebraicUpdate(const IOdata& inputs,
                                  const StateData& stateData,
                                  double update[],
                                  const SolverMode& sMode,
                                  [[maybe_unused]] double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = output(inputs, loc.diffStateLoc[0]);
}
void GovernorTG2::jacobianElements(const IOdata& inputs,
                                   const StateData& stateData,
                                   MatrixData<double>& matrixData,
                                   const IOlocs& inputLocs,
                                   const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double active = outputSlope(inputs, loc.diffStateLoc[0]);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset, loc.diffOffset, active * (1.0 - (T1 / T2)));
        }
        matrixData.assignCheckCol(loc.algOffset,
                                  inputLocs[govOmegaInLocation],
                                  active * (T1 / T2) * K * speedSlope(inputs));
        matrixData.assignCheckCol(loc.algOffset, inputLocs[govpSetInLocation], active);
    }
    if (hasDifferential(sMode)) {
        matrixData.assign(loc.diffOffset, loc.diffOffset, (-1.0 / T2) - stateData.cj);
        matrixData.assignCheckCol(loc.diffOffset,
                                  inputLocs[govOmegaInLocation],
                                  K * speedSlope(inputs) / T2);
    }
}
void GovernorTG2::timestep(CoreTime time,
                           const IOdata& inputs,
                           [[maybe_unused]] const SolverMode& sMode)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[diffOffset] += (time - prevTime) * m_dstate_dt[diffOffset];
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = output(inputs, m_state[diffOffset]);
    prevTime = time;
}
void GovernorTG2::set(std::string_view param, double value, units::unit unitType)
{
    if (isSetHandled(GovernorTG2::getModelDescription().trySet(*this, param, value, unitType))) {
        return;
    }
    Governor::set(param, value, unitType);
}
double GovernorTG2::get(std::string_view param, units::unit unitType) const
{
    if (const auto result = GovernorTG2::getModelDescription().tryGet(*this, param, unitType)) {
        return *result;
    }
    return Governor::get(param, unitType);
}
}  // namespace griddyn::governors
