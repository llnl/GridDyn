/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../comms/SchedulerMessage.h"
#include "AGControl.h"
#include "ReserveDispatcher.h"
#include "Scheduler.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

namespace griddyn {
namespace {
    struct RampTargetUpdate {
        double rampRate;
        CoreTime nextUpdateTime;
    };

    double boundedRampRate(double difference, double rate, double rampLimitUp, double rampLimitDown)
    {
        if (difference > 0.0) {
            if (!std::isfinite(rate) || rate <= 0.0) {
                rate = rampLimitUp;
            }
            return std::clamp(rate, 0.0, rampLimitUp);
        }
        if (!std::isfinite(rate) || rate >= 0.0) {
            rate = -rampLimitDown;
        }
        return std::clamp(rate, -rampLimitDown, 0.0);
    }

    double rampCompletionDelay(double difference, double rate)
    {
        if (std::abs(difference) <= 1.0e-12) {
            return 0.0;
        }
        if (!std::isfinite(rate) || difference * rate <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }
        const double delay = difference / rate;
        return (delay > 0.0 && std::isfinite(delay)) ? delay :
                                                       std::numeric_limits<double>::infinity();
    }

    RampTargetUpdate calculateRampTargetUpdate(SchedulerRamp::RampMode mode,
                                               double remainingPower,
                                               double powerDifference,
                                               double targetDeltaTime,
                                               CoreTime previousTime,
                                               CoreTime targetTime,
                                               CoreTime lastTargetTime,
                                               double rampTime,
                                               double currentRampRate,
                                               double rampLimitUp,
                                               double rampLimitDown)
    {
        double rampRate = currentRampRate;
        CoreTime nextUpdateTime = targetTime;
        double remainingTime;

        switch (mode) {
            case SchedulerRamp::INTERP:
                rampRate = powerDifference / targetDeltaTime;
                if (rampRate > rampLimitUp) {
                    rampRate = rampLimitUp;
                } else if (rampRate < -rampLimitDown) {
                    rampRate = -rampLimitDown;
                }
                break;
            case SchedulerRamp::MID_POINT:
                if (targetDeltaTime >= rampTime) {
                    if (remainingPower != 0.0) {
                        rampRate =
                            boundedRampRate(remainingPower, rampRate, rampLimitUp, rampLimitDown);
                        remainingTime = rampCompletionDelay(remainingPower, rampRate);
                        if (!std::isfinite(remainingTime)) {
                            rampRate = 0.0;
                            nextUpdateTime = targetTime;
                        } else if (remainingTime < ((targetDeltaTime - rampTime) / 2.0)) {
                            nextUpdateTime = previousTime + remainingTime;
                        } else {
                            nextUpdateTime = previousTime + ((targetDeltaTime - rampTime) / 2.0);
                        }
                    } else {
                        const double targetSpan = targetTime - lastTargetTime;
                        if ((previousTime - lastTargetTime) >= ((targetSpan - rampTime) / 2.0)) {
                            const CoreTime rampEndTime =
                                lastTargetTime + ((targetSpan - rampTime) / 2.0) + rampTime;
                            if (previousTime < rampEndTime) {
                                rampRate = powerDifference / rampTime;
                                if (rampRate > rampLimitUp) {
                                    rampRate = rampLimitUp;
                                } else if (rampRate < -rampLimitDown) {
                                    rampRate = -rampLimitDown;
                                }
                                nextUpdateTime = rampEndTime;
                            } else {
                                rampRate = boundedRampRate(powerDifference,
                                                           rampRate,
                                                           rampLimitUp,
                                                           rampLimitDown);
                                remainingTime = rampCompletionDelay(powerDifference, rampRate);
                                nextUpdateTime = std::isfinite(remainingTime) ?
                                    previousTime + remainingTime :
                                    targetTime;
                                nextUpdateTime = std::min(targetTime, nextUpdateTime);
                            }
                        } else {
                            rampRate = 0.0;
                            nextUpdateTime = lastTargetTime + ((targetSpan - rampTime) / 2.0);
                        }
                    }
                } else {
                    const double targetSpan = targetTime - lastTargetTime;
                    const CoreTime rampEndTime =
                        lastTargetTime + ((targetSpan - rampTime) / 2.0) + rampTime;
                    if (previousTime >= rampEndTime) {
                        rampRate =
                            boundedRampRate(powerDifference, rampRate, rampLimitUp, rampLimitDown);
                        remainingTime = rampCompletionDelay(powerDifference, rampRate);
                        nextUpdateTime = std::isfinite(remainingTime) ?
                            previousTime + remainingTime :
                            targetTime;
                        nextUpdateTime = std::min(targetTime, nextUpdateTime);
                    } else {
                        nextUpdateTime = targetTime;
                        if (targetDeltaTime == 0.0) {
                            rampRate = (powerDifference > 0.0) ? rampLimitUp : -rampLimitDown;
                        } else {
                            rampRate = powerDifference / targetDeltaTime;
                            if (rampRate > rampLimitUp) {
                                rampRate = rampLimitUp;
                            } else if (rampRate < -rampLimitDown) {
                                rampRate = -rampLimitDown;
                            }
                        }
                    }
                }
                break;
            case SchedulerRamp::DELAYED:
                if (remainingPower != 0.0) {
                    const double rate = (remainingPower > 0.0) ? rampLimitUp : rampLimitDown;
                    remainingTime =
                        (rate > 0.0) ? std::abs(remainingPower) / rate : targetDeltaTime;
                    remainingTime = std::max(remainingTime, rampTime);
                    remainingTime = std::min(remainingTime, targetDeltaTime);
                    rampRate = (remainingTime > 0.0) ? remainingPower / remainingTime : 0.0;
                    if (rampRate > rampLimitUp) {
                        rampRate = rampLimitUp;
                    } else if (rampRate < -rampLimitDown) {
                        rampRate = -rampLimitDown;
                    }
                    nextUpdateTime = previousTime + remainingTime;
                } else {
                    rampRate = 0.0;
                    nextUpdateTime = targetTime;
                }
                break;
            case SchedulerRamp::JUST_IN_TIME: {
                if (std::abs(powerDifference) <= 0.0001) {
                    rampRate = 0.0;
                    nextUpdateTime = targetTime;
                    break;
                }
                const double rateLimit = (powerDifference > 0.0) ? rampLimitUp : rampLimitDown;
                if (rateLimit <= 0.0) {
                    rampRate = 0.0;
                    nextUpdateTime = targetTime;
                    break;
                }
                const double requiredTime = std::abs(powerDifference) / rateLimit;
                const CoreTime rampStartTime = targetTime - requiredTime;
                if (previousTime < rampStartTime) {
                    rampRate = 0.0;
                    nextUpdateTime = rampStartTime;
                } else {
                    rampRate = boundedRampRate(powerDifference,
                                               powerDifference / targetDeltaTime,
                                               rampLimitUp,
                                               rampLimitDown);
                }
                break;
            }
            case SchedulerRamp::ON_TARGET_RAMP:
                rampRate = 0.0;
                nextUpdateTime = targetTime;
                break;
        }
        return {.rampRate = rampRate, .nextUpdateTime = nextUpdateTime};
    }
}  // namespace

SchedulerRamp::SchedulerRamp(const std::string& objName): Scheduler(objName) {}

SchedulerRamp::SchedulerRamp(double initialValue, const std::string& objName):
    Scheduler(initialValue, objName)
{
}

SchedulerRamp::~SchedulerRamp()
{
    if (reserveDispatcher != nullptr) {
        reserveDispatcher->remove(this);
    }
}

CoreObject* SchedulerRamp::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<SchedulerRamp, Scheduler>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->rampUp = rampUp;
    nobj->rampDown = rampDown;
    nobj->pRampCurr = pRampCurr;
    nobj->rampTime = rampTime;
    nobj->dpdt = dpdt;
    nobj->lastTargetTime = lastTargetTime;
    nobj->mode = mode;
    nobj->reserveAvail = reserveAvail;
    nobj->reserveUse = reserveUse;
    nobj->reserveAct = reserveAct;
    nobj->reservePriority = reservePriority;
    nobj->reserveRampTime = reserveRampTime;
    nobj->ramp10Up = ramp10Up;
    nobj->ramp30Up = ramp30Up;
    nobj->ramp10Down = ramp10Down;
    nobj->ramp30Down = ramp30Down;
    nobj->rampCompletionPending = rampCompletionPending;
    nobj->rampCompletionTime = rampCompletionTime;
    nobj->reserveDispatcher = nullptr;

