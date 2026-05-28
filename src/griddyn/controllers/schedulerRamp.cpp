/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../comms/SchedulerMessage.h"
#include "AGControl.h"
#include "ReserveDispatcher.h"
#include "Scheduler.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <memory>
#include <string>

namespace griddyn {
schedulerRamp::schedulerRamp(const std::string& objName): scheduler(objName) {}

schedulerRamp::schedulerRamp(double initialValue, const std::string& objName):
    scheduler(initialValue, objName)
{
}

CoreObject* schedulerRamp::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<schedulerRamp, scheduler>(this, obj);
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

    return nobj;
}

void schedulerRamp::setTarget(double target)
{
    insertTarget(tsched(prevTime, target));
}

void schedulerRamp::setTarget(coreTime time, double target)
{
    insertTarget(tsched(time, target));
    if (time == nextUpdateTime) {
        updatePTarget();
    }
}

void schedulerRamp::updateA(coreTime time)
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

double schedulerRamp::predict(coreTime time)
{
    const double deltaTime = (time - prevTime);
    if (deltaTime == 0) {
        return m_output;
    }
    const double ramp = getRamp();
    return (m_output + (ramp * deltaTime));
}

void schedulerRamp::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    scheduler::dynObjectInitializeA(time0, flags);
    prevTime = time0 - 0.001;
    lastTargetTime = time0 - 0.001;
}

