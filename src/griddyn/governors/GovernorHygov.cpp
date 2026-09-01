/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorHygov.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::governors {
// The equations below mirror the published block diagram. Extra precedence
// parentheses obscure the signal flow without changing the calculation.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t pmechOutput = 0;
    constexpr index_t filterState = 0;
    constexpr index_t gatePositionState = 1;
    constexpr index_t servoState = 2;
    constexpr index_t flowState = 3;
    constexpr double minimumGate = 1e-8;
}  // namespace

GovernorHygov::GovernorHygov(const std::string& objName): Governor(objName)
{
    K = 20.0;  // 1 / R
    Pmax = 0.9;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
    opFlags.set(USES_POWER_LIMITS);
    opFlags.set(USES_RAMP_LIMITS);
}

CoreObject* GovernorHygov::clone(CoreObject* obj) const
{
    auto* governorClone = cloneBase<GovernorHygov, Governor>(this, obj);
    if (governorClone == nullptr) {
        return obj;
    }
    governorClone->temporaryDroop = temporaryDroop;
    governorClone->Tr = Tr;
    governorClone->Tf = Tf;
    governorClone->Tg = Tg;
    governorClone->Tw = Tw;
    governorClone->VELM = VELM;
    governorClone->At = At;
    governorClone->Dturb = Dturb;
    governorClone->qNL = qNL;
    governorClone->h0 = h0;
    return governorClone;
}

GovernorHygov::~GovernorHygov() = default;

void GovernorHygov::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(K) || !std::isfinite(temporaryDroop) || !std::isfinite(Tr) ||
        !std::isfinite(Tf) || !std::isfinite(Tg) || !std::isfinite(Tw) || !std::isfinite(VELM) ||
        !std::isfinite(Pmax) || !std::isfinite(Pmin) || !std::isfinite(At) ||
        !std::isfinite(Dturb) || !std::isfinite(qNL) || !std::isfinite(h0) || (K <= 0.0) ||
        (temporaryDroop <= 0.0) || (Tr <= 0.0) || (Tf <= 0.0) || (Tg <= 0.0) || (Tw <= 0.0) ||
        (VELM < 0.0) || (Pmax < Pmin) || (At <= 0.0) || (h0 <= 0.0)) {
        throw InvalidParameterValue("HYGOV parameters");
    }

    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 4;
    local.algRoots = 2;
    local.diffRoots = 0;
    local.jacSize = 32;
    prevTime = time0;
    opFlags.reset(GATE_RATE_LIMITED);
    opFlags.reset(GATE_RATE_LIMIT_HIGH);
    opFlags.reset(GATE_POSITION_LIMITED);
    opFlags.reset(GATE_POSITION_LIMIT_HIGH);
}

void GovernorHygov::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[pmechOutput])) {
        throw InvalidParameterValue("HYGOV initial mechanical power");
    }
    const double initialPower = desiredOutput[pmechOutput];
    const double initialFlow = initialPower / (At * h0) + qNL;
    const double initialGate = initialFlow / std::sqrt(h0);
    if (!std::isfinite(initialGate) || (std::abs(initialGate) < minimumGate)) {
        throw InvalidParameterValue("HYGOV initial gate is singular");
    }
    if ((initialGate < Pmin) || (initialGate > Pmax)) {
        throw InvalidParameterValue("HYGOV initial gate outside limits");
    }
    Pset = initialGate / K;

    const index_t algOffset = offsets.getAlgOffset(cLocalSolverMode);
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    m_state[algOffset + pmechOutput] = initialPower;
    m_state[diffOffset + filterState] = 0.0;
    m_state[diffOffset + gatePositionState] = initialGate;
    m_state[diffOffset + servoState] = initialGate;
    m_state[diffOffset + flowState] = initialFlow;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset;
}

double GovernorHygov::speedDeviation(const IOdata& inputs)
{
    return inputs[govOmegaInLocation] - 1.0;
}

double GovernorHygov::governorError(const IOdata& inputs, const double diffState[]) const
{
    const double permanentDroop = 1.0 / K;
    return Pset - speedDeviation(inputs) - permanentDroop * diffState[gatePositionState];
}

double GovernorHygov::filterDerivative(const IOdata& inputs, const double diffState[]) const
{
    return (governorError(inputs, diffState) - diffState[filterState]) / Tf;
}

