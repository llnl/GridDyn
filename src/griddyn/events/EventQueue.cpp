/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "EventQueue.h"

#include "../measurement/Collector.h"
#include "Event.h"
#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include <algorithm>
#include <memory>
#include <string>
#include <typeinfo>

namespace griddyn {
EventQueue::EventQueue()
{
    nullEvent = std::make_shared<EventAdapter>();
    insert(nullEvent);
}
EventQueue::~EventQueue() = default;

CoreTime EventQueue::getNextTime() const
{
    return events.front()->m_nextTime;
}

CoreTime EventQueue::getNextTime(int eventCode) const
{
    const std::scoped_lock lock(queuelock_);
    for (const auto& event : events) {
        if (event->eventCode() == eventCode) {
            return event->m_nextTime;
        }
    }
    return maxTime;
}

void EventQueue::nullEventTime(CoreTime time, CoreTime period)
{
    nullEvent->m_nextTime = time;
    if (period != kNullVal) {
        nullEvent->m_period = period;
    }
    sort();
}

CoreTime EventQueue::getNullEventTime() const
{
    return nullEvent->m_nextTime;
}

std::unique_ptr<EventQueue> EventQueue::clone() const
{
    auto eventQueue = std::make_unique<EventQueue>();
    EventQueue::cloneTo(eventQueue.get());
    return eventQueue;
}

void EventQueue::cloneTo(EventQueue* eventQueue) const
{
    nullEvent->cloneTo(eventQueue->nullEvent.get());
    for (const auto& event : events) {
        if (event == nullEvent)  // we dealt with the nullEvent already
        {
            continue;
        }
        eventQueue->insert(event->clone());
    }
    eventQueue->timeTols = timeTols;
}

void EventQueue::mapObjectsOnto(CoreObject* newRootObject)
{
    for (const auto& event : events) {
        event->updateObject(newRootObject, ObjectUpdateMode::MATCH);
    }
}

ChangeCode EventQueue::executeEvents(CoreTime cTime)
{
    if (events.front()->m_nextTime > cTime + timeTols) {
        return ChangeCode::NO_CHANGE;
    }
    auto ret = ChangeCode::NO_CHANGE;
    auto eret = ChangeCode::NO_CHANGE;
    if (!partB_list.empty()) {
        ret = executeEventsBonly();
    }
    eret = executeEventsAonly(cTime);
    ret = std::max(eret, ret);
    eret = executeEventsBonly();
    ret = std::max(eret, ret);

    return ret;
}

ChangeCode EventQueue::executeEventsAonly(CoreTime cTime)
{
    if (events.front()->m_nextTime > cTime + timeTols) {
        return ChangeCode::NO_CHANGE;
    }
    auto ret = ChangeCode::NO_CHANGE;
    auto eret = ChangeCode::NO_CHANGE;

    bool removeEvents = false;

    const std::scoped_lock lock(queuelock_);

    auto nextEvent = events.begin();
    auto currentEvent = nextEvent;

    while ((*nextEvent)->m_nextTime <= cTime + timeTols) {
        currentEvent = nextEvent;
        ++nextEvent;
        if ((*currentEvent)->two_part_execute) {
            if ((*currentEvent)->partB_turn) {
                eret = (*currentEvent)->execute((*currentEvent)->m_nextTime);
                ret = std::max(eret, ret);
                if ((*currentEvent)->m_remove_event) {
                    removeEvents = true;
                }
                (*currentEvent)->partB_turn = false;
            } else {
                if ((*currentEvent)->partB_only) {
                    partB_list.push_back(*currentEvent);
                } else {
                    (*currentEvent)->executeA((*currentEvent)->m_nextTime);
                    if ((*currentEvent)->partBdelay > timeZero) {
                        (*currentEvent)->m_nextTime += (*currentEvent)->partBdelay;
                        (*currentEvent)->partB_turn = true;
                    } else {
                        partB_list.push_back(*currentEvent);
                    }
                }
            }
        } else {
            eret = (*currentEvent)->execute((*currentEvent)->m_nextTime);
            ret = std::max(eret, ret);
            if ((*currentEvent)->m_remove_event) {
                removeEvents = true;
            }
        }

        if (nextEvent == events.end()) {
            break;
        }
    }
    if (removeEvents) {
        auto removePosition = std::remove_if(events.begin(), nextEvent, [](auto& event) {
            return event->m_remove_event;
        });
        if (removePosition != nextEvent) {
            events.erase(removePosition, nextEvent);
        }
    }
    return ret;
}

ChangeCode EventQueue::executeEventsBonly()
{
    auto ret = ChangeCode::NO_CHANGE;
    auto eret = ChangeCode::NO_CHANGE;
    const std::scoped_lock lock(queuelock_);
    for (auto& currentEvent : partB_list) {
        eret = currentEvent->execute(currentEvent->m_nextTime);
        ret = std::max(eret, ret);
    }
    partB_list.clear();
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wstrict-overflow"
#endif
    std::stable_sort(events.begin(), events.end(), compareEventAdapters);
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif
    return ret;
}

void EventQueue::recheck()
{
    const std::scoped_lock lock(queuelock_);
    for (auto& event : events) {
        event->updateTime();
    }
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wstrict-overflow"
#endif
    std::stable_sort(events.begin(), events.end(), compareEventAdapters);
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif
}

void EventQueue::remove(std::int64_t eventID)
{
    const std::scoped_lock lock(queuelock_);
    auto removePosition =
        std::remove_if(events.begin(), events.end(), [eventID](const auto& event) {
            return (eventID == event->eventID);
        });
    events.erase(removePosition, events.end());
}

count_t EventQueue::size() const
{
    return static_cast<count_t>(events.size());
}

void EventQueue::sort()
{
    const std::scoped_lock lock(queuelock_);
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wstrict-overflow"
#endif
    std::stable_sort(events.begin(), events.end(), compareEventAdapters);
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif
}

void EventQueue::checkDuplicates()
{  // checking for duplicated CoreObject updates which could potentially be bad
    // this function is a private function and should only be called from inside a locked scope
    auto pred = [](const auto& firstEvent, const auto& secondEvent) -> bool {
        if (typeid(*firstEvent) == typeid(*secondEvent)) {
            auto* adapterPrimary = dynamic_cast<EventTypeAdapter<CoreObject>*>(firstEvent.get());
            if (adapterPrimary != nullptr) {
                auto* adapterSecondary =
                    static_cast<EventTypeAdapter<CoreObject>*>(secondEvent.get());
                if (isSameObject(adapterPrimary->getTarget(), adapterSecondary->getTarget())) {
                    return true;
                }
            }
        }
        return false;
    };
    auto lastEvent = std::unique(events.begin(), events.end(), pred);
    if (lastEvent != events.end()) {
        events.erase(lastEvent, events.end());
    }
}

void EventQueue::getEventObjects(std::vector<CoreObject*>& objV) const
{
    for (const auto& event : events) {
        event->getObjects(objV);
    }
    std::stable_sort(objV.begin(), objV.end());
    auto uniqueEnd = std::unique(objV.begin(), objV.end());
    objV.erase(uniqueEnd, objV.end());
}

void EventQueue::set(std::string_view param, double val)
{
    if (param == "timetol") {
        if (val > 0) {
            timeTols = val;
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (param == "nulleventperiod") {
        nullEvent->m_period = val;
    } else if (param == "nulleventtime") {
        nullEvent->m_nextTime = val;
        sort();
    } else {
        throw(UnrecognizedParameter(param));
    }
}

}  // namespace griddyn
