/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterOEL4C.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::limiters {
namespace {
    constexpr double timerRootTolerance = 1e-8;
    constexpr double reactiveRootTolerance = 1e-9;
}  // namespace
ExcitationLimiterOEL4C::ExcitationLimiterOEL4C(const std::string& objName):
    ExcitationLimiter(objName)
{
    setRole(Role::OVER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 1;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = 16;
}

CoreObject* ExcitationLimiterOEL4C::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterOEL4C, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->ki = ki;
        result->kp = kp;
        result->delay = delay;
        result->minimum = minimum;
        result->qRef = qRef;
        result->qRefSet = qRefSet;
        result->violating = false;
        result->delayElapsed = false;
        result->violationStart = 0.0;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterOEL4C::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "ki" || param == "k_i") {
        ki = val;
    } else if (param == "kp" || param == "k_p") {
        kp = val;
    } else if (param == "tdelay" || param == "t_delay") {
        delay = val;
    } else if (param == "vmin" || param == "vminpu" || param == "v_min_pu") {
        minimum = val;
    } else if (param == "qref" || param == "qrefpu" || param == "q_ref_pu") {
        qRef = val;
        qRefSet = true;
    } else {
        ExcitationLimiter::set(param, val, unitType);
    }
}

double ExcitationLimiterOEL4C::get(std::string_view param, units::unit unitType) const
{
    if (param == "ki" || param == "k_i") {
        return ki;
    }
    if (param == "kp" || param == "k_p") {
        return kp;
    }
    if (param == "tdelay" || param == "t_delay") {
        return delay;
    }
    if (param == "vmin" || param == "vminpu" || param == "v_min_pu") {
        return minimum;
    }
    if (param == "qref" || param == "qrefpu" || param == "q_ref_pu") {
        return qRef;
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterOEL4C::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!qRefSet || !std::isfinite(ki) || !std::isfinite(kp) || !std::isfinite(delay) ||
        !std::isfinite(minimum) || !std::isfinite(qRef) || ki < 0.0 || kp < 0.0 || delay < 0.0 ||
        minimum >= 0.0) {
        throw InvalidParameterValue("OEL4C gains, delay, minimum, or Q reference");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 1;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = 16;
    prevTime = time0;
    violating = false;
    delayElapsed = false;
    violationStart = time0;
}

ExcitationLimiterOEL4C::ReactivePower
    ExcitationLimiterOEL4C::reactivePower(const IOdata& inputs)
{
    if (inputs.size() < excitationLimiterInputCount) {
        throw InvalidParameterValue("OEL4C input vector");
    }
    for (auto index :
         {limiterIdInLocation, limiterIqInLocation, limiterVdInLocation, limiterVqInLocation}) {
        if (!std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
            throw InvalidParameterValue("OEL4C requires compatible machine Q and voltage");
        }
    }
    const double idCurrent = inputs[limiterIdInLocation];
    const double iqCurrent = inputs[limiterIqInLocation];
    const double vdVoltage = inputs[limiterVdInLocation];
    const double vqVoltage = inputs[limiterVqInLocation];
    return {.value = (idCurrent * vqVoltage) - (iqCurrent * vdVoltage),
            .dId = vqVoltage,
            .dIq = -vdVoltage,
            .dVd = -iqCurrent,
            .dVq = idCurrent};
}

void ExcitationLimiterOEL4C::updateTimer(bool violation, CoreTime time)
{
    if (violation && !violating) {
        violationStart = time;
        delayElapsed = delay <= 0.0;
    } else if (!violation) {
        delayElapsed = false;
    }
    violating = violation;
}

double ExcitationLimiterOEL4C::piInput(double reactivePowerValue, CoreTime time) const
{
    if (reactivePowerValue <= qRef || !violating ||
        (!delayElapsed && time < violationStart + delay)) {
        return 0.0;
    }
    return qRef - reactivePowerValue;
}

double ExcitationLimiterOEL4C::action(double input, double integral) const
{
    // Dynawo's OEL4C is signed nonpositive. The GridDyn VOEL route carries
    // its positive magnitude and SCRX subtracts that magnitude at its sum.
    return -std::clamp((kp * input) + integral, minimum, 0.0);
}

double ExcitationLimiterOEL4C::integralRate(double input, double integral) const
{
    const double raw = (kp * input) + integral;
    if ((raw <= minimum && input < 0.0) || (raw >= 0.0 && input > 0.0)) {
        return 0.0;
    }
    return ki * input;
}

void ExcitationLimiterOEL4C::dynObjectInitializeB(const IOdata& inputs,
                                                  const IOdata& /*desiredOutput*/,
                                                  IOdata& fieldSet)
{
    updateTimer(reactivePower(inputs).value > qRef, prevTime);
    m_state[1] = 0.0;
    m_state[0] = action(piInput(reactivePower(inputs).value, prevTime), 0.0);
    fieldSet = {m_state[0]};
}

void ExcitationLimiterOEL4C::residual(const IOdata& inputs,
                                      const StateData& stateData,
                                      double resid[],
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const double input = piInput(reactivePower(inputs).value, stateData.time);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = action(input, loc.diffStateLoc[0]) - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        loc.destDiffLoc[0] = integralRate(input, loc.diffStateLoc[0]) - loc.dstateLoc[0];
    }
}

