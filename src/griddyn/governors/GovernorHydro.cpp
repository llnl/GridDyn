/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorHydro.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::governors {
namespace {
    constexpr index_t outputState = 0;
    constexpr index_t leadLagState = 0;
    constexpr index_t gateLagState = 1;
    constexpr index_t waterwayState = 2;
}  // namespace

GovernorHydro::GovernorHydro(const std::string& objName): Governor(objName)
{
    K = 5.0;
    T1 = 0.25;
    T2 = 0.0;
    T3 = 0.1;
    Tw = 0.04;
    Pmax = 1.5;
    Pmin = 0.5;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
    opFlags.set(USES_POWER_LIMITS);
}

CoreObject* GovernorHydro::clone(CoreObject* obj) const
{
    auto* gov = cloneBase<GovernorHydro, Governor>(this, obj);
    if (gov == nullptr) {
        return obj;
    }
    gov->Tw = Tw;
    gov->governorLeadLag = governorLeadLag;
    gov->waterway = waterway;
    return gov;
}

GovernorHydro::~GovernorHydro() = default;

void GovernorHydro::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(K) || !std::isfinite(T1) || !std::isfinite(T2) || !std::isfinite(T3) ||
        !std::isfinite(Tw) || !std::isfinite(Pmax) || !std::isfinite(Pmin) || (T1 <= 0.0) ||
        (T3 <= 0.0) || (Tw <= 0.0) || (Pmax < Pmin)) {
        throw InvalidParameterValue("hydro governor time constants or gate limits");
    }
    governorLeadLag.setParameters(T2, T1, K);
    waterway.setParameters(-Tw, 0.5 * Tw);
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 3;
    local.jacSize = 18;
    prevTime = time0;
}

void GovernorHydro::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[outputState])) {
        throw InvalidParameterValue("hydro governor initial output");
    }
    const double power = desiredOutput[outputState];
    if ((power < Pmin) || (power > Pmax)) {
        throw InvalidParameterValue("hydro governor initial gate outside limits");
    }
    Pset = power;
    const index_t algOffset = offsets.getAlgOffset(cLocalSolverMode);
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[algOffset + outputState] = power;
    m_state[diffOffset + leadLagState] = 0.0;
    m_state[diffOffset + gateLagState] = 0.0;
    m_state[diffOffset + waterwayState] = power;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset;
}

void GovernorHydro::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[outputState] =
            locations.diffStateLoc[waterwayState] - locations.algStateLoc[outputState];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void GovernorHydro::derivative(const IOdata& inputs,
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
    const double speedDeviation = inputs[govOmegaInLocation] - 1.0;
    stateDerivative[leadLagState] = governorLeadLag.derivative(speedDeviation, state[leadLagState]);
    const double governorOutput = governorLeadLag.output(speedDeviation, state[leadLagState]);
    stateDerivative[gateLagState] = (governorOutput - state[gateLagState]) / T3;
    const double unlimitedGate = inputs[govpSetInLocation] - state[gateLagState];
    const double gate =
        std::clamp(unlimitedGate, static_cast<double>(Pmin), static_cast<double>(Pmax));
    const double gateDerivative = (gate == unlimitedGate) ? -stateDerivative[gateLagState] : 0.0;
    stateDerivative[waterwayState] =
        waterway.outputStateDerivative(gate, state[waterwayState], gateDerivative);
}

void GovernorHydro::algebraicUpdate(const IOdata& /*inputs*/,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    locations.destLoc[outputState] = locations.diffStateLoc[waterwayState];
}

void GovernorHydro::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t algOffset = locations.algOffset;
    const index_t diffOffset = locations.diffOffset;
    const double* state = locations.diffStateLoc;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(algOffset + outputState, algOffset + outputState, -1.0);
        matrixData.assign(algOffset + outputState, diffOffset + waterwayState, 1.0);
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    matrixData.assign(diffOffset + leadLagState,
                      diffOffset + leadLagState,
                      governorLeadLag.derivativeStateJacobian() - stateData.cj);
    matrixData.assignCheckCol(diffOffset + leadLagState,
                              inputLocs[govOmegaInLocation],
                              governorLeadLag.derivativeInputJacobian());

    matrixData.assign(diffOffset + gateLagState,
                      diffOffset + gateLagState,
                      -1.0 / T3 - stateData.cj);
    matrixData.assign(diffOffset + gateLagState,
                      diffOffset + leadLagState,
                      governorLeadLag.outputStateJacobian() / T3);
    matrixData.assignCheckCol(diffOffset + gateLagState,
                              inputLocs[govOmegaInLocation],
                              governorLeadLag.outputInputJacobian() / T3);

    const double unlimitedGate = inputs[govpSetInLocation] - state[gateLagState];
    const bool gateLimited = (unlimitedGate < Pmin) || (unlimitedGate > Pmax);
    matrixData.assign(diffOffset + waterwayState,
                      diffOffset + waterwayState,
                      waterway.derivativeStateJacobian() - stateData.cj);
    if (!gateLimited) {
        matrixData.assignCheckCol(diffOffset + waterwayState,
                                  inputLocs[govpSetInLocation],
                                  waterway.derivativeInputJacobian());
        matrixData.assign(diffOffset + waterwayState,
                          diffOffset + gateLagState,
                          -waterway.derivativeInputJacobian() +
                              waterway.outputStateInputDerivativeJacobian() / T3);
        matrixData.assign(diffOffset + waterwayState,
                          diffOffset + leadLagState,
                          -waterway.outputStateInputDerivativeJacobian() *
                              governorLeadLag.outputStateJacobian() / T3);
        matrixData.assignCheckCol(diffOffset + waterwayState,
                                  inputLocs[govOmegaInLocation],
                                  -waterway.outputStateInputDerivativeJacobian() *
                                      governorLeadLag.outputInputJacobian() / T3);
    }
}

void GovernorHydro::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    for (index_t state = 0; state < offsets.local().local.diffSize; ++state) {
        m_state[diffOffset + state] += timeStep * m_dstate_dt[diffOffset + state];
    }
    m_state[offsets.getAlgOffset(cLocalSolverMode) + outputState] =
        m_state[diffOffset + waterwayState];
    prevTime = time;
}

index_t GovernorHydro::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode) + outputState;
    }
    if ((field == "leadlag") || (field == "x")) {
        return offsets.getDiffOffset(sMode) + leadLagState;
    }
    if ((field == "gate") || (field == "gate_lag")) {
        return offsets.getDiffOffset(sMode) + gateLagState;
    }
    if ((field == "waterway") || (field == "water")) {
        return offsets.getDiffOffset(sMode) + waterwayState;
    }
    return kInvalidLocation;
}

void GovernorHydro::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorHydro::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "tw") || (param == "t4")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("hydro governor Tw/T4 must be positive and finite");
        }
        Tw = val;
    } else if ((param == "t1") || (param == "t3")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("hydro governor time constant must be positive and finite");
        }
        Governor::set(param, val, unitType);
    } else if (param == "t2") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("hydro governor lead time must be finite");
        }
        Governor::set(param, val, unitType);
    } else if ((param == "pmax") || (param == "pmin")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("hydro governor gate limit must be finite");
        }
        Governor::set(param, val, unitType);
    } else {
        Governor::set(param, val, unitType);
    }
}

double GovernorHydro::get(std::string_view param, units::unit unitType) const
{
    if ((param == "tw") || (param == "t4")) {
        return Tw;
    }
    return Governor::get(param, unitType);
}

stringVec GovernorHydro::localStateNames() const
{
    return {"pmech", "leadlag", "gate_lag", "waterway"};
}

}  // namespace griddyn::governors
