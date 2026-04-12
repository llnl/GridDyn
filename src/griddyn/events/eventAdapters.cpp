/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "eventAdapters.h"

#include "../Relay.h"
#include "Event.h"
#include "core/coreObject.h"
#include <cmath>
#include <memory>
#include <typeinfo>
#include <utility>

namespace griddyn {
std::atomic<id_type_t> eventAdapter::eventCounter(0);

eventAdapter::eventAdapter(coreTime nextTime, coreTime period):
    m_period(period), m_nextTime(nextTime)
{
    eventID = ++eventCounter;
}

eventAdapter::~eventAdapter() = default;

std::unique_ptr<eventAdapter> eventAdapter::clone() const
{
    auto ea = std::make_unique<eventAdapter>();
    eventAdapter::cloneTo(ea.get());
    return ea;
}
void eventAdapter::cloneTo(eventAdapter* eA) const
{
    eA->m_remove_event = m_remove_event;
    eA->partBdelay = partBdelay;
    eA->two_part_execute = two_part_execute;
    eA->partB_turn = partB_turn;
    eA->partB_only = partB_only;
    eA->m_period = m_period;
    eA->m_nextTime = m_nextTime;
}

void eventAdapter::updateObject(coreObject* /*newObject*/, object_update_mode /*mode*/) {}

void eventAdapter::getObjects(std::vector<coreObject*>& /*objects*/) const {}
void eventAdapter::executeA(coreTime /*cTime*/) {}

void eventAdapter::updateTime() {}
void eventAdapter::initialize() {}

int eventAdapter::eventCode() const
{
    return 0;
}

change_code eventAdapter::execute(coreTime cTime)
{
    if (m_period > timeZero) {
        m_nextTime += std::floor((cTime - m_nextTime) / m_period) * m_period + m_period;
    } else {
        m_nextTime = maxTime;
    }
    return change_code::no_change;
}

bool compareEventAdapters(const std::shared_ptr<eventAdapter>& e1,
                          const std::shared_ptr<eventAdapter>& e2)
{
    return (e1->m_nextTime < e2->m_nextTime);
}

functionEventAdapter::functionEventAdapter(ccode_function_t fcal): fptr(std::move(fcal)) {}
functionEventAdapter::functionEventAdapter(ccode_function_t fcal,
                                           coreTime triggerTime,
                                           coreTime period):
    eventAdapter(triggerTime, period), fptr(std::move(fcal))
{
}

void functionEventAdapter::cloneTo(eventAdapter* ea) const
{
    eventAdapter::cloneTo(ea);
    auto fea = dynamic_cast<functionEventAdapter*>(ea);
    if (ea == nullptr) {
        return;
    }
    fea->fptr = fptr;
    fea->evCode_ = evCode_;
}

std::unique_ptr<eventAdapter> functionEventAdapter::clone() const
{
    std::unique_ptr<eventAdapter> ea = std::make_unique<functionEventAdapter>();
    functionEventAdapter::cloneTo(ea.get());
    return ea;
}

change_code functionEventAdapter::execute(coreTime cTime)
{
    auto retval = fptr();
    if (m_period > timeZero) {
        m_nextTime += std::floor((cTime - m_nextTime) / m_period) * m_period + m_period;
    } else {
        m_remove_event = true;
    }
    return retval;
}

void functionEventAdapter::setfunction(ccode_function_t nfptr)
{
    fptr = std::move(nfptr);
}

void functionEventAdapter::setExecutionMode(event_execution_mode newMode)
{
    switch (newMode) {
        case event_execution_mode::normal:

            two_part_execute = false;
            partB_only = false;
            break;
            /** this one really shouldn't be used as it has no meaning*/
        case event_execution_mode::two_part_execution:
            two_part_execute = true;
            partB_only = true;
            break;
        case event_execution_mode::delayed:
            two_part_execute = true;
            partB_only = true;
            break;
        default:
            break;
    }
}

}  // namespace griddyn
