/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Scheduler.h"

#include "../comms/Communicator.h"
#include "../comms/SchedulerMessage.h"
#include "Dispatcher.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/TimeSeries.hpp"
#include "griddyn/griddyn-config.h"
#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using units::convert;
using units::MW;
using units::puMW;
using units::unit;

// operator overloads for Tsched object
bool operator<(const Tsched& td1, const Tsched& td2)
{
    return (td1.time < td2.time);
}
bool operator<=(const Tsched& td1, const Tsched& td2)
{
    return (td1.time <= td2.time);
}
bool operator>(const Tsched& td1, const Tsched& td2)
{
    return (td1.time > td2.time);
}
bool operator>=(const Tsched& td1, const Tsched& td2)
{
    return (td1.time >= td2.time);
}
bool operator==(const Tsched& td1, const Tsched& td2)
{
    return (td1.time == td2.time);
}
bool operator!=(const Tsched& td1, const Tsched& td2)
{
    return (td1.time != td2.time);
}
bool operator<(const Tsched& td1, CoreTime timeC)
{
    return (td1.time < timeC);
}
bool operator<=(const Tsched& td1, CoreTime timeC)
{
    return (td1.time <= timeC);
}
bool operator>(const Tsched& td1, CoreTime timeC)
{
    return (td1.time > timeC);
}
bool operator>=(const Tsched& td1, CoreTime timeC)
{
    return (td1.time >= timeC);
}
bool operator==(const Tsched& td1, CoreTime timeC)
{
    return (td1.time == timeC);
}
bool operator!=(const Tsched& td1, CoreTime timeC)
{
    return (td1.time != timeC);
}

Scheduler::Scheduler(const std::string& objName, double initialValue):
    Source(objName, initialValue), pCurr(initialValue)
{
    prevTime = negTime;  // override default setting
}

Scheduler::Scheduler(double initialValue, const std::string& objName):
    Scheduler(objName, initialValue)
{
}

CoreObject* Scheduler::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<Scheduler, Source>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->pMax = pMax;
    nobj->pMin = pMin;
    nobj->pTarget = pTarget;
    nobj->m_Base = m_Base;

    return nobj;
}

Scheduler::~Scheduler()
{
    clearSchedule();
}

void Scheduler::setTarget(double target)
{
    insertTarget(Tsched(prevTime, target));
}

void Scheduler::setTarget(CoreTime time, double target)
{
    insertTarget(Tsched(time, target));
}

void Scheduler::setTarget(std::vector<double>& time, std::vector<double>& target)
{
    auto timeIter = time.begin();
    auto targetIter = target.begin();
    const auto timeEnd = time.end();
    const auto targetEnd = target.end();
    while ((timeIter != timeEnd) && (targetIter != targetEnd)) {
        pTarget.emplace_back(*timeIter, *targetIter);
        ++timeIter;
        ++targetIter;
    }
    pTarget.sort();
    if (pTarget.front().time != nextUpdateTime) {
        nextUpdateTime = (pTarget.front()).time;
        alert(this, UPDATE_TIME_CHANGE);
    }
}

void Scheduler::setTarget(const std::string& fileName)
{
    gmlc::utilities::TimeSeries<double, CoreTime> targets;
    targets.loadFile(fileName);

    auto targetList = [&targets]() {
        std::list<Tsched> loadedTargets;
        const auto targetCount = static_cast<int>(targets.size());
        for (int index = 0; index < targetCount; ++index) {
            loadedTargets.emplace_back(targets.time(index), targets.data(index));
        }
        loadedTargets.sort();
        return loadedTargets;
    }();
    pTarget.merge(targetList);
    if (pTarget.front().time != nextUpdateTime) {
        nextUpdateTime = (pTarget.front()).time;
        alert(this, UPDATE_TIME_CHANGE);
    }
}

void Scheduler::updateA(CoreTime time)
{
    const auto deltaTime = (time - prevTime);
    if (deltaTime == timeZero) {
        return;
    }
    if (time >= nextUpdateTime) {
        while (time >= pTarget.front().time) {
            pCurr = (pTarget.front()).target;
            pCurr = std::clamp(pCurr, pMin, pMax);

            pTarget.pop_front();
            if (pTarget.empty()) {
                nextUpdateTime = maxTime;

                break;
            }
            nextUpdateTime = (pTarget.front()).time;
        }
    }
    m_output = pCurr;
    prevTime = time;
    lastUpdateTime = time;
}

