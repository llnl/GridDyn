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

void reversibleEvent::cloneTo(Event* gE) const
{
    Event::cloneTo(gE);
    auto nE = dynamic_cast<reversibleEvent*>(gE);
    if (nE == nullptr) {
        return;
    }
    nE->grabber = createGrabber(field, m_obj);
    nE->grabber->outputUnits = grabber->outputUnits;
    nE->canUndo = canUndo;
}

// virtual void updateEvent(EventInfo &gdEI, CoreObject *rootObject) override;
change_code reversibleEvent::trigger()
{
    if (canUndo) {
        undoValue = grabber->grabData();
        hasUndo = true;
    }
    if (stringEvent) {
        if (m_obj == nullptr) {
            armed = false;
            return change_code::execution_failure;
        }
        try {
            m_obj->set(field, newStringValue);
            return change_code::parameter_change;
        }
        catch (const std::invalid_argument&) {
            return change_code::execution_failure;
        }

    } else {
        return Event::trigger();
    }
}

change_code reversibleEvent::trigger(coreTime time)
{
    if (canUndo) {
        undoValue = grabber->grabData();
        hasUndo = true;
    }
    if (stringEvent) {
        change_code ret = change_code::not_triggered;
        if (time >= triggerTime) {
            if (m_obj == nullptr) {
                armed = false;
                return change_code::execution_failure;
            }
            try {
                m_obj->set(field, newStringValue);
                ret = change_code::parameter_change;
            }
            catch (const std::invalid_argument&) {
                ret = change_code::execution_failure;
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

change_code reversibleEvent::undo()
{
    if (hasUndo) {
        setValue(undoValue);
        hasUndo = false;
        return Event::trigger();
    }
    return change_code::not_triggered;
}

double reversibleEvent::query()
{
    return (grabber) ? (grabber->grabData()) : kNullVal;
}

}  // namespace griddyn::events