double GovernorHygov::temporaryDroopLeadOutput(const IOdata& inputs, const double diffState[]) const
{
    return (Tr * filterDerivative(inputs, diffState) + diffState[filterState]) /
        (temporaryDroop * Tr);
}

double GovernorHygov::regularizedGate(const double diffState[])
{
    // H = (Q/G)^2 is physically singular at G = 0. Continuous HYGOV
    // trajectories initialized with nonzero G and nonnegative gate commands do
    // not cross zero, but the signed floor prevents a trial solver state from
    // producing NaN or infinity. The Jacobian uses zero dH/dG inside this flat
    // regularization interval.
    const double gate = diffState[servoState];
    if (std::abs(gate) >= minimumGate) {
        return gate;
    }
    return std::copysign(minimumGate, gate);
}

double GovernorHygov::turbineHead(const double diffState[]) const
{
    const double gate = regularizedGate(diffState);
    const double ratio = diffState[flowState] / gate;
    return ratio * ratio;
}

double GovernorHygov::mechanicalPower(const IOdata& inputs, const double diffState[]) const
{
    const double gate = diffState[servoState];
    const double flow = diffState[flowState];
    const double localHead = turbineHead(diffState);
    return At * localHead * (flow - qNL) - Dturb * speedDeviation(inputs) * gate;
}

double GovernorHygov::unlimitedGateRate(const IOdata& inputs, const double diffState[]) const
{
    return temporaryDroopLeadOutput(inputs, diffState);
}

double GovernorHygov::limitedGateRate(const IOdata& inputs, const double diffState[]) const
{
    return std::clamp(unlimitedGateRate(inputs, diffState),
                      -static_cast<double>(VELM),
                      static_cast<double>(VELM));
}

int GovernorHygov::gateRateLimitStatus(const IOdata& inputs, const double diffState[]) const
{
    const double rate = unlimitedGateRate(inputs, diffState);
    if (rate >= VELM) {
        return 1;
    }
    return (rate <= -VELM) ? -1 : 0;
}

int GovernorHygov::gatePositionLimitStatus(const IOdata& inputs, const double diffState[]) const
{
    const double rate = limitedGateRate(inputs, diffState);
    if ((diffState[gatePositionState] >= Pmax) && (rate >= 0.0)) {
        return 1;
    }
    if ((diffState[gatePositionState] <= Pmin) && (rate <= 0.0)) {
        return -1;
    }
    return 0;
}

void GovernorHygov::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[pmechOutput] =
            mechanicalPower(inputs, locations.diffStateLoc) - locations.algStateLoc[pmechOutput];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void GovernorHygov::derivative(const IOdata& inputs,
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
    const double gateRate =
        (gatePositionLimitStatus(inputs, state) == 0) ? limitedGateRate(inputs, state) : 0.0;
    stateDerivative[filterState] = filterDerivative(inputs, state);
    stateDerivative[gatePositionState] = gateRate;
    stateDerivative[servoState] = (state[gatePositionState] - state[servoState]) / Tg;
    stateDerivative[flowState] = (h0 - turbineHead(state)) / Tw;
}

void GovernorHygov::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    locations.destLoc[pmechOutput] = mechanicalPower(inputs, locations.diffStateLoc);
}