void schedulerRamp::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    scheduler::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (reserveAvail > 0) {
        //  if (resDispatch==nullptr)
        //  {
        //      reserveDispatcherLink();
        //  }
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

double schedulerRamp::getRamp() const
{
    double ramp = pRampCurr;
    const double diff = reserveUse - reserveAct;
    if (diff > 0.001) {
        ramp = rampUp;
    } else if (diff < -0.001) {
        ramp = -rampDown;
    }

    return ramp;
}

double schedulerRamp::getRampTime() const
{
    const double diff = reserveUse - reserveAct;
    if (diff > 0.001) {
        return diff / (rampUp - pRampCurr);
    }
    if (diff < -0.001) {
        return diff / (-rampDown - pRampCurr);
    }

    if (pTarget.empty()) {
        return static_cast<double>(kDayLength);
    }

    return (pTarget.front()).time - prevTime;
}

double schedulerRamp::getMax(const coreTime /*time*/) const
{
    return pMax;
}

double schedulerRamp::getMin(coreTime /*time*/) const
{
    return pMin;
}

void schedulerRamp::setReserveTarget(double target)
{
    if (target <= reserveAvail) {
        reserveUse = target;
    } else {
        reserveUse = reserveAvail;
    }
}

void schedulerRamp::set(std::string_view param, std::string_view val)
{
    if (param == "rampmode") {
        const auto modeString = gmlc::utilities::convertToLowerCase(val);
        if (modeString == "midpoint") {
            mode = MID_POINT;
        } else if (modeString == "justInTime") {
            mode = JUST_IN_TIME;
        } else if (modeString == "ontargetramp") {
            mode = ON_TARGET_RAMP;
        } else if (modeString == "delayed") {
            mode = DELAYED;
        } else if (modeString == "interp") {
            mode = INTERP;
        }
    } else {
        scheduler::set(param, val);
    }
}

void schedulerRamp::dispatcherLink() {}

void schedulerRamp::set(std::string_view param, double val, units::unit unitType)
{
    double temp;
    if (param == "ramp") {
        rampUp = units::convert(val, unitType, units::puMW / units::s, m_Base);
        rampDown = rampUp;
    } else if (param == "rampup") {
        rampUp = val;
    } else if (param == "rampdown") {
        rampDown = val;
        if (rampDown < 0) {
            rampDown = -rampDown;
        }
    }
    if (param == "ramp10") {
        temp = val;
        ramp10Up = temp;
        ramp10Down = temp;
    } else if (param == "ramp10up") {
        ramp10Up = val;
    } else if (param == "ramp10down") {
        ramp10Down = val;
        if (ramp10Down < 0) {
            ramp10Down = -ramp10Down;
        }
    }

    if (param == "ramp30") {
        temp = val;
        ramp30Up = temp;
        ramp30Down = temp;
    } else if (param == "ramp30up") {
        ramp30Up = val;
    } else if (param == "ramp30down") {
        ramp30Down = val;
        if (ramp30Down < 0) {
            ramp30Down = -ramp30Down;
        }
    } else if (param == "ramptime") {
        rampTime = val;
    } else if (param == "target") {
        setTarget(val);
    } else if (param == "reserve") {
        temp = val;
        // check to updateP the reservedispatcher
        if (temp != reserveAvail) {
            /*
            if (reserveAvail==0)
            {
                reserveAvail=temp;
                if (resDispatch==nullptr)
                {
                    reserveDispatcherLink(nullptr);
                }
            }
            else
            {
                reserveAvail=temp;
                if (resDispatch!=nullptr)
                {
                    resDispatch->schedChange();
                }
            }
            */
        }
    } else if (param == "reserveramptime") {
        reserveRampTime = val;
    } else {
        scheduler::set(param, val);
    }
    updatePTarget();
}

void schedulerRamp::setTarget(const std::string& fileName)
{
    scheduler::setTarget(fileName);
    updatePTarget();
}

// NOLINTNEXTLINE(misc-no-recursion)
void schedulerRamp::updatePTarget()
{
    double rempower = 0.0;
    double remtime = 0.0;
    double target;
    coreTime time;
    double targetSpan;
    double rampLimitUp;

    if (reserveAvail < 0.001) {
        rampLimitUp = rampUp;
    } else if (reserveUse == 0) {
        rampLimitUp = rampUp - reserveAvail / reserveRampTime;
    } else {
        rampLimitUp = rampUp - (reserveAvail - reserveUse) / reserveRampTime;
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
        if (pTarget.empty()) {
            target = (pTarget.front()).target;
            time = (pTarget.front()).time;
            if (target > (pMax - reserveAvail)) {
                target = (pMax - reserveAvail);
            } else if (target < pMin) {
                target = pMin;
            }
        } else {
            if (rempower != 0.0) {
                // assume we were ramp limited so just keep ramping
                remtime = rempower / pRampCurr;
                insertTarget(tsched(target, prevTime + remtime));
                nextUpdateTime = prevTime + remtime;
            } else {
                pRampCurr = 0;
                nextUpdateTime = maxTime;
            }
            return;
        }
    }
    const double targetDeltaTime = (time - prevTime);
    const double powerDifference = target - pCurr;
    if (rempower == 0.0) {
        if ((powerDifference < 0.0001) && (powerDifference > -0.0001)) {
            pRampCurr = 0;
            nextUpdateTime = time;
            return;
        }
    }

    switch (mode) {
        case INTERP:
            nextUpdateTime = time;
            pRampCurr = powerDifference / targetDeltaTime;
            if (pRampCurr > rampLimitUp) {
                pRampCurr = rampLimitUp;
            } else if (pRampCurr < -rampDown) {
                pRampCurr = -rampDown;
            }
            break;
        case MID_POINT:
            if (targetDeltaTime >= rampTime) {
                if (rempower != 0.0) {
                    /*keep ramp until we would begin ramping for the next target*/
                    remtime = rempower / pRampCurr;
                    if (remtime < ((targetDeltaTime - rampTime) / 2.0)) {
                        nextUpdateTime = prevTime + remtime;
                    } else {
                        nextUpdateTime = prevTime + ((targetDeltaTime - rampTime) / 2.0);
                    }
                } else {
                    targetSpan = time - lastTargetTime;
                    if ((prevTime - lastTargetTime) >= (targetSpan - rampTime) / 2.0) {
                        if (prevTime <
                            (lastTargetTime + (targetSpan - rampTime) / 2.0 + rampTime)) {
                            pRampCurr = powerDifference / rampTime;
                            if (pRampCurr > rampLimitUp) {
                                pRampCurr = rampLimitUp;
                            } else if (pRampCurr < -rampDown) {
                                pRampCurr = -rampDown;
                            }
                            nextUpdateTime =
                                lastTargetTime + (targetSpan - rampTime) / 2.0 + rampTime;
                        } else {
                            remtime = powerDifference / pRampCurr;
                            nextUpdateTime = prevTime + remtime;
                            if (time < nextUpdateTime) {
                                nextUpdateTime = time;
                            }
                        }
                    } else {
                        pRampCurr = 0;
                        nextUpdateTime = lastTargetTime + (targetSpan - rampTime) / 2.0;
                    }
                }
            } else {
                targetSpan = time - lastTargetTime;
                if (prevTime >= (lastTargetTime + (targetSpan - rampTime) / 2.0 + rampTime)) {
                    remtime = powerDifference / pRampCurr;
                    nextUpdateTime = prevTime + remtime;
                    if (time < nextUpdateTime) {
                        nextUpdateTime = time;
                    }
                } else {
                    nextUpdateTime = time;
                    if (targetDeltaTime == 0) {
                        if (powerDifference > 0) {
                            pRampCurr = rampLimitUp;
                        } else {
                            pRampCurr = -rampDown;
                        }
                    } else {
                        pRampCurr = powerDifference / targetDeltaTime;
                        if (pRampCurr > rampLimitUp) {
                            pRampCurr = rampLimitUp;
                        } else if (pRampCurr < -rampDown) {
                            pRampCurr = -rampDown;
                        }
                    }
                }
            }
            break;
        case DELAYED:
            if (rempower != 0.0) {
                if (rempower > 0.0) {
                    remtime = rempower / rampLimitUp;
                } else {
                    remtime = rempower / rampDown;
                }
                if (remtime < rampTime) {
                    remtime = rampTime;
                }
                remtime = std::min(remtime, targetDeltaTime);
                pRampCurr = rempower / remtime;
                if (pRampCurr > rampLimitUp) {
                    pRampCurr = rampLimitUp;
                } else if (pRampCurr < -rampDown) {
                    pRampCurr = -rampDown;
                }
                nextUpdateTime = prevTime + remtime;
            } else {
                pRampCurr = 0;
                nextUpdateTime = time;
            }
            break;
        case JUST_IN_TIME:
        case ON_TARGET_RAMP:
            break;
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void schedulerRamp::insertTarget(tsched targetSchedule)
{
    scheduler::insertTarget(targetSchedule);
    if (nextUpdateTime == targetSchedule.time) {
        updatePTarget();
    }
}

double schedulerRamp::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "reserve") {
        val = reserveAvail;
    } else {
        val = scheduler::get(param, unitType);
    }
    return val;
}

void schedulerRamp::receiveMessage(std::uint64_t sourceID,
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
            scheduler::receiveMessage(sourceID, message);
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
