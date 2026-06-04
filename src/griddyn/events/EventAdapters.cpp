/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "EventAdapters.h"

#include "../Relay.h"
#include "Event.h"
#include "core/CoreObject.h"
#include <cmath>
#include <memory>
#include <typeinfo>
#include <utility>

namespace griddyn {
std::atomic<id_type_t> EventAdapter::eventCounter(0);

EventAdapter::EventAdapter(CoreTime nextTime, CoreTime period):
    m_period(period), m_nextTime(nextTime)
{
    eventID = ++eventCounter;
}

EventAdapter::~EventAdapter() = default;

std::unique_ptr<EventAdapter> EventAdapter::clone() const
{
    auto ea = std::make_unique<EventAdapter>();
    EventAdapter::cloneTo(ea.get());
    return ea;
}
void EventAdapter::cloneTo(EventAdapter* eA) const
{
    eA->m_remove_event = m_remove_event;
    eA->partBdelay = partBdelay;
    eA->two_part_execute = two_part_execute;
    eA->partB_turn = partB_turn;
    eA->partB_only = partB_only;
    eA->m_period = m_period;
    eA->m_nextTime = m_nextTime;
}

void EventAdapter::updateObject(CoreObject* /*newObject*/, ObjectUpdateMode /*mode*/) {}

void EventAdapter::getObjects(std::vector<CoreObject*>& /*objects*/) const {}
void EventAdapter::executeA(CoreTime /*cTime*/) {}

void EventAdapter::updateTime() {}
void EventAdapter::initialize() {}

int EventAdapter::eventCode() const
{
    return 0;
}

ChangeCode EventAdapter::execute(CoreTime cTime)
{
    if (m_period > timeZero) {
        m_nextTime += std::floor((cTime - m_nextTime) / m_period) * m_period + m_period;
    } else {
        m_nextTime = maxTime;
    }
    return ChangeCode::NO_CHANGE;
}

bool compareEventAdapters(const std::shared_ptr<EventAdapter>& e1,
                          const std::shared_ptr<EventAdapter>& e2)
{
    return (e1->m_nextTime < e2->m_nextTime);
}

FunctionEventAdapter::FunctionEventAdapter(ccode_function_t fcal): fptr(std::move(fcal)) {}
FunctionEventAdapter::FunctionEventAdapter(ccode_function_t fcal,
                                           CoreTime triggerTime,
                                           CoreTime period):
    EventAdapter(triggerTime, period), fptr(std::move(fcal))
{
}

void FunctionEventAdapter::cloneTo(EventAdapter* ea) const
{
    EventAdapter::cloneTo(ea);
    auto fea = dynamic_cast<FunctionEventAdapter*>(ea);
    if (ea == nullptr) {
        return;
    }
    fea->fptr = fptr;
    fea->evCode_ = evCode_;
}

std::unique_ptr<EventAdapter> FunctionEventAdapter::clone() const
{
    std::unique_ptr<EventAdapter> ea = std::make_unique<FunctionEventAdapter>();
    FunctionEventAdapter::cloneTo(ea.get());
    return ea;
}

ChangeCode FunctionEventAdapter::execute(CoreTime cTime)
{
    auto retval = fptr();
    if (m_period > timeZero) {
        m_nextTime += std::floor((cTime - m_nextTime) / m_period) * m_period + m_period;
    } else {
        m_remove_event = true;
    }
    return retval;
}

void FunctionEventAdapter::setfunction(ccode_function_t nfptr)
{
    fptr = std::move(nfptr);
}

void FunctionEventAdapter::setExecutionMode(EventExecutionMode newMode)
{
    switch (newMode) {
        case EventExecutionMode::NORMAL:

            two_part_execute = false;
            partB_only = false;
            break;
            /** this one really shouldn't be used as it has no meaning*/
        case EventExecutionMode::TWO_PART_EXECUTION:
            two_part_execute = true;
            partB_only = true;
            break;
        case EventExecutionMode::DELAYED:
            two_part_execute = true;
            partB_only = true;
            break;
        default:
            break;
    }
}

}  // namespace griddyn
