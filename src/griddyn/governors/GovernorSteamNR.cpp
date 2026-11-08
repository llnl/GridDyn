/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorSteamNR.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
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
}  // namespace

GovernorSteamNR::GovernorSteamNR(const std::string& objName): GovernorIeeeSimple(objName)
{
    // IEEE 1973 non-reheat steam-turbine governor realization.
    K = 10.0;
    T1 = 0.5;
    T2 = 0.1;
    T3 = 1.0;
    Tch = 1.2;
    Pup = 1.2;
    Pdown = 0.0;
    Pmax = 10.0;
    Pmin = 0.0;
    Wref = 1.0;
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 10;
}

const ModelDescription& GovernorSteamNR::getModelDescription() const
{
    static const auto description = ModelDescription::define<GovernorSteamNR>(
        "GovernorSteamNR",
        &GovernorIeeeSimple::getModelDescription(),
        {parameter({"tch", "t4"},
                   &GovernorSteamNR::Tch,
                   units::second,
                   "Steam-chest time constant",
                   parameterValidators::positive),
         state("pmech", units::puMW, "Mechanical power"),
         state("valve", units::one, "Valve state"),
         state("filter", units::one, "Speed filter"),
         state("steamchest", units::one, "Steam-chest state")});
    return description;
}

CoreObject* GovernorSteamNR::clone(CoreObject* obj) const
{
    auto* gov = cloneBase<GovernorSteamNR, GovernorIeeeSimple>(this, obj);
    if (gov == nullptr) {
        return obj;
    }
    gov->Tch = Tch;
    return gov;
}

GovernorSteamNR::~GovernorSteamNR() = default;

void GovernorSteamNR::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(K) || !std::isfinite(T1) || !std::isfinite(T2) || !std::isfinite(T3) ||
        !std::isfinite(Tch) || !std::isfinite(Pup) || !std::isfinite(Pdown) ||
        !std::isfinite(Pmax) || !std::isfinite(Pmin) || (T1 <= 0.0) || (T3 <= 0.0) ||
        (Tch <= 0.0) || (Pup < Pdown) || (Pmax < Pmin)) {
        throw InvalidParameterValue("SteamNR governor parameters");
    }
    GovernorIeeeSimple::dynObjectInitializeA(time0, flags);
}

void GovernorSteamNR::dynObjectInitializeB(const IOdata& /*inputs*/,
                                           const IOdata& desiredOutput,
                                           IOdata& fieldSet)
{
    const double originalUpperLimit = Pmax;
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[outputState])) {
        throw InvalidParameterValue("SteamNR governor initial output");
    }
    const double power = desiredOutput[outputState];
    if ((power < Pmin) || !adjustInitialUpperLimit(power, "SteamNR initial valve")) {
        throw InvalidParameterValue("SteamNR initial valve outside limits");
    }
    const auto algOffset = offsets.getAlgOffset(cLocalSolverMode);
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[algOffset + outputState] = power;
    m_state[diffOffset + valveState] = power;
    m_state[diffOffset + filterState] = 0.0;
    m_state[diffOffset + chestState] = power;
    Pset = power;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = power;
    if (opFlags[USES_POWER_LIMITS]) {
        const double correction = std::max(0.0, Pmax - originalUpperLimit);
        const double margin = std::max(0.2 * correction, 1e-4 * T3);
        if (power >= Pmax) {
            initialSetpointOverdrive = margin;
            opFlags.set(POWER_LIMIT_HIGH);
            opFlags.set(POWER_LIMITED);
        } else if (power <= Pmin) {
            initialSetpointOverdrive = -margin;
            opFlags.reset(POWER_LIMIT_HIGH);
            opFlags.set(POWER_LIMITED);
        }
    }
}

void GovernorSteamNR::residual(const IOdata& inputs,
                               const StateData& stateData,
                               double resid[],
                               const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[outputState] =
            locations.diffStateLoc[chestState] - locations.algStateLoc[outputState];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void GovernorSteamNR::derivative(const IOdata& inputs,
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
    const double unrestrictedRate = (effectivePowerSetpoint(inputs) - state[valveState] -
                                     (K * state[filterState]) - ((K * T2 * speedDeviation) / T1)) /
        T3;
    dst[valveState] = opFlags[POWER_LIMITED] ? 0.0 : std::clamp(unrestrictedRate, Pdown, Pup);
    dst[filterState] = (-state[filterState] + ((1.0 - (T2 / T1)) * speedDeviation)) / T1;
    dst[chestState] = (state[valveState] - state[chestState]) / Tch;
}

void GovernorSteamNR::jacobianElements(const IOdata& inputs,
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
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        matrixData.assign(locations.algOffset + outputState, diff + chestState, 1.0);
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto* state = locations.diffStateLoc;
    const double speedDeviation = inputs[govOmegaInLocation] - Wref;
    const double unrestrictedRate = (effectivePowerSetpoint(inputs) - state[valveState] -
                                     (K * state[filterState]) - ((K * T2 * speedDeviation) / T1)) /
        T3;
    const bool rateLimited =
        opFlags[POWER_LIMITED] || (unrestrictedRate <= Pdown) || (unrestrictedRate >= Pup);
    matrixData.assign(diff + valveState,
                      diff + valveState,
                      rateLimited ? -stateData.cj : ((-1.0 / T3) - stateData.cj));
    if (!rateLimited) {
        matrixData.assign(diff + valveState, diff + filterState, -K / T3);
        matrixData.assignCheckCol(diff + valveState, inputLocs[govpSetInLocation], 1.0 / T3);
        matrixData.assignCheckCol(diff + valveState,
                                  inputLocs[govOmegaInLocation],
                                  -K * T2 / (T1 * T3));
    }
    matrixData.assign(diff + filterState, diff + filterState, (-1.0 / T1) - stateData.cj);
    matrixData.assignCheckCol(diff + filterState,
                              inputLocs[govOmegaInLocation],
                              (T1 - T2) / (T1 * T1));
    matrixData.assign(diff + chestState, diff + valveState, 1.0 / Tch);
    matrixData.assign(diff + chestState, diff + chestState, (-1.0 / Tch) - stateData.cj);
}

index_t GovernorSteamNR::findIndex(std::string_view field, const SolverMode& sMode) const
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
    return kInvalidLocation;
}

void GovernorSteamNR::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(GovernorSteamNR::getModelDescription().trySet(*this, param, val, unitType))) {
        return;
    }
    GovernorIeeeSimple::set(param, val, unitType);
}

}  // namespace griddyn::governors