void ExcitationLimiterOEL4C::derivative(const IOdata& inputs,
                                        const StateData& stateData,
                                        double deriv[],
                                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    loc.destDiffLoc[0] =
        integralRate(piInput(reactivePower(inputs).value, stateData.time), loc.diffStateLoc[0]);
}

void ExcitationLimiterOEL4C::algebraicUpdate(const IOdata& inputs,
                                             const StateData& stateData,
                                             double update[],
                                             const SolverMode& sMode,
                                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] =
        action(piInput(reactivePower(inputs).value, stateData.time), loc.diffStateLoc[0]);
}

void ExcitationLimiterOEL4C::jacobianElements(const IOdata& inputs,
                                              const StateData& stateData,
                                              MatrixData<double>& matrixData,
                                              const IOlocs& inputLocs,
                                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto reactivePowerEvaluation = reactivePower(inputs);
    const double input = piInput(reactivePowerEvaluation.value, stateData.time);
    const double integral = loc.diffStateLoc[0];
    const double raw = (kp * input) + integral;
    const bool unsaturated = raw > minimum && raw < 0.0;
    const bool blockedAtMinimum = raw <= minimum && input < 0.0;
    const bool blockedAtMaximum = raw >= 0.0 && input > 0.0;
    const bool integrating = !blockedAtMinimum && !blockedAtMaximum;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (unsaturated && !isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset, loc.diffOffset, -1.0);
        }
    }
    if (hasDifferential(sMode)) {
        matrixData.assign(loc.diffOffset, loc.diffOffset, -stateData.cj);
    }
    if (input == 0.0) {
        return;
    }
    const std::array<double, excitationLimiterInputCount> reactivePowerDerivatives{
        0,
        reactivePowerEvaluation.dId,
        reactivePowerEvaluation.dIq,
        reactivePowerEvaluation.dVd,
        reactivePowerEvaluation.dVq};
    for (index_t index = 1; index < excitationLimiterInputCount; ++index) {
        if (hasAlgebraic(sMode) && unsaturated) {
            matrixData.assignCheckCol(loc.algOffset,
                                      inputLocs[index],
                                      kp * reactivePowerDerivatives[index]);
        }
        if (hasDifferential(sMode) && integrating) {
            matrixData.assignCheckCol(loc.diffOffset,
                                      inputLocs[index],
                                      -ki * reactivePowerDerivatives[index]);
        }
    }
}

void ExcitationLimiterOEL4C::rootTest(const IOdata& inputs,
                                      const StateData& stateData,
                                      double roots[],
                                      const SolverMode& sMode)
{
    const auto root = offsets.getRootOffset(sMode);
    if (root == kNullLocation) {
        return;
    }
    const double excess = reactivePower(inputs).value - qRef;
    // Keep a root at an exact Q=Qref operating point off zero after either
    // branch is selected, avoiding repeated zero-time threshold events.
    roots[root] = excess + (violating ? reactiveRootTolerance : -reactiveRootTolerance);
    if (delay > 0.0 && violating && !delayElapsed && excess > 0.0) {
        const double distance = static_cast<double>(stateData.time - (violationStart + delay));
        // CoreTime has nanosecond resolution; IDA may bracket a crossing at
        // half a tick. Give the root finder an attainable zero at that edge.
        roots[root + 1] = std::abs(distance) <= timerRootTolerance ? 0.0 : distance;
    } else {
        roots[root + 1] = 1.0;
    }
}

void ExcitationLimiterOEL4C::rootTrigger(CoreTime time,
                                         const IOdata& inputs,
                                         const std::vector<int>& rootMask,
                                         const SolverMode& sMode)
{
    const auto root = offsets.getRootOffset(sMode);
    if (root == kNullLocation) {
        return;
    }
    if (rootMask[root] != 0) {
        updateTimer(rootMask[root] > 0, time);
    } else if (rootMask[root + 1] != 0) {
        delayElapsed = reactivePower(inputs).value > qRef;
    }
}

ChangeCode ExcitationLimiterOEL4C::rootCheck(const IOdata& inputs,
                                             const StateData& stateData,
                                             const SolverMode& /*sMode*/,
                                             CheckLevel /*level*/)
{
    // GridDyn's algebraic-root sweep passes emptyStateData after setState().
    // In that path prevTime holds the probe time; emptyStateData.time is 0.
    const CoreTime checkTime = (stateData.state == nullptr) ? prevTime : stateData.time;
    const bool nowViolating = reactivePower(inputs).value > qRef;
    if (nowViolating != violating) {
        updateTimer(nowViolating, checkTime);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    if (nowViolating && !delayElapsed && checkTime + timerRootTolerance >= violationStart + delay) {
        delayElapsed = true;
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

void ExcitationLimiterOEL4C::timestep(CoreTime time,
                                      const IOdata& inputs,
                                      const SolverMode& /*sMode*/)
{
    const double step = time - prevTime;
    const double reactivePowerValue = reactivePower(inputs).value;
    updateTimer(reactivePowerValue > qRef, time);
    if (violating && time >= violationStart + delay) {
        delayElapsed = true;
    }
    const double input = piInput(reactivePowerValue, time);
    m_state[1] += step * integralRate(input, m_state[1]);
    m_state[0] = action(input, m_state[1]);
    prevTime = time;
}

stringVec ExcitationLimiterOEL4C::localStateNames() const
{
    return {"voel", "integral"};
}
}  // namespace griddyn::limiters
