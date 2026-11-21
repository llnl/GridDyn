/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Event.h"

#include "../GridDynSimulation.h"
#include "CompoundEvent.h"
#include "CompoundEventPlayer.h"
#include "InterpolatingPlayer.h"
#include "Player.h"
#include "ReversibleEvent.h"
#include "core/CoreExceptions.h"
#include "core/FactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <charconv>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace griddyn {
using gmlc::utilities::stringOps::splitline;
using gmlc::utilities::stringOps::trailingStringInt;
using gmlc::utilities::stringOps::trim;

static ClassFactory<Event> gEventFactory(std::vector<std::string>{"event", "simple", "single"},
                                         "event");
namespace events {
    static ChildClassFactory<Player, Event>
        gPlayerFactory(std::vector<std::string>{"player", "timeseries", "file"});

    static ChildClassFactory<CompoundEvent, Event>
        gCompoundEventFactory(std::vector<std::string>{"multi", "compound"});

    static ChildClassFactory<CompoundEventPlayer, Event> gCompoundEventPlayerFactory(
        std::vector<std::string>{"compoundplayer", "multifile", "multiplayer"});

    static ChildClassFactory<InterpolatingPlayer, Event> gInterpolatingPlayerFactory(
        std::vector<std::string>{"interpolating", "interp", "interpolated"});
    static ChildClassFactory<ReversibleEvent, Event>
        gReversibleEventFactory(std::vector<std::string>{"reversible", "undo", "rollback"});
}  // namespace events

Event::Event(const std::string& eventName):
    HelperObject(eventName), triggerTime(negTime), eventId(static_cast<count_t>(getID()))
{
}

Event::Event(CoreTime time0): triggerTime(time0), eventId(static_cast<count_t>(getID())) {}

Event::Event(const EventInfo& gdEI, CoreObject* rootObject):
    triggerTime(negTime), eventId(static_cast<count_t>(getID()))
{
    Event::updateEvent(gdEI, rootObject);
}

void Event::updateEvent(const EventInfo& gdEI, CoreObject* rootObject)
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
    CoreObject* searchObj = rootObject;

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

void Event::loadField(CoreObject* searchObj, std::string_view newField)
{
    auto renameloc = newField.find(" as ");  // spaces are important
                                             // extract out a rename

    ObjectInfo fdata;
    if (renameloc != std::string::npos) {
        setName(trim(std::string{newField.substr(renameloc + 4)}));
        fdata = ObjectInfo(newField.substr(0, renameloc), searchObj);
    } else {
        fdata = ObjectInfo(newField, searchObj);
    }

    field = fdata.mField;
    if (fdata.mUnitType != units::defunit) {
        unitType = fdata.mUnitType;
    }
    if (fdata.mObject != m_obj) {
        m_obj = fdata.mObject;
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
        HelperObject::setFlag(flag, val);
    }
}
void Event::set(std::string_view param, double val)
{
    if (param == "value") {
        value = val;
    } else if (param == "time") {
        triggerTime = val;
    } else {
        HelperObject::set(param, val);
    }
}

void Event::set(std::string_view param, std::string_view val)
{
    if (param == "field") {
        setTarget(m_obj, std::string{val});
    } else if (param == "units") {
        const units::unit newUnits = unit_cast(units::unit_from_string(std::string{val}));
        if (!is_valid(newUnits)) {
            throw(InvalidParameterValue(param));
        }
        unitType = newUnits;
    } else {
        HelperObject::set(param, val);
    }
}

void Event::setTime(CoreTime time)
{
    triggerTime = time;
}

void Event::setValue(double val, units::unit newUnits)
{
    value = val;
    if (is_valid(newUnits)) {
        if (unitType == units::defunit) {
            unitType = newUnits;
        } else if (m_obj != nullptr) {
            value = convert(value, newUnits, unitType, m_obj->get("basepower"));
            if (value == kNullVal) {
                value = val;
                unitType = newUnits;
            }
        } else {
            value = val;
            unitType = newUnits;
        }
    }
}

std::string Event::to_string() const
{
    // [@time1 | ]rootobj::obj:field[(units)] = val1
    std::stringstream stream;
    if (triggerTime > negTime) {
        stream << '@' << triggerTime;
        stream << " | ";
    }
    stream << fullObjectName(m_obj) << ':' << field;
    if (unitType != units::defunit) {
        stream << '(' << units::to_string(unitType) << ')';
    }
    stream << " = " << value;
    return stream.str();
}

