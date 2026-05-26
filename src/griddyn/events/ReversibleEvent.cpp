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
reversibleEvent::reversibleEvent(const std::string& eventName): Event(eventName) {}
reversibleEvent::reversibleEvent(coreTime time0): Event(time0) {}
reversibleEvent::reversibleEvent(const EventInfo& gdEI, CoreObject* rootObject):
    Event(gdEI, rootObject)
{
    grabber = createGrabber(field, m_obj);
    grabber->outputUnits = unitType;
    canUndo = grabber->loaded;
}

void reversibleEvent::updateEvent(const EventInfo& gdEI, CoreObject* rootObject)
{
    Event::updateEvent(gdEI, rootObject);
    grabber = createGrabber(field, m_obj);
    grabber->outputUnits = unitType;
    canUndo = grabber->loaded;
}

reversibleEvent::~reversibleEvent() = default;

std::unique_ptr<Event> reversibleEvent::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<reversibleEvent>(getName());
    cloneTo(upE.get());
    return upE;
}

void reversibleEvent::cloneTo(Event* evnt) const
{
    Event::cloneTo(evnt);
    auto* newEvent = dynamic_cast<reversibleEvent*>(evnt);
    if (newEvent == nullptr) {
        return;
    }
    newEvent->grabber = createGrabber(field, m_obj);
    newEvent->grabber->outputUnits = grabber->outputUnits;
    newEvent->canUndo = canUndo;
}

// virtual void updateEvent(EventInfo &gdEI, CoreObject *rootObject) override;
ChangeCode reversibleEvent::trigger()
{
    if (canUndo) {
        undoValue = grabber->grabData();
        hasUndo = true;
    }
    if (stringEvent) {
        if (m_obj == nullptr) {
            armed = false;
            return ChangeCode::execution_failure;
        }
        try {
            m_obj->set(field, newStringValue);
            return ChangeCode::parameter_change;
        }
        catch (const std::invalid_argument&) {
            return ChangeCode::execution_failure;
        }

    } else {
        return Event::trigger();
    }
}

ChangeCode reversibleEvent::trigger(coreTime time)
{
    if (canUndo) {
        undoValue = grabber->grabData();
        hasUndo = true;
    }
    if (stringEvent) {
        ChangeCode ret = ChangeCode::not_triggered;
        if (time >= triggerTime) {
            if (m_obj == nullptr) {
                armed = false;
                return ChangeCode::execution_failure;
            }
            try {
                m_obj->set(field, newStringValue);
                ret = ChangeCode::parameter_change;
            }
            catch (const std::invalid_argument&) {
                ret = ChangeCode::execution_failure;
            }
            armed = false;
        }
        return ret;
    }
    return Event::trigger(time);
}

bool reversibleEvent::setTarget(CoreObject* gdo, std::string_view var)
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

void reversibleEvent::updateStringValue(const std::string& newStr)
{
    newStringValue = newStr;
}

void reversibleEvent::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    Event::updateObject(gco, mode);
    if (grabber) {
        grabber->updateObject(gco, mode);
    }
}

ChangeCode reversibleEvent::undo()
{
    if (hasUndo) {
        setValue(undoValue);
        hasUndo = false;
        return Event::trigger();
    }
    return ChangeCode::not_triggered;
}

double reversibleEvent::query()
{
    return (grabber) ? (grabber->grabData()) : kNullVal;
}

}  // namespace griddyn::events