    return nobj;
}

void SchedulerRamp::setTarget(double target)
{
    insertTarget(Tsched(prevTime, target));
}

void SchedulerRamp::setTarget(CoreTime time, double target)
{
    insertTarget(Tsched(time, target));
    if (time == nextUpdateTime) {
        updatePTarget();
    }
}

void SchedulerRamp::updateA(CoreTime time)
{
    double deltaTime = (time - prevTime);

    if (deltaTime == 0) {
        return;
    }

    if (time >= nextUpdateTime) {
        const double originalNextUpdateTime = nextUpdateTime;
        deltaTime = nextUpdateTime - prevTime;
        pCurr = pCurr + (pRampCurr * deltaTime);
        dpdt = getRamp();
        m_output = m_output + (dpdt * deltaTime);
        prevTime = nextUpdateTime;

        updatePTarget();

        deltaTime = time - originalNextUpdateTime;
    }

    pCurr = pCurr + (pRampCurr * deltaTime);
    dpdt = getRamp();
    m_output = m_output + (dpdt * deltaTime);
    reserveAct = m_output - pCurr;
    prevTime = time;
}

double SchedulerRamp::predict(CoreTime time)
{
    const double deltaTime = (time - prevTime);
    if (deltaTime == 0) {
        return m_output;
    }
    const double ramp = getRamp();
    return (m_output + (ramp * deltaTime));
}