ChangeCode Event::trigger()
{
    if (m_obj == nullptr) {
        armed = false;
        return ChangeCode::EXECUTION_FAILURE;
    }
    try {
        m_obj->set(field, value, unitType);
        return ChangeCode::PARAMETER_CHANGE;
    }
    catch (const std::invalid_argument&) {
        return ChangeCode::EXECUTION_FAILURE;
    }
}

ChangeCode Event::trigger(CoreTime time)
{
    ChangeCode ret = ChangeCode::NOT_TRIGGERED;
    if (time >= triggerTime) {
        if (m_obj == nullptr) {
            armed = false;
            return ChangeCode::EXECUTION_FAILURE;
        }
        try {
            m_obj->set(field, value, unitType);
            ret = ChangeCode::PARAMETER_CHANGE;
        }
        catch (const std::invalid_argument&) {
            ret = ChangeCode::EXECUTION_FAILURE;
        }
        armed = false;
    }
    return ret;
}

void Event::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    if (mode == ObjectUpdateMode::DIRECT) {
        setTarget(gco);
    } else {
        if (m_obj != nullptr) {
            auto* newTarget = findMatchingObject(m_obj, gco);
            if (newTarget != nullptr) {
                setTarget(newTarget);
            } else {
                throw(ObjectUpdateFailException());
            }
        } else {
            setTarget(gco);
        }
    }
}

CoreObject* Event::getObject() const
{
    return m_obj;
}

void Event::getObjects(std::vector<CoreObject*>& objects) const
{
    objects.push_back(getObject());
}

bool Event::setTarget(CoreObject* gdo, std::string_view var)
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

namespace {
    enum class EventType : std::uint8_t {
        BASIC,
        COMPOUND,
        PLAYER,
        COMPOUND_PLAYER,
        TOGGLE,
        INTERPOLATING,
        REVERSIBLE,
    };

    EventType findEventType(EventInfo& gdEI)
    {
        if (!gdEI.type.empty()) {
            if ((gdEI.type == "basic") || (gdEI.type == "simple")) {
                return EventType::BASIC;
            }
            if (gdEI.type == "player") {
                return EventType::PLAYER;
            }
            if (gdEI.type == "compound") {
                return EventType::COMPOUND;
            }
            if (gdEI.type == "compoundplayer") {
                return EventType::COMPOUND_PLAYER;
            }
            if (gdEI.type == "toggle") {
                return EventType::TOGGLE;
            }
            if (gdEI.type == "reversible") {
                return EventType::REVERSIBLE;
            }
            if ((gdEI.type == "interpolating") || (gdEI.type == "interpolated")) {
                return EventType::INTERPOLATING;
            }
        }
        if (!gdEI.file.empty()) {
            return EventType::PLAYER;
        }
        if (gdEI.period > timeZero) {
            return EventType::PLAYER;
        }
        if (gdEI.time.size() > 1) {
            return EventType::PLAYER;
        }
        if (gdEI.value.size() > 1) {
            return EventType::COMPOUND;
        }
        if (gdEI.fieldList.size() > 1) {
            return EventType::COMPOUND;
        }
        return EventType::BASIC;
    }
}  // namespace

EventInfo::EventInfo(std::string_view eventString, CoreObject* rootObj)
{
    loadString(eventString, rootObj);
}

