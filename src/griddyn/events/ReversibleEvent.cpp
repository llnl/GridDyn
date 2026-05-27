/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ReversibleEvent.h"

#include "../measurement/GridGrabbers.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace griddyn::events {
ReversibleEvent::ReversibleEvent(const std::string& eventName): Event(eventName) {}
ReversibleEvent::ReversibleEvent(coreTime time0): Event(time0) {}
ReversibleEvent::ReversibleEvent(const EventInfo& gdEI, CoreObject* rootObject):
    Event(gdEI, rootObject)
{
    grabber = createGrabber(field, m_obj);
    grabber->outputUnits = unitType;
    canUndo = grabber->loaded;
}

void ReversibleEvent::updateEvent(const EventInfo& gdEI, CoreObject* rootObject)
{
    Event::updateEvent(gdEI, rootObject);
    grabber = createGrabber(field, m_obj);
    grabber->outputUnits = unitType;
    canUndo = grabber->loaded;
}

ReversibleEvent::~ReversibleEvent() = default;

std::unique_ptr<Event> ReversibleEvent::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<ReversibleEvent>(getName());
    cloneTo(upE.get());
    return upE;
}

void ReversibleEvent::cloneTo(Event* evnt) const
{
    Event::cloneTo(evnt);
    auto* newEvent = dynamic_cast<ReversibleEvent*>(evnt);
    if (newEvent == nullptr) {
        return;
    }
    newEvent->grabber = createGrabber(field, m_obj);
    newEvent->grabber->outputUnits = grabber->outputUnits;
    newEvent->canUndo = canUndo;
}

// virtual void updateEvent(EventInfo &gdEI, CoreObject *rootObject) override;
ChangeCode ReversibleEvent::trigger()
{
    if (canUndo) {
        undoValue = grabber->grabData();
        hasUndo = true;
    }
    if (stringEvent) {
        if (m_obj == nullptr) {
            armed = false;
            return ChangeCode::EXECUTION_FAILURE;
        }
        try {
            m_obj->set(field, newStringValue);
            return ChangeCode::PARAMETER_CHANGE;
        }
        catch (const std::invalid_argument&) {
            return ChangeCode::EXECUTION_FAILURE;
        }

    } else {
        return Event::trigger();
    }
}

ChangeCode ReversibleEvent::trigger(coreTime time)
{
    if (canUndo) {
        undoValue = grabber->grabData();
        hasUndo = true;
    }
    if (stringEvent) {
        ChangeCode ret = ChangeCode::NOT_TRIGGERED;
        if (time >= triggerTime) {
            if (m_obj == nullptr) {
                armed = false;
                return ChangeCode::EXECUTION_FAILURE;
            }
            try {
                m_obj->set(field, newStringValue);
                ret = ChangeCode::PARAMETER_CHANGE;
            }
            catch (const std::invalid_argument&) {
                ret = ChangeCode::EXECUTION_FAILURE;
            }
            armed = false;
        }
        return ret;
    }
    return Event::trigger(time);
}

bool ReversibleEvent::setTarget(CoreObject* gdo, std::string_view var)
{
    auto res = Event::setTarget(gdo, var);
    if (grabber) {
        grabber->updateObject(m_obj);
        grabber->updateField(field);
        canUndo = grabber->loaded;
    } else {
        grabber = createGrabber(field, m_obj);
        grabber->outputUnits = unitType;
        canUndo = grabber->loaded;
    }
    return res;
}

void ReversibleEvent::updateStringValue(const std::string& newStr)
{
    newStringValue = newStr;
}

void ReversibleEvent::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    Event::updateObject(gco, mode);
    if (grabber) {
        grabber->updateObject(gco, mode);
    }
}

ChangeCode ReversibleEvent::undo()
{
    if (hasUndo) {
        setValue(undoValue);
        hasUndo = false;
        return Event::trigger();
    }
    return ChangeCode::NOT_TRIGGERED;
}

double ReversibleEvent::query()
{
    return (grabber) ? (grabber->grabData()) : kNullVal;
}

}  // namespace griddyn::events
