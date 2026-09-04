/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorSteamTCSR.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::governors {
namespace {
    constexpr index_t outputState = 0;
    constexpr index_t valveState = 0;
    constexpr index_t filterState = 1;
    constexpr index_t chestState = 2;
    constexpr index_t reheatState = 3;
    constexpr index_t crossoverState = 4;
}  // namespace

GovernorSteamTCSR::GovernorSteamTCSR(const std::string& objName): GovernorSteamNR(objName)
{
    Trh = 1.2;
    Tco = 1.2;
    Fch = 0.2;
    Fip = 0.3;
    Flp = 0.5;
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 5;
    offsets.local().local.jacSize = 18;
}

CoreObject* GovernorSteamTCSR::clone(CoreObject* obj) const
{
    auto* gov = cloneBase<GovernorSteamTCSR, GovernorSteamNR>(this, obj);
    if (gov == nullptr) {
        return obj;
    }
    gov->Trh = Trh;
    gov->Tco = Tco;
    gov->Fch = Fch;
    gov->Fip = Fip;
    gov->Flp = Flp;
    return gov;
}

GovernorSteamTCSR::~GovernorSteamTCSR() = default;

void GovernorSteamTCSR::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(Trh) || !std::isfinite(Tco) || !std::isfinite(Fch) || !std::isfinite(Fip) ||
        !std::isfinite(Flp) || (Trh <= 0.0) || (Tco <= 0.0) || (Fch < 0.0) || (Fip < 0.0) ||
        (Flp < 0.0) || ((Fch + Fip + Flp) <= 0.0)) {
        throw InvalidParameterValue("SteamTCSR reheat parameters");
    }
    GovernorSteamNR::dynObjectInitializeA(time0, flags);
}

void GovernorSteamTCSR::dynObjectInitializeB(const IOdata& /*inputs*/,
                                             const IOdata& desiredOutput,
                                             IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[outputState])) {
        throw InvalidParameterValue("SteamTCSR governor initial output");
    }
    const double power = desiredOutput[outputState];
    const double stagePower = power / (Fch + Fip + Flp);
    if ((stagePower < Pmin) || (stagePower > Pmax)) {
        throw InvalidParameterValue("SteamTCSR initial valve outside limits");
    }
    const auto algOffset = offsets.getAlgOffset(cLocalSolverMode);
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[algOffset + outputState] = power;
    m_state[diffOffset + valveState] = stagePower;
    m_state[diffOffset + filterState] = 0.0;
    m_state[diffOffset + chestState] = stagePower;
    m_state[diffOffset + reheatState] = stagePower;
    m_state[diffOffset + crossoverState] = stagePower;
    Pset = stagePower;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = stagePower;
}

