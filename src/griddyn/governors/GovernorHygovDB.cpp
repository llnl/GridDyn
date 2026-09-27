/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorHygovDB.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::governors {
GovernorHygovDB::GovernorHygovDB(const std::string& name): GovernorHygov(name)
{
    K = 20.0;
    temporaryDroop = 1.0;
    Pmax = 1.0;
    Pmin = 0.0;
    VELM = 0.3;
    Tf = 0.05;
    Tr = 1.0;
    Tg = 0.05;
    Tw = 1.0;
    At = 1.0;
    Dturb = 0.0;
    qNL = 0.1;
}

CoreObject* GovernorHygovDB::clone(CoreObject* obj) const
{
    auto* result = cloneBase<GovernorHygovDB, GovernorHygov>(this, obj);
    if (result != nullptr) {
        result->dbL = dbL;
        result->dbU = dbU;
    }
    return result;
}

void GovernorHygovDB::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(dbL) || !std::isfinite(dbU) || dbL > dbU) {
        throw InvalidParameterValue("HYGOVDB deadband limits");
    }
    GovernorHygov::dynObjectInitializeA(time0, flags);
    offsets.local().local.algRoots = 0;
    offsets.local().local.jacSize = 20;
}

void GovernorHygovDB::set(std::string_view param, double value, units::unit unitType)
{
    if (param == "dbl") {
        dbL = value;
    } else if (param == "dbu") {
        dbU = value;
    } else {
        GovernorHygov::set(param, value, unitType);
    }
}

double GovernorHygovDB::get(std::string_view param, units::unit unitType) const
{
    if (param == "dbl") {
        return dbL;
    }
    if (param == "dbu") {
        return dbU;
    }
    return GovernorHygov::get(param, unitType);
}

double GovernorHygovDB::governorSpeedDeviation(const IOdata& inputs) const
{
    const double speedDeviation = inputs[govOmegaInLocation] - 1.0;
    if (speedDeviation < dbL) {
        return speedDeviation - dbL;
    }
    if (speedDeviation > dbU) {
        return speedDeviation - dbU;
    }
    return 0.0;
}

double GovernorHygovDB::governorSpeedSlope(const IOdata& inputs) const
{
    const double speedDeviation = inputs[govOmegaInLocation] - 1.0;
    return (speedDeviation < dbL || speedDeviation > dbU) ? 1.0 : 0.0;
}

double GovernorHygovDB::gateRate(const double* state) const
{
    const double rate = std::clamp(state[0], -static_cast<double>(VELM), static_cast<double>(VELM));
    if ((state[1] >= Pmax && rate > 0.0) || (state[1] <= Pmin && rate < 0.0)) {
        return 0.0;
    }
    return rate;
}

double GovernorHygovDB::mechanicalPower(const IOdata& inputs, const double* state) const
{
    const double gate = std::abs(state[2]) >= 1e-8 ? state[2] : std::copysign(1e-8, state[2]);
    const double head = (state[3] / gate) * (state[3] / gate);
    return (At * head * (state[3] - qNL)) - (Dturb * state[2] * (inputs[govOmegaInLocation] - 1.0));
}

void GovernorHygovDB::derivative(const IOdata& inputs,
                                 const StateData& stateData,
                                 double deriv[],
                                 const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    const double droopGain = (state[0] / temporaryDroop) + state[1];
    const double gate = std::abs(state[2]) >= 1e-8 ? state[2] : std::copysign(1e-8, state[2]);
    const double head = (state[3] / gate) * (state[3] / gate);
    loc.destDiffLoc[0] = (Pset - governorSpeedDeviation(inputs) - (droopGain / K) - state[0]) / Tf;
    loc.destDiffLoc[1] = gateRate(state);
    loc.destDiffLoc[2] = (droopGain - state[2]) / Tg;
    loc.destDiffLoc[3] = (h0 - head) / Tw;
}

void GovernorHygovDB::residual(const IOdata& inputs,
                               const StateData& stateData,
                               double resid[],
                               const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = mechanicalPower(inputs, loc.diffStateLoc) - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
            loc.destDiffLoc[stateIndex] -= loc.dstateLoc[stateIndex];
        }
    }
}

