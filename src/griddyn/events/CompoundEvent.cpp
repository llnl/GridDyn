/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CompoundEvent.h"

#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace griddyn::events {
CompoundEvent::CompoundEvent(CoreTime time0): Event(time0) {}

CompoundEvent::CompoundEvent(const std::string& eventName): Event(eventName) {}

CompoundEvent::CompoundEvent(const EventInfo& gdEI, CoreObject* rootObject): Event(gdEI, rootObject)
{
    targetObjects = gdEI.targetObjs;
    values = gdEI.value;
    units = gdEI.units;
    fields = gdEI.fieldList;
}

std::unique_ptr<Event> CompoundEvent::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<CompoundEvent>(getName());
    cloneTo(upE.get());
    return upE;
}

void CompoundEvent::cloneTo(Event* event) const
{
    Event::cloneTo(event);
    auto* compoundEvent = dynamic_cast<CompoundEvent*>(event);
    if (compoundEvent == nullptr) {
        return;
    }
    compoundEvent->fields = fields;
    compoundEvent->values = values;
    compoundEvent->units = units;
    compoundEvent->targetObjects = targetObjects;
}

void CompoundEvent::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    // TODO(pt): more thinking on exception safety
    if (mode == ObjectUpdateMode::DIRECT) {
        setTarget(gco);
    } else if (mode == ObjectUpdateMode::MATCH) {
        if (m_obj != nullptr) {
            auto* matchedObject = findMatchingObject(m_obj, gco);
            if (matchedObject == nullptr) {
                throw(ObjectUpdateFailException());
            }
            m_obj = matchedObject;
        }
        for (auto& obj : targetObjects) {
            if (obj != nullptr) {
                auto* matchedObject = findMatchingObject(obj, gco);
                if (matchedObject == nullptr) {
                    throw(ObjectUpdateFailException());
                }
                obj = matchedObject;
            }
        }
    }
}

CoreObject* CompoundEvent::getObject() const
{
    return targetObjects[0];
}

void CompoundEvent::getObjects(std::vector<CoreObject*>& objects) const
{
    for (const auto& targetObject : targetObjects) {
        objects.push_back(targetObject);
    }
}

void CompoundEvent::setValue(double val, units::unit newUnits)
{
    // TODO(pt): this has issues
    for (auto& assignedValue : values) {
        assignedValue = val;
    }
    for (auto& assignedUnits : units) {
        assignedUnits = newUnits;
    }
}

void CompoundEvent::setValue(const std::vector<double>& val)
{
    values = val;
}

std::string CompoundEvent::to_string() const
{
    // @time1[,time2,time3,... |+ period] >[rootobj::obj:]field(units) = val1,[val2,val3,...]
    std::stringstream output;
    output << '@' << triggerTime << " | ";

    output << fullObjectName(targetObjects[0]) << ':' << fields[0];
    if (units[0] != units::defunit) {
        output << '(' << units::to_string(units[0]) << ')';
    }
    output << " = " << values[0];
    for (std::size_t index = 1; index < values.size(); ++index) {
        output << "; " << fullObjectName(targetObjects[index]) << ':' << fields[index];
        if (units[index] != units::defunit) {
            output << '(' << units::to_string(units[index]) << ')';
        }
        output << " = " << values[index];
    }

    return output.str();
}
ChangeCode CompoundEvent::trigger()
{
    try {
        if (targetObjects.empty()) {
            m_obj->set(field, value, unitType);
        } else {
            for (std::size_t index = 0; index < targetObjects.size(); ++index) {
                targetObjects[index]->set(fields[index], values[index], units[index]);
            }
        }
        return ChangeCode::PARAMETER_CHANGE;
    }
    catch (const std::invalid_argument&) {
        return ChangeCode::EXECUTION_FAILURE;
    }
}

ChangeCode CompoundEvent::trigger(CoreTime time)
{
    ChangeCode ret = ChangeCode::NOT_TRIGGERED;
    if (time >= triggerTime) {
        try {
            if (targetObjects.empty()) {
                m_obj->set(field, value, unitType);
            } else {
                for (std::size_t index = 0; index < targetObjects.size(); ++index) {
                    targetObjects[index]->set(fields[index], values[index], units[index]);
                }
            }
            ret = ChangeCode::PARAMETER_CHANGE;
        }
        catch (const std::invalid_argument&) {
            ret = ChangeCode::EXECUTION_FAILURE;
        }
        armed = false;
    }
    return ret;
}

bool CompoundEvent::setTarget(CoreObject* gdo, std::string_view var)
{
    if (!var.empty()) {
        field = var;
    }
    if (gdo != nullptr) {
        m_obj = gdo;
    }

    if (m_obj != nullptr) {
        setName(m_obj->getName());
        armed = true;
    } else {
        armed = false;
    }
    return armed;
}
}  // namespace griddyn::events