void GovernorSteamTCSR::residual(const IOdata& inputs,
                                 const StateData& stateData,
                                 double resid[],
                                 const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto* state = locations.diffStateLoc;
        locations.destLoc[outputState] = Fch * state[chestState] + Fip * state[reheatState] +
            Flp * state[crossoverState] - locations.algStateLoc[outputState];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void GovernorSteamTCSR::derivative(const IOdata& inputs,
                                   const StateData& stateData,
                                   double deriv[],
                                   const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const auto* state = locations.diffStateLoc;
    auto* dst = locations.destDiffLoc;
    const double speedDeviation = inputs[govOmegaInLocation] - Wref;
    const double unrestrictedRate = (inputs[govpSetInLocation] - state[valveState] -
                                     K * state[filterState] - K * T2 * speedDeviation / T1) /
        T3;
    dst[valveState] = opFlags[POWER_LIMITED] ? 0.0 : std::clamp(unrestrictedRate, Pdown, Pup);
    dst[filterState] = (-state[filterState] + (1.0 - T2 / T1) * speedDeviation) / T1;
    dst[chestState] = (state[valveState] - state[chestState]) / Tch;
    dst[reheatState] = (state[chestState] - state[reheatState]) / Trh;
    dst[crossoverState] = (state[reheatState] - state[crossoverState]) / Tco;
}

void GovernorSteamTCSR::jacobianElements(const IOdata& inputs,
                                         const StateData& stateData,
                                         MatrixData<double>& matrixData,
                                         const IOlocs& inputLocs,
                                         const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto diff = locations.diffOffset;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(locations.algOffset + outputState,
                          locations.algOffset + outputState,
                          -1.0);
        matrixData.assign(locations.algOffset + outputState, diff + chestState, Fch);
        matrixData.assign(locations.algOffset + outputState, diff + reheatState, Fip);
        matrixData.assign(locations.algOffset + outputState, diff + crossoverState, Flp);
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto* state = locations.diffStateLoc;
    const double speedDeviation = inputs[govOmegaInLocation] - Wref;
    const double unrestrictedRate = (inputs[govpSetInLocation] - state[valveState] -
                                     K * state[filterState] - K * T2 * speedDeviation / T1) /
        T3;
    const bool rateLimited =
        opFlags[POWER_LIMITED] || (unrestrictedRate <= Pdown) || (unrestrictedRate >= Pup);
    matrixData.assign(diff + valveState,
                      diff + valveState,
                      rateLimited ? -stateData.cj : -1.0 / T3 - stateData.cj);
    if (!rateLimited) {
        matrixData.assign(diff + valveState, diff + filterState, -K / T3);
        matrixData.assignCheckCol(diff + valveState, inputLocs[govpSetInLocation], 1.0 / T3);
        matrixData.assignCheckCol(diff + valveState,
                                  inputLocs[govOmegaInLocation],
                                  -K * T2 / (T1 * T3));
    }
    matrixData.assign(diff + filterState, diff + filterState, -1.0 / T1 - stateData.cj);
    matrixData.assignCheckCol(diff + filterState,
                              inputLocs[govOmegaInLocation],
                              (T1 - T2) / (T1 * T1));
    matrixData.assign(diff + chestState, diff + valveState, 1.0 / Tch);
    matrixData.assign(diff + chestState, diff + chestState, -1.0 / Tch - stateData.cj);
    matrixData.assign(diff + reheatState, diff + chestState, 1.0 / Trh);
    matrixData.assign(diff + reheatState, diff + reheatState, -1.0 / Trh - stateData.cj);
    matrixData.assign(diff + crossoverState, diff + reheatState, 1.0 / Tco);
    matrixData.assign(diff + crossoverState, diff + crossoverState, -1.0 / Tco - stateData.cj);
}

index_t GovernorSteamTCSR::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode) + outputState;
    }
    if ((field == "valve") || (field == "integrator")) {
        return offsets.getDiffOffset(sMode) + valveState;
    }
    if ((field == "filter") || (field == "x")) {
        return offsets.getDiffOffset(sMode) + filterState;
    }
    if ((field == "steamchest") || (field == "chest")) {
        return offsets.getDiffOffset(sMode) + chestState;
    }
    if ((field == "reheater") || (field == "reheat")) {
        return offsets.getDiffOffset(sMode) + reheatState;
    }
    if ((field == "crossover") || (field == "crossover_pipe")) {
        return offsets.getDiffOffset(sMode) + crossoverState;
    }
    return kInvalidLocation;
}

void GovernorSteamTCSR::set(std::string_view param, std::string_view val)
{
    GovernorSteamNR::set(param, val);
}

void GovernorSteamTCSR::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "trh") || (param == "t5")) {
        Trh = val;
    } else if ((param == "tco") || (param == "t6")) {
        Tco = val;
    } else if ((param == "fch") || (param == "fhp")) {
        Fch = val;
    } else if ((param == "fip") || (param == "frh")) {
        Fip = val;
    } else if ((param == "flp") || (param == "fco")) {
        Flp = val;
    } else {
        GovernorSteamNR::set(param, val, unitType);
    }
}

double GovernorSteamTCSR::get(std::string_view param, units::unit unitType) const
{
    if ((param == "trh") || (param == "t5")) {
        return Trh;
    }
    if ((param == "tco") || (param == "t6")) {
        return Tco;
    }
    if ((param == "fch") || (param == "fhp")) {
        return Fch;
    }
    if ((param == "fip") || (param == "frh")) {
        return Fip;
    }
    if ((param == "flp") || (param == "fco")) {
        return Flp;
    }
    return GovernorSteamNR::get(param, unitType);
}

}  // namespace griddyn::governors