void GovernorHygovDB::algebraicUpdate(const IOdata& inputs,
                                      const StateData& stateData,
                                      double update[],
                                      const SolverMode& sMode,
                                      [[maybe_unused]] double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = mechanicalPower(inputs, loc.diffStateLoc);
}

void GovernorHygovDB::jacobianElements(const IOdata& inputs,
                                       const StateData& stateData,
                                       MatrixData<double>& matrixData,
                                       const IOlocs& inputLocs,
                                       const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double* state = loc.diffStateLoc;
    const double gate = std::abs(state[2]) >= 1e-8 ? state[2] : std::copysign(1e-8, state[2]);
    const double head = (state[3] / gate) * (state[3] / gate);
    const double dhdg = std::abs(state[2]) >= 1e-8 ? -2.0 * head / gate : 0.0;
    const double dhdq = 2.0 * state[3] / (gate * gate);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset,
                       loc.diffOffset + 2,
                       (At * dhdg * (state[3] - qNL)) -
                           (Dturb * (inputs[govOmegaInLocation] - 1.0)));
            matrixData.assign(loc.algOffset,
                              loc.diffOffset + 3,
                              At * ((dhdq * (state[3] - qNL)) + head));
        }
        matrixData.assignCheckCol(loc.algOffset, inputLocs[govOmegaInLocation], -Dturb * state[2]);
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(loc.diffOffset,
               loc.diffOffset,
               (-((1.0 + (1.0 / (K * temporaryDroop))) / Tf)) - stateData.cj);
    matrixData.assign(loc.diffOffset, loc.diffOffset + 1, -1.0 / (K * Tf));
    matrixData.assignCheckCol(
        loc.diffOffset, inputLocs[govOmegaInLocation], -governorSpeedSlope(inputs) / Tf);
    const double raw = state[0];
    const double rate = std::clamp(raw, -static_cast<double>(VELM), static_cast<double>(VELM));
    const bool positionLimited =
        (state[1] >= Pmax && rate > 0.0) || (state[1] <= Pmin && rate < 0.0);
    if (!positionLimited && raw > -VELM && raw < VELM) {
        matrixData.assign(loc.diffOffset + 1, loc.diffOffset, 1.0);
    }
    matrixData.assign(loc.diffOffset + 1, loc.diffOffset + 1, -stateData.cj);
    matrixData.assign(loc.diffOffset + 2, loc.diffOffset, 1.0 / (temporaryDroop * Tg));
    matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 1, 1.0 / Tg);
    matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 2, (-1.0 / Tg) - stateData.cj);
    matrixData.assign(loc.diffOffset + 3, loc.diffOffset + 2, -dhdg / Tw);
    matrixData.assign(loc.diffOffset + 3, loc.diffOffset + 3, (-dhdq / Tw) - stateData.cj);
}

void GovernorHygovDB::timestep(CoreTime time,
                               const IOdata& inputs,
                               [[maybe_unused]] const SolverMode& sMode)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        m_state[diffOffset + stateIndex] += timeStep * m_dstate_dt[diffOffset + stateIndex];
    }
    m_state[diffOffset + 1] = std::clamp(
        m_state[diffOffset + 1], static_cast<double>(Pmin), static_cast<double>(Pmax));
    m_state[offsets.getAlgOffset(cLocalSolverMode)] =
        mechanicalPower(inputs, m_state.data() + diffOffset);
    prevTime = time;
}

void GovernorHygovDB::rootTest([[maybe_unused]] const IOdata& inputs,
                               [[maybe_unused]] const StateData& stateData,
                               [[maybe_unused]] double roots[],
                               [[maybe_unused]] const SolverMode& sMode)
{
}
void GovernorHygovDB::rootTrigger([[maybe_unused]] CoreTime time,
                                  [[maybe_unused]] const IOdata& inputs,
                                  [[maybe_unused]] const std::vector<int>& rootMask,
                                  [[maybe_unused]] const SolverMode& sMode)
{
}
ChangeCode GovernorHygovDB::rootCheck([[maybe_unused]] const IOdata& inputs,
                                      [[maybe_unused]] const StateData& stateData,
                                      [[maybe_unused]] const SolverMode& sMode,
                                      [[maybe_unused]] CheckLevel level)
{
    return ChangeCode::NO_CHANGE;
}
}  // namespace griddyn::governors