namespace {
    double readEventNumber(std::string_view text, std::string_view operand)
    {
        const auto token = trim(text);
        const auto* begin = token.data() + (token.starts_with('+') ? 1 : 0);
        const auto* end = token.data() + token.size();
        double number = 0.0;
        const auto result = std::from_chars(begin, end, number);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(number)) {
            throw InvalidParameterValue(operand);
        }
        return number;
    }

    void readEventTimes(EventInfo& info, std::string_view schedule)
    {
        auto times = splitline(schedule, ',');
        if (times.empty()) {
            throw InvalidParameterValue("event time");
        }
        auto& last = times.back();
        last = trim(last);
        for (auto separator = last.find('+', 1); separator != std::string::npos;
             separator = last.find('+', separator + 1)) {
            if (last[separator - 1] != 'e' && last[separator - 1] != 'E') {
                info.period =
                    readEventNumber(std::string_view(last).substr(separator + 1), "event period");
                last.resize(separator);
                break;
            }
        }
        for (const auto& time : times) {
            info.time.emplace_back(readEventNumber(time, "event time"));
        }
    }

    void readEventAssignment(EventInfo& info, std::string_view text, CoreObject* rootObj)
    {
        const auto entry = trim(text);
        if (entry.empty()) {
            return;
        }
        std::string_view assignment(entry);
        const auto timeMarker = assignment.find('@');
        if (timeMarker == 0) {
            const auto separator = assignment.find_first_of("|>", 1);
            if (separator == std::string_view::npos) {
                throw InvalidParameterValue("event time separator");
            }
            readEventTimes(info, assignment.substr(1, separator - 1));
            assignment.remove_prefix(separator + 1);
        } else if (timeMarker != std::string_view::npos) {
            readEventTimes(info, assignment.substr(timeMarker + 1));
            assignment = assignment.substr(0, timeMarker);
        }

        const auto equals = assignment.find('=');
        if (equals == std::string_view::npos) {
            throw InvalidParameterValue("event assignment");
        }
        const ObjectInfo target(trim(assignment.substr(0, equals)), rootObj);
        info.targetObjs.push_back(target.mObject);
        info.units.push_back(target.mUnitType);
        info.fieldList.push_back(target.mField);

        const auto values = trim(assignment.substr(equals + 1));
        if (values.starts_with('{')) {
            const auto close = values.find('}');
            if (close == std::string::npos || close == 1) {
                throw InvalidParameterValue("event file");
            }
            info.file = values.substr(1, close - 1);
            info.columns.push_back(trailingStringInt(info.file, info.file, 0));
            const auto period = trim(std::string_view(values).substr(close + 1));
            if (!period.empty()) {
                if (!period.starts_with('+')) {
                    throw InvalidParameterValue("event period");
                }
                info.period = readEventNumber(std::string_view(period).substr(1), "event period");
            }
        } else {
            const auto samples = splitline(values, ',');
            if (samples.empty()) {
                throw InvalidParameterValue("event value");
            }
            for (const auto& value : samples) {
                info.value.push_back(readEventNumber(value, "event value"));
            }
        }
    }
}  // namespace

// @time[,time,...][+period]|object:field(units)=value[,value,...]
// or object:field(units)=value[,value,...] @time[,time,...][+period]
void EventInfo::loadString(std::string_view eventString, CoreObject* rootObj)
{
    for (const auto& entry : gmlc::utilities::stringOps::splitlineBracket(eventString, ";")) {
        readEventAssignment(*this, entry, rootObj);
    }
}

std::unique_ptr<Event>
    makeEvent(std::string_view field, double val, CoreTime eventTime, CoreObject* rootObject)
{
    auto eventObject = std::make_unique<Event>(eventTime);
    const ObjectInfo fdata(std::string{field}, rootObject);
    eventObject->setTarget(fdata.mObject, fdata.mField);
    eventObject->setValue(val, fdata.mUnitType);
    return eventObject;
}

std::unique_ptr<Event> makeEvent(std::string_view eventString, CoreObject* rootObject)
{
    EventInfo gdEI(eventString, rootObject);
    return makeEvent(gdEI, rootObject);
}

std::unique_ptr<Event> makeEvent(EventInfo& gdEI, CoreObject* rootObject)
{
    std::unique_ptr<Event> eventObject;
    if (!gdEI.type.empty()) {
        eventObject = CoreClassFactory<Event>::instance()->createObject(gdEI.type);
        if (eventObject) {
            eventObject->updateEvent(gdEI, rootObject);
            return eventObject;
        }
    }
    auto evType = findEventType(gdEI);

    switch (evType) {
        case EventType::BASIC:
            eventObject = std::make_unique<Event>(gdEI, rootObject);
            break;
        case EventType::COMPOUND:
            eventObject = std::make_unique<events::CompoundEvent>(gdEI, rootObject);
            break;
        case EventType::PLAYER:
            eventObject = std::make_unique<events::Player>(gdEI, rootObject);
            break;
        case EventType::COMPOUND_PLAYER:
            eventObject = std::make_unique<events::CompoundEventPlayer>(gdEI, rootObject);
            break;
        case EventType::INTERPOLATING:
            eventObject = std::make_unique<events::InterpolatingPlayer>(gdEI, rootObject);
            break;
        case EventType::REVERSIBLE:
            eventObject = std::make_unique<events::ReversibleEvent>(gdEI, rootObject);
            break;
        case EventType::TOGGLE:
            break;
    }

    return eventObject;
}

}  // namespace griddyn