void SchedulerRamp::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    Scheduler::dynObjectInitializeA(time0, flags);
    prevTime = time0 - 0.001;
    lastTargetTime = time0 - 0.001;
}

void SchedulerRamp::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    Scheduler::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (reserveAvail > 0) {
        reserveDispatcherLink();
    }
    while (!pTarget.empty()) {
        if ((pTarget.front()).time < prevTime) {
            pTarget.pop_front();
        } else {
            break;
        }
    }
    updatePTarget();
    dpdt = pRampCurr;
}

double SchedulerRamp::getRamp() const
{
    double ramp = pRampCurr;
    const double diff = reserveUse - reserveAct;
    if (diff > 0.001) {
        ramp = getRampLimitUp();
    } else if (diff < -0.001) {
        ramp = -getRampLimitDown();
    }

    return ramp;
}

double SchedulerRamp::getRampTime() const
{
    const double diff = reserveUse - reserveAct;
    if (diff > 0.001) {
        const double rate = getRampLimitUp() - pRampCurr;
        return (rate > 0.0) ? diff / rate : static_cast<double>(kDayLength);
    }
    if (diff < -0.001) {
        const double rate = -getRampLimitDown() - pRampCurr;
        return (rate < 0.0) ? diff / rate : static_cast<double>(kDayLength);
    }

    if (pTarget.empty()) {
        return static_cast<double>(kDayLength);
    }

    return (pTarget.front()).time - prevTime;
}

double SchedulerRamp::getRampLimitUp() const
{
    double limit = std::min({rampUp, ramp10Up / 600.0, ramp30Up / 1800.0});
    if (reserveAvail >= 0.001) {
        limit -= (reserveAvail - reserveUse) / reserveRampTime;
    }
    return std::max(0.0, limit);
}

double SchedulerRamp::getRampLimitDown() const
{
    return std::max(0.0, std::min({rampDown, ramp10Down / 600.0, ramp30Down / 1800.0}));
}

double SchedulerRamp::getMax(const CoreTime /*time*/) const
{
    return pMax - reserveAvail;
}

double SchedulerRamp::getMin(CoreTime /*time*/) const
{
    return pMin;
}

void SchedulerRamp::setReserveTarget(double target)
{
    if (!std::isfinite(target) || target < 0.0) {
        throw InvalidParameterValue("scheduler reserve target must be finite and nonnegative");
    }
    reserveUse = std::clamp(target, 0.0, reserveAvail);
    updatePTarget();
    if (reserveDispatcher != nullptr) {
        reserveDispatcher->schedChange();
    }
}

