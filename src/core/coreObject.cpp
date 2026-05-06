/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "coreObject.h"

#include "coreExceptions.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewOps.h"
#include "nullObject.h"
#include "utilities/dataDictionary.h"
#include <cassert>
#include <cmath>
#include <string>
#include <unordered_map>

namespace griddyn {
// start at 101 since there are some objects that use low numbers as a check for interface number
// and the id as secondary
std::atomic<id_type_t> coreObject::s_obcnt(101);

coreObject::coreObject(std::string_view objName): m_refCount(0), m_oid(s_obcnt++), name(objName)
{
    static nullObject nullObject0(0);
    // not using updateName since in many cases the id has not been set yet
    if (!name.empty() && (name.back() == '#')) {
        name.pop_back();
        gmlc::utilities::stringOps::appendInteger(name, static_cast<std::uint64_t>(m_oid));
    }
    parent = &nullObject0;
}

// this constructor is only used for building some select nullObjects
coreObject::coreObject(id_type_t coid): m_refCount(0), m_oid(coid)
{
    id = static_cast<index_t>(coid);
    parent = nullptr;
}
coreObject::~coreObject() = default;

static dataDictionary<id_type_t, std::string> descriptionDictionary;

// inherited copy construction method
coreObject* coreObject::clone(coreObject* obj) const
{
    if (obj == nullptr) {
        obj = new coreObject(name);
        descriptionDictionary.copy(m_oid, obj->m_oid);
    }
    obj->enabled = enabled;
    obj->id = id;
    obj->prevTime = prevTime;

    obj->nextUpdateTime = nextUpdateTime;
    obj->lastUpdateTime = lastUpdateTime;
    obj->updatePeriod = updatePeriod;
    obj->updateDelay = updateDelay;

    return obj;
}

void coreObject::updateName()
{
    if (name.empty()) {
        return;
    }
    switch (name.back()) {
        case '$':
            name.pop_back();
            gmlc::utilities::stringOps::appendInteger(name, static_cast<std::uint64_t>(id));
            break;
        case '#':
            name.pop_back();
            gmlc::utilities::stringOps::appendInteger(name, static_cast<std::uint64_t>(m_oid));
            break;
        case '@':
            name.pop_back();
            gmlc::utilities::stringOps::appendInteger(name, static_cast<std::uint64_t>(locIndex));
            break;
        default:
            break;
    }
}

void coreObject::add(coreObject* obj)
{
    if (obj != nullptr) {
        throw(objectAddFailure(this));
    }
}

void coreObject::remove(coreObject* obj)
{
    if (obj != nullptr) {
        throw(objectRemoveFailure(this));
    }
}

void coreObject::addHelper(std::shared_ptr<helperObject> obj)  // NOLINT
{
    if (obj) {
        throw(objectAddFailure(this));
    }
}

void coreObject::addOwningReference()
{
    // use relaxed ordering since no one cares about order on the increment operation
    m_refCount.fetch_add(1, std::memory_order_relaxed);
}

static stringVec locNumStrings{"updateperiod",
                               "updaterate",
                               "nextupdatetime",
                               "basepower",
                               "enabled",
                               "id"};
static const stringVec locStrStrings{"name", "description"};

void coreObject::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    switch (pstype) {
        case paramStringType::all:
            pstr.reserve(pstr.size() + locNumStrings.size() + locStrStrings.size() + 1);
            pstr.insert(pstr.end(), locNumStrings.begin(), locNumStrings.end());
            pstr.emplace_back("#");
            pstr.insert(pstr.end(), locStrStrings.begin(), locStrStrings.end());
            break;
        case paramStringType::localnum:
            pstr = locNumStrings;
            break;
        case paramStringType::localstr:
            pstr = locStrStrings;
            break;
        case paramStringType::numeric:
            pstr.reserve(pstr.size() + locNumStrings.size());
            pstr.insert(pstr.end(), locNumStrings.begin(), locNumStrings.end());
            break;
        case paramStringType::str:
            pstr.reserve(pstr.size() + locStrStrings.size());
            pstr.insert(pstr.end(), locStrStrings.begin(), locStrStrings.end());
            break;
        case paramStringType::localflags:
        case paramStringType::flags:
            break;
    }
}

