/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorTgov1Variants.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::governors {
GovernorTgov1Variant::GovernorTgov1Variant(bool deadband,
                                           bool normalizedReference,
                                           const std::string& name):
    Governor(name), useDeadband(deadband), normalized(normalizedReference)
{
    T1 = 0.1;
    T2 = 0.2;
    T3 = 10.0;
    Pmax = 1.2;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}

GovernorTgov1Variant::GovernorTgov1Variant(const std::string& name):
    GovernorTgov1Variant(false, false, name)
{
}

CoreObject* GovernorTgov1Variant::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorTgov1Variant, Governor>(this, obj);
    if (out != nullptr) {
        out->useDeadband = useDeadband;
        out->normalized = normalized;
        out->dbL = dbL;
        out->dbU = dbU;
        out->Dt = Dt;
        out->paux = paux;
        out->referenceOffset = referenceOffset;
    }
    return out;
}

GovernorTgov1DB::GovernorTgov1DB(const std::string& name): GovernorTgov1Variant(true, false, name)
{
}
GovernorTgov1N::GovernorTgov1N(const std::string& name): GovernorTgov1Variant(false, true, name) {}
GovernorTgov1NDB::GovernorTgov1NDB(const std::string& name): GovernorTgov1Variant(true, true, name)
{
}
CoreObject* GovernorTgov1DB::clone(CoreObject* obj) const
{
    return cloneBase<GovernorTgov1DB, GovernorTgov1Variant>(this, obj);
}
CoreObject* GovernorTgov1N::clone(CoreObject* obj) const
{
    return cloneBase<GovernorTgov1N, GovernorTgov1Variant>(this, obj);
}
CoreObject* GovernorTgov1NDB::clone(CoreObject* obj) const
{
    return cloneBase<GovernorTgov1NDB, GovernorTgov1Variant>(this, obj);
}

void GovernorTgov1Variant::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(K) || K <= 0.0 || !std::isfinite(T1) || T1 <= 0.0 || !std::isfinite(T2) ||
        !std::isfinite(T3) || T3 < 0.0 || !std::isfinite(Pmax) || !std::isfinite(Pmin) ||
        Pmax < Pmin || !std::isfinite(Dt) || !std::isfinite(paux) || !std::isfinite(dbL) ||
        !std::isfinite(dbU) || dbL > dbU) {
        throw InvalidParameterValue("ANDES TGOV1 variant parameters");
    }
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 2;
    local.algRoots = 0;
    local.diffRoots = 0;
    local.jacSize = 12;
    prevTime = time0;
}

void GovernorTgov1Variant::dynObjectInitializeB(const IOdata& /*inputs*/,
                                                const IOdata& desiredOutput,
                                                IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0]) || desiredOutput[0] < Pmin ||
        !adjustInitialUpperLimit(desiredOutput[0], "TGOV1 valve")) {
        throw InvalidParameterValue("ANDES TGOV1 initial valve outside limits");
    }
    const double valve = desiredOutput[0];
    const auto algOffset = offsets.getAlgOffset(cLocalSolverMode);
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[algOffset] = valve;
    m_state[diffOffset] = valve;
    m_state[diffOffset + 1] = valve;
    fieldSet.resize(2);
    // Pset input is the effective valve reference. In ANDES the unnormalized
    // model stores R*Pset and divides its sum by R; N stores Pset directly.
    referenceOffset = -(normalized ? paux : K * paux);
    fieldSet[govpSetInLocation] = valve;
}

double GovernorTgov1Variant::speedSignal(const IOdata& inputs) const
{
    const double speedDeviation = inputs[govOmegaInLocation] - 1.0;
    if (!useDeadband) {
        return speedDeviation;
    }
    if (speedDeviation < dbL) {
        return speedDeviation - dbL;
    }
    if (speedDeviation > dbU) {
        return speedDeviation - dbU;
    }
    return 0.0;
}
double GovernorTgov1Variant::speedSlope(const IOdata& inputs) const
{
    const double speedDeviation = inputs[govOmegaInLocation] - 1.0;
    return (!useDeadband || speedDeviation < dbL || speedDeviation > dbU) ? 1.0 : 0.0;
}
double GovernorTgov1Variant::valveCommand(const IOdata& inputs) const
{
    return inputs[govpSetInLocation] + referenceOffset + (normalized ? paux : K * paux) -
        (K * speedSignal(inputs));
}
double GovernorTgov1Variant::valveRate(const IOdata& inputs, const double* state) const
{
    const double raw = (valveCommand(inputs) - state[0]) / T1;
    if ((state[0] >= Pmax && raw > 0.0) || (state[0] <= Pmin && raw < 0.0)) {
        return 0.0;
    }
    return raw;
}
double GovernorTgov1Variant::output(const IOdata& inputs, const double* state) const
{
    return (T3 > 0.0 ? state[1] + ((T2 / T3) * (state[0] - state[1])) : state[0]) -
        (Dt * speedSignal(inputs));
}