void SchedulerRamp::set(std::string_view param, std::string_view val)
{
    if (param == "rampmode") {
        const auto modeString = gmlc::utilities::convertToLowerCase(val);
        RampMode newMode;
        if (modeString == "midpoint") {
            newMode = MID_POINT;
        } else if (modeString == "justintime") {
            newMode = JUST_IN_TIME;
        } else if (modeString == "ontargetramp") {
            newMode = ON_TARGET_RAMP;
        } else if (modeString == "delayed") {
            newMode = DELAYED;
        } else if (modeString == "interp") {
            newMode = INTERP;
        } else {
            throw InvalidParameterValue("unsupported scheduler ramp mode");
        }
        mode = newMode;
        rampCompletionPending = false;
        rampCompletionTime = maxTime;
        updatePTarget();
    } else {
        Scheduler::set(param, val);
    }
}

void SchedulerRamp::dispatcherLink()
{
    reserveDispatcherLink();
}

void SchedulerRamp::reserveDispatcherLink()
{
    auto* dispatcher = dynamic_cast<ReserveDispatcher*>(find("reservedispatcher"));
    if (reserveDispatcher != nullptr && reserveDispatcher != dispatcher) {
        reserveDispatcher->remove(this);
    }
    if (dispatcher != nullptr && reserveAvail > 0.0) {
        dispatcher->add(this);
    } else if (reserveDispatcher != nullptr) {
        reserveDispatcher->remove(this);
    }
}

void SchedulerRamp::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("scheduler ramp parameters must be finite");
    }

    if (param == "ramp") {
        const double rate = units::convert(val, unitType, units::puMW / units::s, m_Base);
        if (!std::isfinite(rate) || rate < 0.0) {
            throw InvalidParameterValue("scheduler ramp rates must be nonnegative");
        }
        rampUp = rate;
        rampDown = rampUp;
    } else if (param == "rampup") {
        const double rate = units::convert(val, unitType, units::puMW / units::s, m_Base);
        if (!std::isfinite(rate) || rate < 0.0) {
            throw InvalidParameterValue("scheduler ramp rates must be nonnegative");
        }
        rampUp = rate;
    } else if (param == "rampdown") {
        const double rate = units::convert(val, unitType, units::puMW / units::s, m_Base);
        if (!std::isfinite(rate) || rate < 0.0) {
            throw InvalidParameterValue("scheduler ramp rates must be nonnegative");
        }
        rampDown = rate;
    } else if (param == "ramp10") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0) {
            throw InvalidParameterValue("scheduler 10-minute ramp limits must be nonnegative");
        }
        ramp10Up = amount;
        ramp10Down = amount;
    } else if (param == "ramp10up") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0) {
            throw InvalidParameterValue("scheduler 10-minute ramp limits must be nonnegative");
        }
        ramp10Up = amount;
    } else if (param == "ramp10down") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0) {
            throw InvalidParameterValue("scheduler 10-minute ramp limits must be nonnegative");
        }
        ramp10Down = amount;
    } else if (param == "ramp30") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0) {
            throw InvalidParameterValue("scheduler 30-minute ramp limits must be nonnegative");
        }
        ramp30Up = amount;
        ramp30Down = amount;
    } else if (param == "ramp30up") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0) {
            throw InvalidParameterValue("scheduler 30-minute ramp limits must be nonnegative");
        }
        ramp30Up = amount;
    } else if (param == "ramp30down") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0) {
            throw InvalidParameterValue("scheduler 30-minute ramp limits must be nonnegative");
        }
        ramp30Down = amount;
    } else if (param == "ramptime") {
        const double seconds =
            units::convert(val, unitType == units::defunit ? units::s : unitType, units::s);
        if (!std::isfinite(seconds) || seconds <= 0.0) {
            throw InvalidParameterValue("scheduler ramp time must be positive");
        }
        rampTime = seconds;
    } else if (param == "reserve") {
        const double amount = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(amount) || amount < 0.0 || amount > pMax - pMin) {
            throw InvalidParameterValue(
                "scheduler reserve must be nonnegative and fit within its power range");
        }
        reserveAvail = amount;
        reserveUse = std::min(reserveUse, reserveAvail);
        pCurr = std::clamp(pCurr, pMin, pMax - reserveAvail);
    } else if (param == "reserveramptime") {
        const double seconds =
            units::convert(val, unitType == units::defunit ? units::s : unitType, units::s);
        if (!std::isfinite(seconds) || seconds <= 0.0) {
            throw InvalidParameterValue("scheduler reserve ramp time must be positive");
        }
        reserveRampTime = seconds;
    } else if ((param == "max") || (param == "min")) {
        const double bound = units::convert(val, unitType, units::puMW, m_Base);
        if (!std::isfinite(bound) || (param == "max" && bound - reserveAvail < pMin) ||
            (param == "min" && bound > pMax - reserveAvail)) {
            throw InvalidParameterValue("scheduler power bounds conflict with reserve");
        }
        Scheduler::set(param, val, unitType);
    } else {
        Scheduler::set(param, val, unitType);
    }
    updatePTarget();
    if (param == "reserve") {
        reserveDispatcherLink();
        if (reserveDispatcher != nullptr) {
            reserveDispatcher->schedChange();
        }
    }
}