void GovernorHygov::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t algOffset = locations.algOffset;
    const index_t diffOffset = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const double gate = regularizedGate(state);
    const double physicalGate = state[servoState];
    const double flow = state[flowState];
    const double localHead = turbineHead(state);
    const double dHeadDGate =
        (std::abs(physicalGate) >= minimumGate) ? -2.0 * localHead / physicalGate : 0.0;
    const double dHeadDFlow = 2.0 * flow / (gate * gate);
    const bool rateLimited = gateRateLimitStatus(inputs, state) != 0;
    const bool positionLimited = gatePositionLimitStatus(inputs, state) != 0;
    const double permanentDroop = 1.0 / K;
    const double dLeadDFilter = (1.0 / temporaryDroop) * (1.0 / Tr - 1.0 / Tf);
    const double dLeadDGatePosition = -permanentDroop / (temporaryDroop * Tf);
    const double dLeadDOmega = -1.0 / (temporaryDroop * Tf);

    if (hasAlgebraic(sMode)) {
        matrixData.assign(algOffset + pmechOutput, algOffset + pmechOutput, -1.0);
        matrixData.assign(algOffset + pmechOutput,
                          diffOffset + servoState,
                          At * dHeadDGate * (flow - qNL) - Dturb * speedDeviation(inputs));
        matrixData.assign(algOffset + pmechOutput,
                          diffOffset + flowState,
                          At * (dHeadDFlow * (flow - qNL) + localHead));
        matrixData.assignCheckCol(algOffset + pmechOutput,
                                  inputLocs[govOmegaInLocation],
                                  -Dturb * state[servoState]);
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    matrixData.assign(diffOffset + filterState, diffOffset + filterState, -1.0 / Tf - stateData.cj);
    matrixData.assign(diffOffset + filterState,
                      diffOffset + gatePositionState,
                      -permanentDroop / Tf);
    matrixData.assignCheckCol(diffOffset + filterState, inputLocs[govOmegaInLocation], -1.0 / Tf);

    if (rateLimited || positionLimited) {
        matrixData.assign(diffOffset + gatePositionState,
                          diffOffset + gatePositionState,
                          -stateData.cj);
    } else {
        matrixData.assign(diffOffset + gatePositionState, diffOffset + filterState, dLeadDFilter);
        matrixData.assign(diffOffset + gatePositionState,
                          diffOffset + gatePositionState,
                          dLeadDGatePosition - stateData.cj);
        matrixData.assignCheckCol(diffOffset + gatePositionState,
                                  inputLocs[govOmegaInLocation],
                                  dLeadDOmega);
    }

    matrixData.assign(diffOffset + servoState, diffOffset + gatePositionState, 1.0 / Tg);
    matrixData.assign(diffOffset + servoState, diffOffset + servoState, -1.0 / Tg - stateData.cj);

    matrixData.assign(diffOffset + flowState, diffOffset + servoState, -dHeadDGate / Tw);
    matrixData.assign(diffOffset + flowState,
                      diffOffset + flowState,
                      -dHeadDFlow / Tw - stateData.cj);
}

void GovernorHygov::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    for (index_t state = 0; state < offsets.local().local.diffSize; ++state) {
        m_state[diffOffset + state] += timeStep * m_dstate_dt[diffOffset + state];
    }
    m_state[diffOffset + gatePositionState] = std::clamp(m_state[diffOffset + gatePositionState],
                                                         static_cast<double>(Pmin),
                                                         static_cast<double>(Pmax));
    m_state[offsets.getAlgOffset(cLocalSolverMode) + pmechOutput] =
        mechanicalPower(inputs, m_state.data() + diffOffset);
    updateLimitFlags(inputs, m_state.data() + diffOffset);
    prevTime = time;
}

bool GovernorHygov::updateLimitFlags(const IOdata& inputs, const double diffState[])
{
    const int rateStatus = gateRateLimitStatus(inputs, diffState);
    const int positionStatus = gatePositionLimitStatus(inputs, diffState);
    const bool changed = (opFlags[GATE_RATE_LIMITED] != (rateStatus != 0)) ||
        (opFlags[GATE_RATE_LIMIT_HIGH] != (rateStatus > 0)) ||
        (opFlags[GATE_POSITION_LIMITED] != (positionStatus != 0)) ||
        (opFlags[GATE_POSITION_LIMIT_HIGH] != (positionStatus > 0));
    opFlags.set(GATE_RATE_LIMITED, rateStatus != 0);
    opFlags.set(GATE_RATE_LIMIT_HIGH, rateStatus > 0);
    opFlags.set(GATE_POSITION_LIMITED, positionStatus != 0);
    opFlags.set(GATE_POSITION_LIMIT_HIGH, positionStatus > 0);
    return changed;
}

void GovernorHygov::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double rate = unlimitedGateRate(inputs, state);
    if (opFlags[GATE_RATE_LIMITED]) {
        roots[rootOffset] = opFlags[GATE_RATE_LIMIT_HIGH] ? VELM - rate : rate + VELM;
    } else {
        roots[rootOffset] = std::min(VELM - rate, rate + VELM);
    }
    const double limitedRateValue = limitedGateRate(inputs, state);
    if (opFlags[GATE_POSITION_LIMITED]) {
        roots[rootOffset + 1] =
            opFlags[GATE_POSITION_LIMIT_HIGH] ? limitedRateValue : -limitedRateValue;
    } else {
        roots[rootOffset + 1] =
            std::min(Pmax - state[gatePositionState], state[gatePositionState] - Pmin);
    }
}