void GovernorTgov1Variant::derivative(const IOdata& inputs,
                                      const StateData& stateData,
                                      double deriv[],
                                      const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    loc.destDiffLoc[0] = valveRate(inputs, loc.diffStateLoc);
    loc.destDiffLoc[1] = (T3 > 0.0) ? (loc.diffStateLoc[0] - loc.diffStateLoc[1]) / T3 : 0.0;
}
void GovernorTgov1Variant::residual(const IOdata& inputs,
                                    const StateData& stateData,
                                    double resid[],
                                    const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = output(inputs, loc.diffStateLoc) - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        loc.destDiffLoc[0] -= loc.dstateLoc[0];
        loc.destDiffLoc[1] -= loc.dstateLoc[1];
    }
}
void GovernorTgov1Variant::algebraicUpdate(const IOdata& inputs,
                                           const StateData& stateData,
                                           double update[],
                                           const SolverMode& sMode,
                                           [[maybe_unused]] double alpha)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = output(inputs, loc.diffStateLoc);
}
void GovernorTgov1Variant::jacobianElements(const IOdata& inputs,
                                            const StateData& stateData,
                                            MatrixData<double>& matrixData,
                                            const IOlocs& inputLocs,
                                            const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double outputLeadFraction = (T3 > 0.0) ? T2 / T3 : 1.0;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset, loc.diffOffset, outputLeadFraction);
            if (T3 > 0.0) {
                matrixData.assign(loc.algOffset, loc.diffOffset + 1, 1.0 - outputLeadFraction);
            }
        }
        matrixData.assignCheckCol(loc.algOffset,
                                  inputLocs[govOmegaInLocation],
                                  -Dt * speedSlope(inputs));
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double raw = (valveCommand(inputs) - loc.diffStateLoc[0]) / T1;
    const bool limited =
        (loc.diffStateLoc[0] >= Pmax && raw > 0.0) || (loc.diffStateLoc[0] <= Pmin && raw < 0.0);
    matrixData.assign(loc.diffOffset, loc.diffOffset, (limited ? 0.0 : -1.0 / T1) - stateData.cj);
    if (!limited) {
        matrixData.assignCheckCol(loc.diffOffset,
                                  inputLocs[govOmegaInLocation],
                                  -K * speedSlope(inputs) / T1);
        matrixData.assignCheckCol(loc.diffOffset, inputLocs[govpSetInLocation], 1.0 / T1);
    }
    if (T3 > 0.0) {
        matrixData.assign(loc.diffOffset + 1, loc.diffOffset, 1.0 / T3);
    }
    matrixData.assign(loc.diffOffset + 1,
                      loc.diffOffset + 1,
                      (T3 > 0.0 ? -1.0 / T3 : 0.0) - stateData.cj);
}
void GovernorTgov1Variant::timestep(CoreTime time,
                                    const IOdata& inputs,
                                    [[maybe_unused]] const SolverMode& sMode)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[diffOffset] = std::clamp(m_state[diffOffset] + (timeStep * m_dstate_dt[diffOffset]),
                                     static_cast<double>(Pmin),
                                     static_cast<double>(Pmax));
    m_state[diffOffset + 1] += timeStep * m_dstate_dt[diffOffset + 1];
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = output(inputs, m_state.data() + diffOffset);
    prevTime = time;
}
index_t GovernorTgov1Variant::findIndex(std::string_view field, const SolverMode& mode) const
{
    if (field == "pm" || field == "pmech") {
        return offsets.getAlgOffset(mode);
    }
    if (field == "valve") {
        return offsets.getDiffOffset(mode);
    }
    if (field == "turbine") {
        return offsets.getDiffOffset(mode) + 1;
    }
    return kInvalidLocation;
}
void GovernorTgov1Variant::set(std::string_view param, double value, units::unit unitType)
{
    if (!std::isfinite(value)) {
        throw InvalidParameterValue("ANDES TGOV1 parameter must be finite");
    }
    if (param == "dbl") {
        dbL = value;
    } else if (param == "dbu") {
        dbU = value;
    } else if (param == "dt") {
        Dt = value;
    } else if (param == "paux") {
        paux = value;
    } else {
        Governor::set(param, value, unitType);
    }
}
double GovernorTgov1Variant::get(std::string_view param, units::unit unitType) const
{
    if (param == "dbl") {
        return dbL;
    }
    if (param == "dbu") {
        return dbU;
    }
    if (param == "dt") {
        return Dt;
    }
    if (param == "paux") {
        return paux;
    }
    return Governor::get(param, unitType);
}
}  // namespace griddyn::governors