void SchedulerRamp::setTarget(const std::string& fileName)
{
    Scheduler::setTarget(fileName);
    updatePTarget();
}

// NOLINTNEXTLINE(misc-no-recursion)
void SchedulerRamp::updatePTarget()
{
    double rempower = 0.0;
    double remtime = 0.0;
    double target;
    CoreTime time;
    const double rampLimitUp = getRampLimitUp();
    const double rampLimitDown = getRampLimitDown();

    if (rampCompletionPending) {
        if (prevTime >= rampCompletionTime) {
            rampCompletionPending = false;
            rampCompletionTime = maxTime;
            pRampCurr = 0.0;
            nextUpdateTime = pTarget.empty() ? maxTime : pTarget.front().time;
            return;
        }
        if (pTarget.empty() || pTarget.front().time > prevTime) {
            nextUpdateTime = pTarget.empty() ? rampCompletionTime :
                                               std::min(rampCompletionTime, pTarget.front().time);
            return;
        }
        // A newly due target replaces the target whose ramp was in progress.
        rampCompletionPending = false;
        rampCompletionTime = maxTime;
    }
    if (pTarget.empty()) {
        pRampCurr = 0;
        nextUpdateTime = maxTime;
        return;
    }

    target = (pTarget.front()).target;
    time = (pTarget.front()).time;
    if (target > (pMax - reserveAvail)) {
        target = (pMax - reserveAvail);
    } else if (target < pMin) {
        target = pMin;
    }

    if (time <= prevTime) {
        // get rid of first element
        pTarget.pop_front();
        rempower = target - pCurr;
        // ignore small variations
        if ((rempower <= 0.0001) && (rempower >= -0.0001)) {
            rempower = 0.0;
        }
        lastTargetTime = time;
        if (mode == ON_TARGET_RAMP) {
            if (rempower == 0.0) {
                pRampCurr = 0.0;
                nextUpdateTime = pTarget.empty() ? maxTime : pTarget.front().time;
                return;
            }
            pRampCurr = (rempower > 0.0) ? rampLimitUp : -rampLimitDown;
            remtime = rampCompletionDelay(rempower, pRampCurr);
            if (std::isfinite(remtime)) {
                rampCompletionPending = true;
                rampCompletionTime = prevTime + remtime;
                nextUpdateTime = pTarget.empty() ?
                    rampCompletionTime :
                    std::min(rampCompletionTime, pTarget.front().time);
            } else {
                pRampCurr = 0.0;
                nextUpdateTime = pTarget.empty() ? maxTime : pTarget.front().time;
            }
            return;
        }
        while (!pTarget.empty() && pTarget.front().time <= prevTime) {
            target = pTarget.front().target;
            time = pTarget.front().time;
            pTarget.pop_front();
            target = std::clamp(target, pMin, pMax - reserveAvail);
            rempower = target - pCurr;
            if (std::abs(rempower) <= 0.0001) {
                rempower = 0.0;
            }
            lastTargetTime = time;
        }
        if (!pTarget.empty()) {
            target = (pTarget.front()).target;
            time = (pTarget.front()).time;
            target = std::clamp(target, pMin, pMax - reserveAvail);
        } else {
            if (rempower != 0.0) {
                // Continue the ramp to the final requested target.
                if (rempower * pRampCurr <= 0.0) {
                    pRampCurr = (rempower > 0.0) ? rampLimitUp : -rampLimitDown;
                }
                remtime = rampCompletionDelay(rempower, pRampCurr);
                if (std::isfinite(remtime)) {
                    const CoreTime completionTime = prevTime + remtime;
                    insertTarget(Tsched(completionTime, target));
                    nextUpdateTime = completionTime;
                } else {
                    pRampCurr = 0.0;
                    nextUpdateTime = maxTime;
                }
            } else {
                pRampCurr = 0;
                nextUpdateTime = maxTime;
            }
            return;
        }
    }
    const double targetDeltaTime = (time - prevTime);
    const double powerDifference = target - pCurr;
    if (targetDeltaTime <= 0.0) {
        pRampCurr = 0.0;
        nextUpdateTime = maxTime;
        return;
    }
    if (rempower == 0.0) {
        if ((powerDifference < 0.0001) && (powerDifference > -0.0001)) {
            pRampCurr = 0;
            nextUpdateTime = time;
            return;
        }
    }

    const auto rampUpdate = calculateRampTargetUpdate(mode,
                                                      rempower,
                                                      powerDifference,
                                                      targetDeltaTime,
                                                      prevTime,
                                                      time,
                                                      lastTargetTime,
                                                      rampTime,
                                                      pRampCurr,
                                                      rampLimitUp,
                                                      rampLimitDown);
    pRampCurr = rampUpdate.rampRate;
    nextUpdateTime = rampUpdate.nextUpdateTime;
}

