/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CompoundEvent.h"

#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace griddyn::events {
CompoundEvent::CompoundEvent(coreTime time0): Event(time0) {}

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

void CompoundEvent::cloneTo(Event* gE) const
{
    Event::cloneTo(gE);
    auto nE = dynamic_cast<CompoundEvent*>(gE);
    if (nE == nullptr) {
        return;
    }
    nE->fields = fields;
    nE->values = values;
    nE->units = units;
    nE->targetObjects = targetObjects;
}

void CompoundEvent::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    // TODO(pt): more thinking on exception safety
    if (mode == ObjectUpdateMode::DIRECT) {
        setTarget(gco);
    } else if (mode == ObjectUpdateMode::MATCH) {
        for (auto& obj : targetObjects) {
            if (obj != nullptr) {
                auto tempobj = findMatchingObject(obj, gco);
                if (tempobj == nullptr) {
                    throw(ObjectUpdateFailException());
                }
                obj = tempobj;
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
    for (auto& obj : targetObjects) {
        objects.push_back(obj);
    }
}

void CompoundEvent::setValue(double val, units::unit newUnits)
{
    // TODO(pt): this has issues
    for (auto& vv : values) {
        vv = val;
    }
    for (auto& uu : units) {
        uu = newUnits;
    }
}

void CompoundEvent::set(std::string_view param, double val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Event::set(param, val);
    }
}

void CompoundEvent::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        Event::set(param, val);
    }
}

void CompoundEvent::setValue(const std::vector<double>& val)
{
    values = val;
}

std::string CompoundEvent::to_string() const
{
    // @time1[,time2,time3,... |+ period] >[rootobj::obj:]field(units) = val1,[val2,val3,...]
    std::stringstream ss;
    ss << '@' << triggerTime << " | ";

    ss << fullObjectName(targetObjects[0]) << ':' << fields[0];
    if (units[0] != units::defunit) {
        ss << '(' << units::to_string(units[0]) << ')';
    }
    ss << " = " << values[0];
    for (index_t kk = 1; kk < static_cast<index_t>(values.size()); ++kk) {
        ss << "; " << fullObjectName(targetObjects[kk]) << ':' << fields[kk];
        if (units[kk] != units::defunit) {
            ss << '(' << units::to_string(units[kk]) << ')';
        }
        ss << " = " << values[kk];
    }

    return ss.str();
}
ChangeCode CompoundEvent::trigger()
{
    try {
        if (targetObjects.empty()) {
            m_obj->set(field, value, unitType);
        } else {
            int index = 0;
            for (auto& to : targetObjects) {
                to->set(fields[index], values[index], units[index]);
                ++index;
            }
        }
        return ChangeCode::PARAMETER_CHANGE;
    }
    catch (const std::invalid_argument&) {
        return ChangeCode::EXECUTION_FAILURE;
    }
}

ChangeCode CompoundEvent::trigger(coreTime time)
{
    ChangeCode ret = ChangeCode::NOT_TRIGGERED;
    if (time >= triggerTime) {
        try {
            if (targetObjects.empty()) {
                m_obj->set(field, value, unitType);
            } else {
                int index = 0;
                for (auto& to : targetObjects) {
                    to->set(fields[index], values[index], units[index]);
                    ++index;
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
