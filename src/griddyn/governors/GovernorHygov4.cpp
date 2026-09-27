/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorHygov4.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::governors {
GovernorHygov4::GovernorHygov4(const std::string& name): Governor(name)
{
    Pmax = 1.0;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}
CoreObject* GovernorHygov4::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorHygov4, Governor>(this, obj);
    if (out != nullptr) {
        out->Rperm = Rperm;
        out->Rtemp = Rtemp;
        out->UO = UO;
        out->UC = UC;
        out->Tp = Tp;
        out->Tg = Tg;
        out->Tr = Tr;
        out->Tw = Tw;
        out->At = At;
        out->Dturb = Dturb;
        out->Hdam = Hdam;
        out->qNL = qNL;
        out->paux = paux;
        out->referenceOffset = referenceOffset;
    }
    return out;
}
void GovernorHygov4::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    const std::array<double, 15> pars{
        Rperm, Rtemp, UO, UC, Pmax, Pmin, Tp, Tg, Tr, Tw, At, Dturb, Hdam, qNL, paux};
    if (std::any_of(pars.begin(), pars.end(), [](double value) { return !std::isfinite(value); }) ||
        Rperm <= 0.0 || Rtemp < 0.0 || UO < UC || Tp <= 0.0 || Tg <= 0.0 || Tr <= 0.0 ||
        Tw <= 0.0 || At <= 0.0 || Hdam <= 0.0 || Pmax < Pmin) {
        throw InvalidParameterValue("HYGOV4 parameters");
    }
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 4;
    local.algRoots = 0;
    local.diffRoots = 0;
    local.jacSize = 28;
    prevTime = time0;
}
void GovernorHygov4::dynObjectInitializeB([[maybe_unused]] const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("HYGOV4 initial power");
    }
    const double flow = qNL + (desiredOutput[0] / (At * Hdam));
    const double gate = flow / std::sqrt(Hdam);
    if (!std::isfinite(gate) || std::abs(gate) < 1e-8 || gate < Pmin ||
        !adjustInitialUpperLimit(gate, "HYGOV4 initial gate")) {
        throw InvalidParameterValue("HYGOV4 initial gate outside limits");
    }
    const auto algOffset = offsets.getAlgOffset(cLocalSolverMode);
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[algOffset] = desiredOutput[0];
    m_state[diffOffset] = gate;
    m_state[diffOffset + 1] = gate;
    m_state[diffOffset + 2] = 0.0;
    m_state[diffOffset + 3] = flow;
    Pset = Rperm * gate;
    // The generator's pset input remains its dispatched mechanical power.
    // Preserve the ANDES pref=Rperm*gate equilibrium with a reference offset.
    referenceOffset = Pset - desiredOutput[0] - paux;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset - paux;
}
double GovernorHygov4::regularizedGate(double gate)
{
    return std::abs(gate) >= 1e-8 ? gate : std::copysign(1e-8, gate);
}
double GovernorHygov4::mechanicalPower(const IOdata& inputs, const double* state) const
{
    const double gate = regularizedGate(state[0]);
    const double head = (state[3] / gate) * (state[3] / gate);
    return (At * head * (state[3] - qNL)) - (Dturb * state[0] * (inputs[govOmegaInLocation] - 1.0));
}
double GovernorHygov4::gateRate(const double* state) const
{
    const double raw = state[2] / Tg;
    const double rate = std::clamp(raw, UC, UO);
    if ((state[0] >= Pmax && rate > 0.0) || (state[0] <= Pmin && rate < 0.0)) {
        return 0.0;
    }
    return rate;
}
void GovernorHygov4::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    const double gate = regularizedGate(state[0]);
    const double head = (state[3] / gate) * (state[3] / gate);
    loc.destDiffLoc[0] = gateRate(state);
    loc.destDiffLoc[1] = (state[0] - state[1]) / Tr;
    loc.destDiffLoc[2] =
        (inputs[govpSetInLocation] + referenceOffset + paux - (Rperm * state[0]) -
         (Rtemp * (state[0] - state[1])) - (inputs[govOmegaInLocation] - 1.0) - state[2]) /
        Tp;
    loc.destDiffLoc[3] = (Hdam - head) / Tw;
}
void GovernorHygov4::residual(const IOdata& inputs,
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
void GovernorHygov4::algebraicUpdate(const IOdata& inputs,
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
void GovernorHygov4::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double* state = loc.diffStateLoc;
    const double gate = regularizedGate(state[0]);
    const double head = (state[3] / gate) * (state[3] / gate);
    const double dhdg = std::abs(state[0]) >= 1e-8 ? -2.0 * head / gate : 0.0;
    const double dhdq = 2.0 * state[3] / (gate * gate);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset,
                              loc.diffOffset,
                              (At * dhdg * (state[3] - qNL)) -
                                  (Dturb * (inputs[govOmegaInLocation] - 1.0)));
            matrixData.assign(loc.algOffset,
                              loc.diffOffset + 3,
                              At * ((dhdq * (state[3] - qNL)) + head));
        }
        matrixData.assignCheckCol(loc.algOffset, inputLocs[govOmegaInLocation], -Dturb * state[0]);
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double raw = state[2] / Tg;
    const double rate = std::clamp(raw, UC, UO);
    const bool gateLimited = (state[0] >= Pmax && rate > 0.0) || (state[0] <= Pmin && rate < 0.0);
    matrixData.assign(loc.diffOffset, loc.diffOffset, -stateData.cj);
    if (!gateLimited && raw > UC && raw < UO) {
        matrixData.assign(loc.diffOffset, loc.diffOffset + 2, 1.0 / Tg);
    }
    matrixData.assign(loc.diffOffset + 1, loc.diffOffset, 1.0 / Tr);
    matrixData.assign(loc.diffOffset + 1, loc.diffOffset + 1, (-1.0 / Tr) - stateData.cj);
    matrixData.assign(loc.diffOffset + 2, loc.diffOffset, -(Rperm + Rtemp) / Tp);
    matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 1, Rtemp / Tp);
    matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 2, (-1.0 / Tp) - stateData.cj);
    matrixData.assignCheckCol(loc.diffOffset + 2, inputLocs[govOmegaInLocation], -1.0 / Tp);
    matrixData.assignCheckCol(loc.diffOffset + 2, inputLocs[govpSetInLocation], 1.0 / Tp);
    matrixData.assign(loc.diffOffset + 3, loc.diffOffset, -dhdg / Tw);
    matrixData.assign(loc.diffOffset + 3, loc.diffOffset + 3, (-dhdq / Tw) - stateData.cj);
}
void GovernorHygov4::timestep(CoreTime time,
                              const IOdata& inputs,
                              [[maybe_unused]] const SolverMode& sMode)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const auto diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    for (index_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
        m_state[diffOffset + stateIndex] += timeStep * m_dstate_dt[diffOffset + stateIndex];
    }
    m_state[diffOffset] =
        std::clamp(m_state[diffOffset], static_cast<double>(Pmin), static_cast<double>(Pmax));
    m_state[offsets.getAlgOffset(cLocalSolverMode)] =
        mechanicalPower(inputs, m_state.data() + diffOffset);
    prevTime = time;
}
index_t GovernorHygov4::findIndex(std::string_view field, const SolverMode& mode) const
{
    if (field == "pm" || field == "pmech") {
        return offsets.getAlgOffset(mode);
    }
    if (field == "gate") {
        return offsets.getDiffOffset(mode);
    }
    if (field == "washout") {
        return offsets.getDiffOffset(mode) + 1;
    }
    if (field == "pilot") {
        return offsets.getDiffOffset(mode) + 2;
    }
    if (field == "flow") {
        return offsets.getDiffOffset(mode) + 3;
    }
    return kInvalidLocation;
}
void GovernorHygov4::set(std::string_view param, double value, units::unit unitType)
{
    if (!std::isfinite(value)) {
        throw InvalidParameterValue("HYGOV4 parameter must be finite");
    }
    if (param == "rperm") {
        Rperm = value;
    } else if (param == "rtemp") {
        Rtemp = value;
    } else if (param == "uo") {
        UO = value;
    } else if (param == "uc") {
        UC = value;
    } else if (param == "tp") {
        Tp = value;
    } else if (param == "tg") {
        Tg = value;
    } else if (param == "tr") {
        Tr = value;
    } else if (param == "tw") {
        Tw = value;
    } else if (param == "at") {
        At = value;
    } else if (param == "dturb") {
        Dturb = value;
    } else if (param == "hdam") {
        Hdam = value;
    } else if (param == "qnl") {
        qNL = value;
    } else if (param == "paux") {
        paux = value;
    } else {
        Governor::set(param, value, unitType);
    }
}
double GovernorHygov4::get(std::string_view param, units::unit unitType) const
{
    if (param == "rperm") {
        return Rperm;
    }
    if (param == "rtemp") {
        return Rtemp;
    }
    if (param == "uo") {
        return UO;
    }
    if (param == "uc") {
        return UC;
    }
    if (param == "tp") {
        return Tp;
    }
    if (param == "tg") {
        return Tg;
    }
    if (param == "tr") {
        return Tr;
    }
    if (param == "tw") {
        return Tw;
    }
    if (param == "at") {
        return At;
    }
    if (param == "dturb") {
        return Dturb;
    }
    if (param == "hdam") {
        return Hdam;
    }
    if (param == "qnl") {
        return qNL;
    }
    if (param == "paux") {
        return paux;
    }
    return Governor::get(param, unitType);
}
}  // namespace griddyn::governors