void GovernorHygov::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] == 0) && (rootMask[rootOffset + 1] == 0)) {
        return;
    }
    double* state = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    state[gatePositionState] =
        std::clamp(state[gatePositionState], static_cast<double>(Pmin), static_cast<double>(Pmax));
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode GovernorHygov::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    double* state = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    state[gatePositionState] =
        std::clamp(state[gatePositionState], static_cast<double>(Pmin), static_cast<double>(Pmax));
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

void GovernorHygov::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorHygov::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "r") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV R must be positive and finite");
        }
        Governor::set(param, val, unitType);
    } else if ((param == "temporarydroop") || (param == "rtmp") || (param == "rtemp")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV temporary droop must be positive and finite");
        }
        temporaryDroop = val;
    } else if ((param == "tr") || (param == "t_r") || (param == "t1")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV Tr must be positive and finite");
        }
        Tr = val;
    } else if ((param == "tf") || (param == "t_f")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV Tf must be positive and finite");
        }
        Tf = val;
    } else if ((param == "tg") || (param == "t_g")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV Tg must be positive and finite");
        }
        Tg = val;
    } else if ((param == "velm") || (param == "velmax")) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("HYGOV VELM must be nonnegative and finite");
        }
        VELM = val;
    } else if ((param == "gmax") || (param == "pmax")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("HYGOV maximum gate limit must be finite");
        }
        Pmax = val;
    } else if ((param == "gmin") || (param == "pmin")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("HYGOV minimum gate limit must be finite");
        }
        Pmin = val;
    } else if ((param == "tw") || (param == "t_w")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV Tw must be positive and finite");
        }
        Tw = val;
    } else if ((param == "at") || (param == "a_t")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV At must be positive and finite");
        }
        At = val;
    } else if ((param == "dt") || (param == "dturb") || (param == "d_turb")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("HYGOV Dturb must be finite");
        }
        Dturb = val;
    } else if ((param == "qnl") || (param == "q_nl")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("HYGOV qNL must be finite");
        }
        qNL = val;
    } else if ((param == "h0") || (param == "head0")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("HYGOV h0 must be positive and finite");
        }
        h0 = val;
    } else {
        Governor::set(param, val, unitType);
    }
}

double GovernorHygov::get(std::string_view param, units::unit unitType) const
{
    if ((param == "temporarydroop") || (param == "rtmp") || (param == "rtemp")) {
        return temporaryDroop;
    }
    if ((param == "tr") || (param == "t_r") || (param == "t1")) {
        return Tr;
    }
    if ((param == "tf") || (param == "t_f")) {
        return Tf;
    }
    if ((param == "tg") || (param == "t_g")) {
        return Tg;
    }
    if ((param == "velm") || (param == "velmax")) {
        return VELM;
    }
    if ((param == "gmax") || (param == "gmin")) {
        return (param == "gmax") ? Pmax : Pmin;
    }
    if ((param == "tw") || (param == "t_w")) {
        return Tw;
    }
    if ((param == "at") || (param == "a_t")) {
        return At;
    }
    if ((param == "dt") || (param == "dturb") || (param == "d_turb")) {
        return Dturb;
    }
    if ((param == "qnl") || (param == "q_nl")) {
        return qNL;
    }
    if ((param == "h0") || (param == "head0")) {
        return h0;
    }
    if (param == "head") {
        return turbineHead(m_state.data() + offsets.getDiffOffset(cLocalSolverMode));
    }
    return Governor::get(param, unitType);
}

stringVec GovernorHygov::localStateNames() const
{
    return {"pmech", "filter", "gate_position", "gate", "flow"};
}

index_t GovernorHygov::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode) + pmechOutput;
    }
    if ((field == "filter") || (field == "lg")) {
        return offsets.getDiffOffset(sMode) + filterState;
    }
    if ((field == "gate_position") || (field == "c") || (field == "gtpos")) {
        return offsets.getDiffOffset(sMode) + gatePositionState;
    }
    if ((field == "gate") || (field == "g")) {
        return offsets.getDiffOffset(sMode) + servoState;
    }
    if ((field == "flow") || (field == "q")) {
        return offsets.getDiffOffset(sMode) + flowState;
    }
    if ((field == "head") || (field == "h")) {
        return kInvalidLocation;
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::governors