void coreObject::set(std::string_view param, std::string_view val)
{
    if ((param == "name") || (param == "rename") || (param == "id")) {
        setName(val);
    } else if (param == "description") {
        setDescription(val);
    } else if ((param.empty()) || (param.front() == '#')) {
        // comment parameter meant to do nothing
    } else {
        if (val == "true") {
            setFlag(param, true);
        } else if (val == "false") {
            setFlag(param, false);
        } else {
            auto lower = gmlc::utilities::convertToLowerCase(param);
            if (lower != param) {
                set(lower, val);
                logging::warning(this, "parameters should be lower case \"{}\" is not", param);
            } else {
                logging::warning(this, "parameter {} not found", param);
                throw(unrecognizedParameter(param));
            }
        }
    }
}

void coreObject::setDescription(std::string_view description)  // NOLINT
{
    descriptionDictionary.update(m_oid, std::string{description});
}

std::string coreObject::getDescription() const
{
    return descriptionDictionary.query(m_oid);
}
void coreObject::nameUpdate()
{
    parent->alert(this, OBJECT_NAME_CHANGE);
}
void coreObject::idUpdate()
{
    parent->alert(this, OBJECT_ID_CHANGE);
}
void coreObject::setUpdateTime(double newUpdateTime)
{
    nextUpdateTime = newUpdateTime;
}

// check for potential loops in the parent object
static bool parentReferenceLoop(coreObject* pobj, coreObject* test)
{
    while (!isSameObject(pobj, pobj->getParent())) {
        if (pobj == test) {
            return true;
        }
        pobj = pobj->getParent();
    }
    return false;
}
static nullObject nullObjectEp(0);

void coreObject::setParent(coreObject* parentObj)
{
    if (parentObj == nullptr) {
        parent = &nullObjectEp;
        return;
    }
    if (parentReferenceLoop(parentObj, this)) {
        throw(griddyn::objectAddFailure(this));
    }
    parent = parentObj;
}

void coreObject::setFlag(std::string_view flag, bool val)
{
    if ((flag == "enable") || (flag == "status") || (flag == "enabled")) {
        if (isEnabled() != val) {
            if (val) {
                enable();
            } else {
                disable();
            }
        }
    } else if (flag == "disabled") {
        if (isEnabled() == val)  // looking for opposites to trigger
        {
            if (val) {
                disable();
            } else {
                enable();
            }
        }
    } else if (flag == "updates") {
        enable_updates(val);
    } else if (flag == "searchable") {
        alert(this, OBJECT_IS_SEARCHABLE);
    } else if (flag == "hasupdates") {
        alert(this, UPDATE_REQUIRED);
    } else if ((flag.empty()) || (flag.front() == '#')) {
        // comment parameter meant to do nothing
    } else {
        auto lower = gmlc::utilities::convertToLowerCase(flag);
        if (lower != flag) {
            setFlag(lower, val);
            logging::warning(this, "flags should be lower case \"{}\" is not", flag);
        } else {
            throw(unrecognizedParameter(flag));
        }
    }
}

bool coreObject::getFlag(std::string_view flag) const
{
    bool ret = false;
    if (flag == "enabled") {
        ret = isEnabled();
    } else if (flag == "updates") {
        ret = hasUpdates();
    }
    return ret;
}

double coreObject::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "eventcode") {
        val = 0.0;
    } else if (param == "period") {
        val = units::convert(static_cast<double>(updatePeriod), units::second, unitType);
    } else if ((param == "time") || (param == "currenttime")) {
        val = static_cast<double>(prevTime);
    } else if ((param == "update") || (param == "nextupdate")) {
        val = static_cast<double>(nextUpdateTime);
    } else if (param == "lastupdate") {
        val = static_cast<double>(lastUpdateTime);
    }
    return val;
}

void coreObject::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "updateperiod") || (param == "period")) {
        updatePeriod = units::convert(val, unitType, units::second);
    } else if ((param == "updaterate") || (param == "rate")) {
        double rt = units::convert(val, unitType, units::Hz);
        if (rt <= 0.0) {
            updatePeriod = kBigNum;
        } else {
            updatePeriod = 1.0 / rt;
        }
    } else if (param == "nextupdatetime") {
        nextUpdateTime = units::convert(val, unitType, units::second);
    } else if ((param == "number") || (param == "renumber") || (param == "id")) {
        setUserID(static_cast<index_t>(val));
    } else if ((param.empty()) || (param.front() == '#')) {
        // comment parameter meant to do nothing
    } else {
        try {
            setFlag(param, (val > 0.1));
        }
        catch (const unrecognizedParameter&) {
            auto lower = gmlc::utilities::convertToLowerCase(param);
            if (lower != param) {
                set(lower, val, unitType);
                logging::warning(this, "parameters should be lower case \"{}\" is not", param);
            } else {
                throw;
            }
        }
    }
}