// NOLINTNEXTLINE(misc-no-recursion)
void SchedulerRamp::insertTarget(Tsched targetSchedule)
{
    Scheduler::insertTarget(targetSchedule);
    if (nextUpdateTime == targetSchedule.time) {
        updatePTarget();
    }
}

double SchedulerRamp::get(std::string_view param, units::unit unitType) const
{
    if (param == "reserve") {
        return units::convert(reserveAvail, units::puMW, unitType, m_Base);
    }
    if (param == "ramp") {
        return units::convert(rampUp, units::puMW / units::s, unitType, m_Base);
    }
    if (param == "rampup") {
        return units::convert(rampUp, units::puMW / units::s, unitType, m_Base);
    }
    if (param == "rampdown") {
        return units::convert(rampDown, units::puMW / units::s, unitType, m_Base);
    }
    if (param == "ramp10") {
        return units::convert(std::min(ramp10Up, ramp10Down), units::puMW, unitType, m_Base);
    }
    if (param == "ramp10up") {
        return units::convert(ramp10Up, units::puMW, unitType, m_Base);
    }
    if (param == "ramp10down") {
        return units::convert(ramp10Down, units::puMW, unitType, m_Base);
    }
    if (param == "ramp30") {
        return units::convert(std::min(ramp30Up, ramp30Down), units::puMW, unitType, m_Base);
    }
    if (param == "ramp30up") {
        return units::convert(ramp30Up, units::puMW, unitType, m_Base);
    }
    if (param == "ramp30down") {
        return units::convert(ramp30Down, units::puMW, unitType, m_Base);
    }
    if (param == "ramptime") {
        return units::convert(rampTime, units::s, unitType);
    }
    if (param == "reserveramptime") {
        return units::convert(reserveRampTime, units::s, unitType);
    }
    return Scheduler::get(param, unitType);
}

void SchedulerRamp::receiveMessage(std::uint64_t sourceID,
                                   const std::shared_ptr<CommMessage>& message)
{
    using comms::SchedulerMessagePayload;
    // auto sm = std::dynamic_pointer_cast<schedulerMessage> (message);
    switch (message->getMessageType()) {
        case SchedulerMessagePayload::CLEAR_TARGETS:
            clearSchedule();
            break;
        case SchedulerMessagePayload::SHUTDOWN:
        case SchedulerMessagePayload::STARTUP:
        case SchedulerMessagePayload::UPDATE_TARGETS:
        case SchedulerMessagePayload::UPDATE_RESERVES:
        case SchedulerMessagePayload::USE_RESERVE:
            break;
        default:
            Scheduler::receiveMessage(sourceID, message);
            break;
    }
}

/*
void schedulerRamp::reserveDispatcherLink(reserveDispatcher *rD)
{
        if (rD==nullptr)
        {
                resDispatch=(reserveDispatcher *)find("reservedispatcher");
                if (resDispatch!=nullptr)
                {
                        resDispatch->addGen(this);
                }
        }
        else
        {
                if (resDispatch==nullptr)
                {
                        resDispatch=rD;
                }
                else
                {
                        if (resDispatch!=rD)
                        {
                                resDispatch->removeSched(this);
                                resDispatch=rD;
                        }
                }
        }
}
*/
}  // namespace griddyn
