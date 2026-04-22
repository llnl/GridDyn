/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Event.h"

#include "../gridDynSimulation.h"
#include "Player.h"
#include "compoundEvent.h"
#include "compoundEventPlayer.h"
#include "core/coreExceptions.h"
#include "core/factoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "interpolatingPlayer.h"
#include "reversibleEvent.h"
#include <memory>
#include <sstream>
#include <string>

namespace griddyn {
using gmlc::utilities::str2vector;
using gmlc::utilities::stringOps::trailingStringInt;
using gmlc::utilities::stringOps::trim;
using gmlc::utilities::stringOps::trimString;

static classFactory<Event> evntFac(std::vector<std::string>{"event", "simple", "single"}, "event");
namespace events {
    static childClassFactory<Player, Event>
        playerFac(std::vector<std::string>{"player", "timeseries", "file"});

    static childClassFactory<compoundEvent, Event>
        cmpdEvnt(std::vector<std::string>{"multi", "compound"});

    static childClassFactory<compoundEventPlayer, Event>
        cmpdEvntPlay(std::vector<std::string>{"compoundplayer", "multifile", "multiplayer"});

    static childClassFactory<interpolatingPlayer, Event>
        interpPlay(std::vector<std::string>{"interpolating", "interp", "interpolated"});
    static childClassFactory<reversibleEvent, Event>
        revEvnt(std::vector<std::string>{"reversible", "undo", "rollback"});
}  // namespace events

Event::Event(const std::string& eventName):
    helperObject(eventName), triggerTime(negTime), eventId(static_cast<count_t>(getID()))
{
}

Event::Event(coreTime time0): triggerTime(time0), eventId(static_cast<count_t>(getID())) {}

Event::Event(const EventInfo& gdEI, coreObject* rootObject): eventId(static_cast<count_t>(getID()))
{
    Event::updateEvent(gdEI, rootObject);
}

void Event::updateEvent(const EventInfo& gdEI, coreObject* rootObject)
{
    if (!gdEI.description.empty()) {
        setDescription(gdEI.description);
    }
    if (!gdEI.name.empty()) {
        setName(gdEI.name);
    }
    if (!gdEI.value.empty()) {
        value = gdEI.value[0];
    }
    if (!gdEI.time.empty()) {
        triggerTime = gdEI.time[0];
    }
    coreObject* searchObj = rootObject;

    if (!gdEI.targetObjs.empty()) {
        searchObj = gdEI.targetObjs[0];
    }
    if (!gdEI.units.empty()) {
        unitType = gdEI.units[0];
    }
    if (!gdEI.fieldList.empty()) {
        loadField(searchObj, gdEI.fieldList[0]);
    } else {
        m_obj = searchObj;
    }

    armed = checkArmed();
}

bool Event::checkArmed()
{
    if (m_obj != nullptr) {
        if (!field.empty()) {
            return true;
        }
    }
    return false;
}

void Event::loadField(coreObject* searchObj, const std::string& newField)
{
    auto renameloc = newField.find(" as ");  // spaces are important
                                             // extract out a rename

    objInfo fdata;
    if (renameloc != std::string::npos) {
        setName(trim(newField.substr(renameloc + 4)));
        fdata = objInfo(newField.substr(0, renameloc), searchObj);
    } else {
        fdata = objInfo(newField, searchObj);
    }

    field = fdata.m_field;
    if (fdata.m_unitType != units::defunit) {
        unitType = fdata.m_unitType;
    }
    if (fdata.m_obj != m_obj) {
        m_obj = fdata.m_obj;
        if (getName().empty()) {
            setName(m_obj->getName() + ":" + field);
        }
    }
    armed = checkArmed();
}

std::unique_ptr<Event> Event::clone() const
{
    std::unique_ptr<Event> upE = std::make_unique<Event>(getName());
    cloneTo(upE.get());
    return upE;
}

void Event::cloneTo(Event* evnt) const
{
    evnt->value = value;
    evnt->field = field;
    evnt->armed = armed;
    evnt->m_obj = m_obj;
    evnt->resettable = resettable;
    evnt->reversible = reversible;
    evnt->initRequired = initRequired;
    evnt->unitType = unitType;
    evnt->triggerTime = triggerTime;
}

void Event::setFlag(std::string_view flag, bool val)
{
    if (flag == "armed") {
        armed = val;
    } else {
        helperObject::setFlag(flag, val);
    }
}
void Event::set(std::string_view param, double val)
{
    if (param == "value") {
        value = val;
    } else if (param == "time") {
        triggerTime = val;
    } else if (param == "armed") {
        armed = (val > 0.1);
    } else {
        helperObject::set(param, val);
    }
}

void Event::set(std::string_view param, std::string_view val)
{
    if (param == "field") {
        setTarget(m_obj, std::string{val});
    } else if (param == "units") {
        units::unit newUnits = unit_cast(units::unit_from_string(std::string{val}));
        if (!is_valid(newUnits)) {
            throw(invalidParameterValue(std::string{param}));
        }
        unitType = newUnits;
    } else {
        helperObject::set(param, val);
    }
}

void Event::setTime(coreTime time)
{
    triggerTime = time;
}

void Event::setValue(double val, units::unit newUnits)
{
    value = val;
    if (is_valid(newUnits)) {
        if (unitType == units::defunit) {
            unitType = newUnits;
        } else {
            value = convert(value, newUnits, unitType, m_obj->get("basepower"));
            if (value == kNullVal) {
                value = val;
                unitType = newUnits;
            }
        }
    }
}

std::string Event::to_string() const
{
    // [@time1 | ]rootobj::obj:field[(units)] = val1
    std::stringstream ss;
    if (triggerTime > negTime) {
        ss << '@' << triggerTime;
        ss << " | ";
    }
    ss << fullObjectName(m_obj) << ':' << field;
    if (unitType != units::defunit) {
        ss << '(' << units::to_string(unitType) << ')';
    }
    ss << " = " << value;
    return ss.str();
}

change_code Event::trigger()
{
    if (m_obj == nullptr) {
        armed = false;
        return change_code::execution_failure;
    }
    try {
        m_obj->set(field, value, unitType);
        return change_code::parameter_change;
    }
    catch (const std::invalid_argument&) {
        return change_code::execution_failure;
    }
}

change_code Event::trigger(coreTime time)
{
    change_code ret = change_code::not_triggered;
    if (time >= triggerTime) {
        if (m_obj == nullptr) {
            armed = false;
            return change_code::execution_failure;
        }
        try {
            m_obj->set(field, value, unitType);
            ret = change_code::parameter_change;
        }
        catch (const std::invalid_argument&) {
            ret = change_code::execution_failure;
        }
        armed = false;
    }
    return ret;
}

void Event::updateObject(coreObject* gco, object_update_mode mode)
{
    if (mode == object_update_mode::direct) {
        setTarget(gco);
    } else {
        if (m_obj != nullptr) {
            auto newTarget = findMatchingObject(m_obj, gco);
            if (newTarget != nullptr) {
                setTarget(newTarget);
            } else {
                throw(objectUpdateFailException());
            }
        } else {
            setTarget(gco);
        }
    }
}

coreObject* Event::getObject() const
{
    return m_obj;
}

void Event::getObjects(std::vector<coreObject*>& objects) const
{
    objects.push_back(getObject());
}

bool Event::setTarget(coreObject* gdo, const std::string& var)
{
    if (gdo != nullptr) {
        m_obj = gdo;
        setName(m_obj->getName());
    }
    if (!var.empty()) {
        loadField(m_obj, var);
    }

    armed = checkArmed();
    return armed;
}

enum class event_types {
    basic,
    compound,
    player,
    compoundplayer,
    toggle,
    interpolating,
    reversible,
};

event_types findEventType(EventInfo& gdEI)
{
    if (!gdEI.type.empty()) {
        if ((gdEI.type == "basic") || (gdEI.type == "simple")) {
            return event_types::basic;
        }
        if (gdEI.type == "player") {
            return event_types::player;
        }
        if (gdEI.type == "compound") {
            return event_types::compound;
        }
        if (gdEI.type == "compoundplayer") {
            return event_types::compoundplayer;
        }
        if (gdEI.type == "toggle") {
            return event_types::toggle;
        }
        if (gdEI.type == "reversible") {
            return event_types::reversible;
        }
        if ((gdEI.type == "interpolating") || (gdEI.type == "interpolated")) {
            return event_types::interpolating;
        }
    }
    if (!gdEI.file.empty()) {
        return event_types::player;
    }
    if (gdEI.period > timeZero) {
        return event_types::player;
    }
    if (gdEI.time.size() > 1) {
        return event_types::player;
    }
    if (gdEI.value.size() > 1) {
        return event_types::compound;
    }
    if (gdEI.fieldList.size() > 1) {
        return event_types::compound;
    }
    return event_types::basic;
}

EventInfo::EventInfo(const std::string& eventString, coreObject* rootObj)
{
    loadString(eventString, rootObj);
}

// @time1[,time2,time3,... + period] |[rootobj::obj1:]field(units) const =
// val1,[val2,val3,...];[rootobj::obj1:]field(units) const = val1,[val2,val3,...];  or
// [rootobj::obj:]field(units) = val1,[val2,val3,...] @time1[,time2,time3,...|+ period] or
void EventInfo::loadString(const std::string& eventString, coreObject* rootObj)
{
    if (eventString.find_first_of(';') != std::string::npos) {
        auto svector = gmlc::utilities::stringOps::splitlineBracket(eventString, ";");
        if (svector.size() > 1) {
            for (const auto& estring : svector) {
                if (!estring.empty()) {
                    loadString(estring, rootObj);
                }
            }
            return;
        }
    }
    std::string objString;
    auto posA = eventString.find_first_of('@');
    if (posA == std::string::npos) {
        objString = eventString;
    } else {
        auto posT = eventString.find_first_of('|', posA + 2);
        std::string tstring = (posT != std::string::npos) ?
            eventString.substr(posA + 1, posT - posA - 1) :
            eventString.substr(posA + 1, std::string::npos);
        trimString(tstring);
        auto cstr = tstring.find_first_of(',');
        if (cstr == std::string::npos) {
            cstr = tstring.find_first_of('+');
            if (cstr == std::string::npos) {
                time.emplace_back(std::stod(tstring));
            } else {
                time.emplace_back(std::stod(tstring.substr(0, cstr)));
                period = std::stod(tstring.substr(cstr + 1, std::string::npos));
            }
        } else {
            time = str2vector<coreTime>(tstring, negTime, ",");
        }
        objString = (posA > 2) ? eventString.substr(0, posA - 1) :
                                 eventString.substr(posT + 1, std::string::npos);
    }
    trimString(objString);
    auto posE = objString.find_first_of('=');
    std::string vstring = objString.substr(posE + 1, std::string::npos);
    trimString(vstring);
    objString = objString.substr(0, posE);
    // break down the object specification
    objInfo fdata(objString, rootObj);

    targetObjs.push_back(fdata.m_obj);
    units.push_back(fdata.m_unitType);
    fieldList.push_back(fdata.m_field);

    auto posFile = vstring.find_first_of('{');
    if (posFile != std::string::npos) {  // now we get into file based event
        auto posEndFile = vstring.find_first_of('}', posFile);
        file = vstring.substr(posE + 1, posEndFile - posFile - 1);

        int col = trailingStringInt(file, file, 0);
        columns.push_back(col);
        auto posPlus = vstring.find_first_of('+', posEndFile);
        if (posPlus != std::string::npos) {
            period = std::stod(vstring.substr(posPlus + 1, std::string::npos));
        }
    } else {
        auto cstr = vstring.find_first_of(',');
        if (cstr == std::string::npos) {
            value.push_back(std::stod(vstring));
        } else {
            value = str2vector(vstring, -1.0, ",");
        }
    }
}

std::unique_ptr<Event>
    make_event(const std::string& field, double val, coreTime eventTime, coreObject* rootObject)
{
    auto ev = std::make_unique<Event>(eventTime);
    objInfo fdata(field, rootObject);
    ev->setTarget(fdata.m_obj, fdata.m_field);
    ev->setValue(val, fdata.m_unitType);
    return ev;
}

std::unique_ptr<Event> make_event(const std::string& eventString, coreObject* rootObject)
{
    EventInfo gdEI(eventString, rootObject);
    return make_event(gdEI, rootObject);
}

std::unique_ptr<Event> make_event(EventInfo& gdEI, coreObject* rootObject)
{
    std::unique_ptr<Event> ev;
    if (!gdEI.type.empty()) {
        ev = coreClassFactory<Event>::instance()->createObject(gdEI.type);
        if (ev) {
            ev->updateEvent(gdEI, rootObject);
            return ev;
        }
    }
    auto evType = findEventType(gdEI);

    switch (evType) {
        case event_types::basic:
            ev = std::make_unique<Event>(gdEI, rootObject);
            break;
        case event_types::compound:
            ev = std::make_unique<events::compoundEvent>(gdEI, rootObject);
            break;
        case event_types::player:
            ev = std::make_unique<events::Player>(gdEI, rootObject);
            break;
        case event_types::compoundplayer:
            ev = std::make_unique<events::compoundEventPlayer>(gdEI, rootObject);
            break;
        case event_types::interpolating:
            ev = std::make_unique<events::interpolatingPlayer>(gdEI, rootObject);
            break;
        case event_types::reversible:
            ev = std::make_unique<events::reversibleEvent>(gdEI, rootObject);
            break;
        case event_types::toggle:
            break;
        default:
            break;
    }

    return ev;
}

}  // namespace griddyn