std::string coreObject::getString(std::string_view param) const
{
    std::string out("NA");
    if (param == "name") {
        out = name;
    } else if (param == "description") {
        out = getDescription();
    } else if (param == "parent") {
        if (parent != nullptr) {
            out = parent->getName();
        }
    }
    return out;
}

coreObject* coreObject::getSubObject(std::string_view /*typeName*/, index_t /*num*/) const
{
    return nullptr;
}
coreObject* coreObject::findByUserID(std::string_view /*typeName*/, index_t searchID) const
{
    if (searchID == id) {
        return const_cast<coreObject*>(this);
    }
    return nullptr;
}

void coreObject::updateA(coreTime time)
{
    lastUpdateTime = time;
}
coreTime coreObject::updateB()
{
    assert(nextUpdateTime > negTime / 2.0);  // The assert is to check for spurious calls
    if (nextUpdateTime < maxTime) {
        while (lastUpdateTime >= nextUpdateTime) {
            nextUpdateTime += updatePeriod;
        }
    }
    return nextUpdateTime;
}

void coreObject::enable()
{
    enabled = true;
}
void coreObject::disable()
{
    enabled = false;
}
bool coreObject::isEnabled() const
{
    return enabled;
}
// core objects are clonable derived classes may not be
bool coreObject::isCloneable() const
{
    return true;
}
void coreObject::alert(coreObject* object, int code)
{
    parent->alert(object, code);
}
void coreObject::log(coreObject* object, print_level level, const std::string& message)
{
    parent->log(object, level, message);
}

bool coreObject::shouldLog(print_level level) const
{
    return (parent != nullptr) ? parent->shouldLog(level) : true;
}

void coreObject::makeNewOID()
{
    m_oid = ++s_obcnt;
}
// NOTE: there is some potential for recursion here if the parent object searches in lower objects
// But in some cases you search up, and others you want to search down so we will rely on
// intelligence on the part of the implementer
coreObject* coreObject::find(std::string_view object) const
{
    return (parent->find(object));
}

int coreObject::getInt(std::string_view param) const
{
    return static_cast<int>(get(param));
}

std::string fullObjectName(const coreObject* obj)
{
    if (obj->parent->m_oid != 0U)  // the nullobject oid==0
    {
        if (obj->parent->parent->m_oid != 0U) {
            return fullObjectName(obj->parent) + "::" + obj->getName();  // yay recursion
        }
        return obj
            ->getName();  // the objective is to be searchable from the root object so don't need to
        // list the root object name
    }
    return obj->getName();
}

void removeReference(coreObject* objToDelete)
{
    if (objToDelete != nullptr) {
        // don't do a write unless we absolutely need to
        if (objToDelete->m_refCount <= 1 || --objToDelete->m_refCount <= 0) {
            delete objToDelete;
        }
    }
}

void removeReference(coreObject* objToDelete, const coreObject* parent)
{
    if (objToDelete != nullptr) {
        if (objToDelete->m_refCount <= 1 || --objToDelete->m_refCount <= 0) {
            // don't do a write on an atomic unless we absolutely need to
            delete objToDelete;
        } else if (parent == objToDelete->parent) {
            objToDelete->parent = &nullObjectEp;
        }
    }
}

void setMultipleFlags(coreObject* obj, std::string_view flags)
{
    auto lcflags = gmlc::utilities::convertToLowerCase(std::string{flags});
    auto flgs = gmlc::utilities::string_viewOps::split(lcflags);
    gmlc::utilities::string_viewOps::trim(flgs);
    for (const auto& flag : flgs) {
        if (flag.empty()) {
            continue;
        }
        if (flag.front() != '-') {
            obj->setFlag(std::string{flag}, true);
        } else {
            obj->setFlag(std::string{flag.substr(1)}, false);
        }
    }
}

static const std::unordered_map<std::string, print_level> printLevelsMap{
    {"none", print_level::no_print},
    {"error", print_level::error},
    {"warning", print_level::warning},
    {"normal", print_level::normal},
    {"summary", print_level::summary},
    {"debug", print_level::debug},
    {"trace", print_level::trace},
    {"no_print", print_level::no_print},
    {"error_print", print_level::error},
    {"warning_print", print_level::warning},
    {"normal_print", print_level::normal},
    {"summary_print", print_level::summary},
    {"debug_print", print_level::debug},
    {"trace_print", print_level::trace},
};

print_level stringToPrintLevel(const std::string& level)
{
    auto fnd = printLevelsMap.find(level);
    if (fnd != printLevelsMap.end()) {
        return fnd->second;
    }
    throw(invalidParameterValue(level));
}

}  // namespace griddyn