double Scheduler::predict(CoreTime time)
{
    double out = m_output;
    if (time >= nextUpdateTime) {
        out = (pTarget.front()).target;
        out = std::clamp(out, pMin, pMax);
    }
    return out;
}

void Scheduler::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    commLink = cManager.build();

    commLink->registerReceiveCallback(
        [this](std::uint64_t sourceID, const std::shared_ptr<CommMessage>& message) {
            receiveMessage(sourceID, message);
        });
    prevTime = time0;
}

void Scheduler::dynObjectInitializeB(const IOdata& /*inputs*/,
                                     const IOdata& desiredOutput,
                                     IOdata& /*fieldSet*/)
{
    if (desiredOutput[0] > pMax) {
        pMax = desiredOutput[0];
    } else if (desiredOutput[0] < pMin) {
        pMin = desiredOutput[0];
    }

    // try to register to a dispatcher

    pCurr = desiredOutput[0];
    m_output = pCurr;
}

double Scheduler::getTarget() const
{
    return (pTarget.empty()) ? pCurr : (pTarget.front()).target;
}

double Scheduler::getMax(CoreTime /*time*/) const
{
    return pMax;
}

double Scheduler::getMin(CoreTime /*time*/) const
{
    return pMin;
}

void Scheduler::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        if (!cManager.set(param, val)) {
            Source::set(param, val);
        }
    }
}

void Scheduler::set(std::string_view param, double val, unit unitType)
{
    if (param == "min") {
        pMin = convert(val, unitType, puMW, m_Base);
        pCurr = std::clamp(pCurr, pMin, pMax);
    } else if (param == "max") {
        pMax = convert(val, unitType, puMW, m_Base);
        pCurr = std::clamp(pCurr, pMin, pMax);
    } else if (param == "base") {
        m_Base = convert(val, unitType, MW, systemBasePower);
    } else if (param == "target") {
        setTarget(convert(val, unitType, puMW, m_Base));
    } else {
        if (!cManager.set(param, val)) {
            Source::set(param, val, unitType);
        }
    }
}

void Scheduler::setFlag(std::string_view flag, bool val)
{
    if (!cManager.setFlag(flag, val)) {
        Source::setFlag(flag, val);
    }
}

double Scheduler::get(std::string_view param, unit unitType) const
{
    double val = kNullVal;
    if (param == "min") {
        val = convert(pMin, puMW, unitType, m_Base);
    } else if (param == "max") {
        val = convert(pMax, puMW, unitType, m_Base);
    } else {
        val = Source::get(param, unitType);
    }
    return val;
}

void Scheduler::clearSchedule()
{
    if (!pTarget.empty()) {
        pTarget.resize(0);
        nextUpdateTime = maxTime;
        alert(this, UPDATE_TIME_CHANGE);
    }
}

void Scheduler::insertTarget(Tsched targetSchedule)
{
    if (targetSchedule < nextUpdateTime) {
        pTarget.push_front(targetSchedule);
        nextUpdateTime = targetSchedule.time;
        alert(this, UPDATE_TIME_CHANGE);
    } else {
        pTarget.push_back(targetSchedule);
        pTarget.sort();
    }
}

void Scheduler::receiveMessage(std::uint64_t sourceID, const std::shared_ptr<CommMessage>& message)
{
    using comms::SchedulerMessagePayload;
    auto* schedulerPayload = message->getPayload<SchedulerMessagePayload>();
    switch (message->getMessageType()) {
        case SchedulerMessagePayload::CLEAR_TARGETS:
            clearSchedule();
            break;
        case SchedulerMessagePayload::SHUTDOWN:
        case SchedulerMessagePayload::STARTUP:
            break;
        case SchedulerMessagePayload::UPDATE_TARGETS:
            clearSchedule();
            [[fallthrough]];
        case SchedulerMessagePayload::ADD_TARGETS:
            setTarget(schedulerPayload->m_time, schedulerPayload->m_target);
            break;
        case SchedulerMessagePayload::REGISTER_DISPATCHER:
            dispatcherId = sourceID;
            break;
        default:
            break;
    }
}

void Scheduler::dispatcherLink()
{
    auto* dispatch = static_cast<Dispatcher*>(getParent()->find("dispatcher"));
    if (dispatch != nullptr) {
        dispatch->add(this);
    }
}

}  // namespace griddyn
