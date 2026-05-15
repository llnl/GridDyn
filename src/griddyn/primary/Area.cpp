/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "../Area.h"

#include "../Link.h"
#include "../Relay.h"
#include "../gridBus.h"
#include "../gridDynSimulation.h"
#include "../measurement/objectGrabbers.h"
#include "core/coreExceptions.h"
#include "core/coreObjectList.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "listMaintainer.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::ensureSizeAtLeast;
using gmlc::utilities::vecFindne;
using units::convert;
using units::defunit;
using units::Hz;
using units::MW;
using units::rad;
using units::s;
using units::unit;

std::atomic<count_t> Area::areaCounter{0};

static typeFactory<Area> gf(
    "area", std::to_array<std::string_view>({"basic", "simple"}), "basic");

Area::Area(const std::string& objName): gridPrimary(objName)
{
    // default values
    setUserID(++areaCounter);
    updateName();
    opFlags.set(multipart_calculation_capable);
    obList = std::make_unique<coreObjectList>();
    opObjectLists = std::make_unique<listMaintainer>();
}

coreObject* Area::clone(coreObject* obj) const
{
    auto* area = cloneBase<Area, gridPrimary>(this, obj);
    if (area == nullptr) {
        return obj;
    }

    area->masterBus = masterBus;
    area->fTarget = fTarget;
    // clone all the areas
    for (size_t kk = 0; kk < m_Areas.size(); kk++) {
        if (kk >= area->m_Areas.size()) {
            auto* gA = static_cast<Area*>(m_Areas[kk]->clone());
            area->add(gA);
        } else {
            m_Areas[kk]->clone(area->m_Areas[kk]);
        }
    }
    // clone all the buses
    for (size_t kk = 0; kk < m_Buses.size(); kk++) {
        if (kk >= area->m_Buses.size()) {
            auto* bus = static_cast<gridBus*>(m_Buses[kk]->clone());
            area->add(bus);
        } else {
            m_Buses[kk]->clone(area->m_Buses[kk]);
        }
    }

    // clone all the relays
    for (size_t kk = 0; kk < m_Relays.size(); kk++) {
        if (kk >= area->m_Relays.size()) {
            auto* relay = static_cast<Relay*>(m_Relays[kk]->clone());
            area->add(relay);
        } else {
            m_Relays[kk]->clone(area->m_Relays[kk]);
        }
    }

    // clone all the links
    for (size_t kk = 0; kk < m_Links.size(); kk++) {
        if (kk >= area->m_Links.size()) {
            auto* lnk = static_cast<Link*>(m_Links[kk]->clone());
            // now we need to make sure the links are mapped properly
            for (index_t tt = 0; tt < lnk->terminalCount(); ++tt) {
                auto* bus =
                    static_cast<gridBus*>(findMatchingObject(m_Links[kk]->getBus(tt + 1), area));

                if (bus != nullptr) {
                    lnk->updateBus(bus, tt + 1);
                }
            }
            area->add(lnk);
        } else {
            m_Links[kk]->clone(area->m_Links[kk]);
            for (index_t tt = 0; tt < area->m_Links[kk]->terminalCount(); ++tt) {
                auto* bus =
                    static_cast<gridBus*>(findMatchingObject(m_Links[kk]->getBus(tt + 1), area));
                area->m_Links[kk]->updateBus(bus, tt + 1);
            }
        }
    }

    if ((isRoot()) &&
        (obj ==
         nullptr)) {  // Now make sure to update all the objects linkages in the different objects
        area->updateObjectLinkages(area);
    }

    for (auto& rel : area->m_Relays) {
        rel->updateObject(area, object_update_mode::match);
    }
    return area;
}

void Area::updateObjectLinkages(coreObject* newRoot)
{
    for (auto* obj : primaryObjects) {
        obj->updateObjectLinkages(newRoot);
    }
}

// destructor
Area::~Area()
{
    for (auto* obj : primaryObjects) {
        removeReference(obj, this);
    }
}

void Area::add(coreObject* obj)
{
    if (obj == nullptr) {
        return;
    }
    if (dynamic_cast<gridBus*>(obj) != nullptr) {
        return add(static_cast<gridBus*>(obj));
    }
    if (dynamic_cast<Link*>(obj) != nullptr) {
        return add(static_cast<Link*>(obj));
    }
    if (dynamic_cast<Area*>(obj) != nullptr) {
        return add(static_cast<Area*>(obj));
    }
    if (dynamic_cast<Relay*>(obj) != nullptr) {
        return add(static_cast<Relay*>(obj));
    }

    obj->addOwningReference();
    objectHolder.push_back(obj);
    obj->locIndex = static_cast<index_t>(objectHolder.size()) - 1;
    obj->setParent(this);
    obList->insert(obj);
    if (obj->getNextUpdateTime() < kHalfBigNum)  // check if the object has updates
    {
        alert(obj, UPDATE_REQUIRED);
    }
}

template<class X>
void addObject(Area* area, X* obj, std::vector<X*>& objVector)
{
    if (!area->isMember(obj)) {
        auto insertRes = area->obList->insert(obj);
        if (!insertRes) {
            throw(objectAddFailure(area));
        }
        objVector.push_back(obj);
        obj->setParent(area);
        obj->addOwningReference();
        obj->locIndex = static_cast<index_t>(objVector.size()) - 1;

        obj->set("basepower", area->systemBasePower);
        obj->set("basefreq", area->systemBaseFrequency);
        area->primaryObjects.push_back(obj);
        obj->locIndex2 = static_cast<index_t>(area->primaryObjects.size()) - 1;
        if (area->checkFlag(pFlow_initialized)) {
            area->alert(area, OBJECT_COUNT_INCREASE);
        }
    }
}

void Area::add(gridBus* bus)
{
    addObject(this, bus, m_Buses);
}

void Area::add(Area* area)
{
    addObject(this, area, m_Areas);
}

// add link
void Area::add(Link* lnk)
{
    addObject(this, lnk, m_Links);
}

// add link
void Area::add(Relay* relay)
{
    addObject(this, relay, m_Relays);
}

// --------------- remove components ---------------
void Area::remove(coreObject* obj)
{
    if (obj == nullptr) {
        return;
    }
    if (dynamic_cast<gridBus*>(obj) != nullptr) {
        return remove(static_cast<gridBus*>(obj));
    }
    if (dynamic_cast<Link*>(obj) != nullptr) {
        return remove(static_cast<Link*>(obj));
    }
    if (dynamic_cast<Area*>(obj) != nullptr) {
        return remove(static_cast<Area*>(obj));
    }
    if (dynamic_cast<Relay*>(obj) != nullptr) {
        return remove(static_cast<Relay*>(obj));
    }
    // try removing from the objectHolder List
    if ((!isValidIndex(obj->locIndex, objectHolder)) ||
        (!isSameObject(objectHolder[obj->locIndex], obj))) {
        throw(objectRemoveFailure(this));
    }

    objectHolder[obj->locIndex]->setParent(nullptr);
    if (opFlags[being_deleted]) {
        objectHolder[obj->locIndex] = nullptr;
    } else {
        objectHolder.erase(objectHolder.begin() + obj->locIndex);
        // now shift the indices
        for (auto kk = obj->locIndex; kk < static_cast<index_t>(objectHolder.size()); ++kk) {
            objectHolder[kk]->locIndex = kk;
        }
        obList->remove(obj);
    }
}

template<class X>
void removeObject(Area* area, X* obj, std::vector<X*>& objVector)
{
    if ((!isValidIndex(obj->locIndex, objVector)) ||
        (!isSameObject(objVector[obj->locIndex], obj))) {
        throw(objectRemoveFailure(area));
    }

    objVector[obj->locIndex]->setParent(nullptr);
    if (area->opFlags[being_deleted]) {
        objVector[obj->locIndex] = nullptr;
    } else {
        if (area->checkFlag(pFlow_initialized)) {
            area->alert(area, OBJECT_COUNT_DECREASE);
        }
        objVector.erase(objVector.begin() + obj->locIndex);
        // now shift the indices
        for (auto kk = obj->locIndex; kk < static_cast<index_t>(objVector.size()); ++kk) {
            objVector[kk]->locIndex = kk;
        }
        area->primaryObjects.erase(area->primaryObjects.begin() + obj->locIndex2);
        for (auto kk = obj->locIndex2; kk < static_cast<index_t>(area->primaryObjects.size());
             ++kk) {
            objVector[kk]->locIndex2 = kk;
        }
        area->obList->remove(obj);
    }
}

// remove bus
void Area::remove(gridBus* bus)
{
    removeObject(this, bus, m_Buses);
}

// remove link
void Area::remove(Link* lnk)
{
    removeObject(this, lnk, m_Links);
}

// remove area
void Area::remove(Area* area)
{
    removeObject(this, area, m_Areas);
}

// remove area
void Area::remove(Relay* relay)
{
    removeObject(this, relay, m_Relays);
}

void Area::alert(coreObject* obj, int code)
{
    switch (code) {
        case OBJECT_NAME_CHANGE:
        case OBJECT_ID_CHANGE:
            obList->updateObject(obj);
            break;
        case OBJECT_IS_SEARCHABLE:
            if (isRoot()) {
                obList->insert(obj);
            } else {
                getParent()->alert(obj, code);
            }
            break;
        default:
            gridPrimary::alert(obj, code);
    }
}

gridBus* Area::getBus(index_t x) const
{
    return (isValidIndex(x, m_Buses)) ? m_Buses[x] : nullptr;
}

Link* Area::getLink(index_t x) const
{
    return (isValidIndex(x, m_Links)) ? m_Links[x] : nullptr;
}

Area* Area::getArea(index_t x) const
{
    return (isValidIndex(x, m_Areas)) ? m_Areas[x] : nullptr;
}

Relay* Area::getRelay(index_t x) const
{
    return (isValidIndex(x, m_Relays)) ? m_Relays[x] : nullptr;
}

Generator* Area::getGen(index_t x)
{
    for (auto* a : m_Areas) {
        auto tcnt = static_cast<count_t>(a->get("gencount"));
        if (x < tcnt) {
            return (a->getGen(x));
        }
        x = x - tcnt;
    }
    for (auto* b : m_Buses) {
        count_t tcnt = static_cast<count_t>(b->get("gencount"));
        if (x < tcnt) {
            return b->getGen(x);
        }
        x = x - tcnt;
    }
    return nullptr;
}

coreObject* Area::find(std::string_view objName) const
{
    coreObject* obj = obList->find(objName);
    if (obj == nullptr) {
        auto rlc = objName.find_first_of(":/?@#$!%");
        if (rlc != std::string::npos) {
            obj = locateObject(std::string{objName}, this, false, false);
        }
    }

    if (obj == nullptr) {
        // try searching the subareas
        for (const auto& area : m_Areas) {
            obj = area->find(objName);
            if (obj != nullptr) {
                break;
            }
        }
    }
    return obj;
}

coreObject* Area::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "bus") {
        return getBus(num);
    }
    if (typeName == "link") {
        return getLink(num);
    }
    if (typeName == "area") {
        return getArea(num);
    }
    if (typeName == "relay") {
        return getRelay(num);
    }
    if ((typeName == "object") || (typeName == "subobject")) {
        return (isValidIndex(num, primaryObjects)) ? primaryObjects[num] : nullptr;
    }
    return nullptr;
}

void Area::setAll(std::string_view type, std::string_view param, double val, units::unit unitType)
{
    if (type == "all") {
        set(param, val, unitType);
        for (auto& area : m_Areas) {
            area->setAll(type, param, val, unitType);
        }
        for (auto& obj : primaryObjects) {
            try {
                obj->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    }
    if (type == "area") {
        try {
            set(param, val, unitType);
        }
        catch (const unrecognizedParameter&) {
            // we ignore this exception in this function
        }
        for (auto& area : m_Areas) {
            area->setAll(type, param, val, unitType);
        }
    } else if (type == "bus") {
        for (auto& bus : m_Buses) {
            try {
                bus->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if (type == "link") {
        for (auto& lnk : m_Links) {
            try {
                lnk->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if (type == "relay") {
        for (auto& rel : m_Relays) {
            try {
                rel->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if ((type == "gen") || (type == "load") || (type == "generator") ||
               (type == "secondary")) {
        for (auto& bus : m_Buses) {
            bus->setAll(type, param, val, unitType);
        }
        for (auto& area : m_Areas) {
            area->setAll(type, param, val, unitType);
        }
    }
}

coreObject* Area::findByUserID(std::string_view typeName, index_t searchID) const
{
    if ((typeName == "area") && (searchID == getUserID())) {
        return const_cast<Area*>(this);
    }
    if ((typeName == "gen") || (typeName == "load") || (typeName == "generator")) {
        // this is potentially computationally expensive, wouldn't recommend doing this search in a
        // big system
        for (auto* bus : m_Buses) {
            coreObject* obj = bus->findByUserID(typeName, searchID);
            if (obj != nullptr) {
                return obj;
            }
        }
        for (auto* area : m_Areas) {
            coreObject* obj = area->findByUserID(typeName, searchID);
            if (obj != nullptr) {
                return obj;
            }
        }
        return nullptr;
    }
    auto possObjs = obList->find(searchID);
    if (possObjs.empty()) {
        for (auto* area : m_Areas) {
            coreObject* obj = area->findByUserID(typeName, searchID);
            if (obj != nullptr) {
                return obj;
            }
        }
        return nullptr;
    }
    if (typeName == "bus") {
        for (auto* po : possObjs) {
            if (isValidIndex(po->locIndex, m_Buses)) {
                if (isSameObject(po, m_Buses[po->locIndex])) {
                    return po;
                }
            }
        }
    } else if (typeName == "link") {
        for (auto* po : possObjs) {
            if (isValidIndex(po->locIndex, m_Links)) {
                if (isSameObject(po, m_Links[po->locIndex])) {
                    return po;
                }
            }
        }
    } else if (typeName == "area") {
        for (auto* po : possObjs) {
            if (isValidIndex(po->locIndex, m_Areas)) {
                if (isSameObject(po, m_Areas[po->locIndex])) {
                    return po;
                }
            }
        }
    } else if (typeName == "relay") {
        for (auto* po : possObjs) {
            if (isValidIndex(po->locIndex, m_Relays)) {
                if (isSameObject(po, m_Relays[po->locIndex])) {
                    return po;
                }
            }
        }
    }
    // if we haven't found something try the subareas
    for (auto* area : m_Areas) {
        coreObject* obj = area->findByUserID(typeName, searchID);
        if (obj != nullptr) {
            return obj;
        }
    }
    return nullptr;
}

// check bus members
bool Area::isMember(const coreObject* object) const
{
    return obList->isMember(object);
}

// reset the bus parameters
void Area::reset(reset_levels level)
{
    for (auto* obj : primaryObjects) {
        obj->reset(level);
    }
}

// dynInitializeB states
void Area::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto* obj : primaryObjects) {
        obj->pFlowInitializeA(time0, flags);
    }
}

void Area::pFlowObjectInitializeB()
{
    std::vector<gridPrimary*> lateBObjects;

    // links need to be initialized first so the initial power flow can be computed through the
    // buses
    for (auto* link : m_Links) {
        if (link->checkFlag(late_b_initialize)) {
            lateBObjects.push_back(link);
        } else {
            link->pFlowInitializeB();
        }
    }

    for (auto* area : m_Areas) {
        if (area->checkFlag(late_b_initialize)) {
            lateBObjects.push_back(area);
        } else {
            area->pFlowInitializeB();
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->checkFlag(late_b_initialize)) {
            lateBObjects.push_back(bus);
        } else {
            bus->pFlowInitializeB();
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->checkFlag(late_b_initialize)) {
            lateBObjects.push_back(rel);
        } else {
            rel->pFlowInitializeB();
        }
    }
    for (auto* obj : lateBObjects) {
        obj->pFlowInitializeB();
    }

    opObjectLists->makePreList(primaryObjects);
}

void Area::updateLocalCache()
{
    // links should come first
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            link->updateLocalCache();
        }
    }
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            area->updateLocalCache();
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            bus->updateLocalCache();
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            rel->updateLocalCache();
        }
    }
}

void Area::updateLocalCache(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    // links should come first
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            link->updateLocalCache(inputs, sD, sMode);
        }
    }
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            area->updateLocalCache(inputs, sD, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            bus->updateLocalCache(inputs, sD, sMode);
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            rel->updateLocalCache(inputs, sD, sMode);
        }
    }
}

change_code Area::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level)
{
    auto ret = change_code::no_change;
    opFlags.set(disable_flag_updates);  // this is so the adjustment object list can't get reset in
                                        // the middle of
    // this computation
    if (level < check_level_t::low_voltage_check) {
        for (auto* obj : pFlowAdjustObjects) {
            auto iret = obj->powerFlowAdjust(inputs, flags, level);
            if (iret > ret) {
                ret = iret;
            }
        }
    } else {
        for (auto* obj : primaryObjects) {
            if (obj->isEnabled()) {
                auto iret = obj->powerFlowAdjust(inputs, flags, level);
                if (iret > ret) {
                    ret = iret;
                }
            }
        }
    }
    // unset the lock
    opFlags.reset(disable_flag_updates);
    if (opFlags[flag_update_required]) {
        updateFlags();
    }
    return ret;
}

void Area::pFlowCheck(std::vector<Violation>& Violation_vector)
{
    for (auto* obj : primaryObjects) {
        obj->pFlowCheck(Violation_vector);
    }
}

// dynInitializeB states for dynamic solution
void Area::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto* obj : primaryObjects) {
        if (obj->isEnabled()) {
            obj->dynInitializeA(time0, flags);
        }
    }
}

// dynInitializeB states for dynamic solution part 2  //final clean up
void Area::dynObjectInitializeB(const IOdata& inputs, const IOdata& desiredOutput, IOdata& fieldSet)
{
    std::vector<gridPrimary*> lateBObjects;

    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            if (link->checkFlag(late_b_initialize)) {
                lateBObjects.push_back(link);
            } else {
                link->dynInitializeB(inputs, desiredOutput, fieldSet);
            }
        }
    }
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            if (area->checkFlag(late_b_initialize)) {
                lateBObjects.push_back(area);
            } else {
                area->dynInitializeB(inputs, desiredOutput, fieldSet);
            }
        }
    }
    double pmx = 0;
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            if (bus->checkFlag(late_b_initialize)) {
                lateBObjects.push_back(bus);
            } else {
                bus->dynInitializeB(inputs, desiredOutput, fieldSet);
                double bmx = bus->getMaxGenReal();
                if (bmx > pmx) {
                    pmx = bmx;
                    masterBus = bus->locIndex;
                }
            }
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            if (rel->checkFlag(late_b_initialize)) {
                lateBObjects.push_back(rel);
            } else {
                rel->dynInitializeB(inputs, desiredOutput, fieldSet);
            }
        }
    }
    for (auto* obj : lateBObjects) {
        obj->dynInitializeB(inputs, desiredOutput, fieldSet);
    }

    opObjectLists->makePreList(primaryObjects);
}

// TODO(PT): make this do something or remove it
void Area::updateTheta(coreTime /*time*/) {}

void Area::converge(coreTime time,
                    double state[],
                    double dstate_dt[],
                    const solverMode& sMode,
                    converge_mode mode,
                    double tol)
{
    if (opFlags[reverse_converge]) {
        auto ra = opObjectLists->rbegin(sMode);
        auto rend = opObjectLists->rend(sMode);
        while (ra != rend) {
            (*ra)->converge(time, state, dstate_dt, sMode, mode, tol);
            ++ra;
        }
    } else {
        auto fa = opObjectLists->begin(sMode);
        auto fend = opObjectLists->end(sMode);
        while (fa != fend) {
            (*fa)->converge(time, state, dstate_dt, sMode, mode, tol);
            ++fa;
        }
    }
    // Toggle the reverse indicator every time
    if (opFlags[direction_oscillate]) {
        opFlags.flip(reverse_converge);
    }
}

void Area::setFlag(std::string_view flag, bool val)
{
    if (flag == "reverse_converge") {
        opFlags.set(reverse_converge, val);
    } else if (flag == "direction_oscillate") {
        opFlags.set(direction_oscillate, val);
    } else {
        gridPrimary::setFlag(flag, val);
    }
}

// set properties
void Area::set(std::string_view param, std::string_view val)
{
    gridPrimary::set(param, val);
}

static stringVec locNumStrings{};
static const stringVec locStrStrings{};
static const stringVec flagStrings{};

void Area::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<Area, gridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void Area::set(std::string_view param, double val, unit unitType)
{
    if (param == "basepower") {
        systemBasePower = convert(val, unitType, MW);
        for (auto* obj : primaryObjects) {
            obj->set(param, val);
        }
    } else if ((param == "basefrequency") || (param == "basefreq")) {
        // the default unit in this case should be Hz since that is what everyone assumes but we
        // need to store it in rps NOTE: we only do this assumed conversion for an area/simulation

        systemBaseFrequency = convert(val, (unitType == defunit) ? Hz : unitType, rad / s);

        for (auto* obj : primaryObjects) {
            obj->set(param, systemBaseFrequency, rad / s);
        }
    } else {
        gridPrimary::set(param, val, unitType);
    }
}

double Area::get(std::string_view param, unit unitType) const
{
    double val = 0.0;
    size_t vali = 0;
    if (param == "buscount") {
        vali = m_Buses.size();
    } else if (param == "linkcount") {
        vali = m_Links.size();
    } else if (param == "areacount") {
        vali = m_Areas.size();
    } else if (param == "relaycount") {
        vali = m_Relays.size();
    } else if (param == "totalbuscount") {
        for (auto* gA : m_Areas) {
            val += gA->get(param);
        }
        for (auto* gA : m_Links) {
            val += gA->get("buscount");
        }
        val += static_cast<double>(m_Buses.size());
    } else if (param == "totallinkcount") {
        for (auto* gA : m_Areas) {
            val += gA->get(param);
        }
        for (auto* gA : m_Links) {
            val += gA->get("linkcount");
        }
        // links should return 1 from getting link count so don't need to add the links size again.
    } else if (param == "totalareacount") {
        for (auto* gA : m_Areas) {
            val += gA->get(param);
        }
        val += m_Areas.size();
    } else if (param == "totalrelaycount") {
        for (auto* gA : m_Areas) {
            val += gA->get(param);
        }
        for (auto* gA : m_Links) {
            val += gA->get("relaycount");
        }
        val += m_Relays.size();
    } else if ((param == "gencount") || (param == "loadcount")) {
        for (auto* obj : primaryObjects) {
            double objCount = obj->get(param);
            val += (objCount != kNullVal ? objCount : 0.0);
        }
    } else if (param == "subobjectcount") {
        vali = primaryObjects.size();
    } else if (auto fptr = getObjectFunction(this, std::string{param}).first) {
        auto unit = getObjectFunction(this, std::string{param}).second;
        coreObject* tobj = const_cast<Area*>(this);
        val = convert(fptr(tobj), unit, unitType);
    } else {
        return gridPrimary::get(param, unitType);
    }
    return (vali != 0) ? (static_cast<double>(vali)) : val;
}

void Area::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    // update the tie lines first
    for (auto* gL : m_Links) {
        if (gL->isEnabled()) {
            gL->timestep(time, inputs, sMode);
        }
    }
    for (auto* gA : m_Areas) {
        if (gA->isEnabled()) {
            gA->timestep(time, inputs, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            bus->timestep(time, inputs, sMode);
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            rel->timestep(time, inputs, sMode);
        }
    }
    prevTime = time;
}

count_t Area::getBusVector(std::vector<gridBus*>& busVector, index_t start) const
{
    auto cnt = static_cast<count_t>(m_Buses.size());
    if (cnt > 0) {
        ensureSizeAtLeast(busVector, start + cnt);
        std::copy(m_Buses.begin(), m_Buses.end(), busVector.begin() + start);
    }
    for (auto* area : m_Areas) {
        cnt += area->getBusVector(busVector, start + cnt);
    }
    return cnt;
}

count_t Area::getLinkVector(std::vector<Link*>& linkVector, index_t start) const
{
    auto cnt = static_cast<count_t>(m_Links.size());
    if (cnt > 0) {
        ensureSizeAtLeast(linkVector, static_cast<std::size_t>(start) + cnt);
        std::copy(m_Links.begin(), m_Links.end(), linkVector.begin() + start);
    }
    for (auto* area : m_Areas) {
        cnt += area->getLinkVector(linkVector, start + cnt);
    }
    return cnt;
}

count_t Area::getVoltage(std::vector<double>& voltages, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getVoltage(voltages, start + cnt);
    }

    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(voltages, start + m_Buses.size());
    for (index_t kk = 0; kk < bsize; ++kk) {
        voltages[static_cast<std::size_t>(start) + cnt + kk] = m_Buses[kk]->getVoltage();
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getVoltage(std::vector<double>& voltages,
                         const double state[],
                         const solverMode& sMode,
                         index_t start) const
{
    count_t cnt = 0;

    for (auto* area : m_Areas) {
        cnt += area->getVoltage(voltages, state, sMode, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(voltages, start + bsize);

    for (index_t kk = 0; kk < bsize; ++kk) {
        voltages[start + cnt + kk] = m_Buses[kk]->getVoltage(state, sMode);
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getAngle(std::vector<double>& angles, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getAngle(angles, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(angles, start + bsize);
    for (index_t kk = 0; kk < bsize; ++kk) {
        angles[start + cnt + kk] = m_Buses[kk]->getAngle();
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getAngle(std::vector<double>& angles,
                       const double state[],
                       const solverMode& sMode,
                       index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getAngle(angles, state, sMode, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(angles, start + bsize);
    for (index_t kk = 0; kk < bsize; ++kk) {
        angles[start + cnt + kk] = m_Buses[kk]->getAngle(state, sMode);
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getFreq(std::vector<double>& frequencies, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getFreq(frequencies, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(frequencies, start + bsize);
    for (index_t kk = 0; kk < bsize; ++kk) {
        frequencies[start + cnt + kk] = m_Buses[kk]->getFreq();
    }
    cnt += bsize;
    return cnt;
}

/*
count_t Area::getFreq(std::vector<double> &F, const double state[], const solverMode &sMode, index_t
start) const
{
    count_t cnt = 0;
    for (auto &area : m_Areas)
    {
        cnt += area->getFreq(F, state, sMode, start + cnt);
    }
    if (F.size() < start + m_Buses.size())
    {
        F.resize(start + m_Buses.size());
    }
    for (size_t kk = 0; kk < m_Buses.size(); ++kk)
    {
        F[start + cnt + kk] = m_Buses[kk]->getFreq(state, sMode);
    }
    cnt += static_cast<count_t> (m_Buses.size());
    return cnt;
}
*/

count_t Area::getLinkRealPower(std::vector<double>& powers, index_t start, int busNumber) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getLinkRealPower(powers, start + cnt, busNumber);
    }
    auto Lsize = static_cast<index_t>(m_Links.size());
    ensureSizeAtLeast(powers, start + Lsize);

    for (index_t kk = 0; kk < Lsize; ++kk) {
        powers[start + cnt + kk] = m_Links[kk]->getRealPower(busNumber);
    }
    cnt += Lsize;
    return cnt;
}

count_t Area::getLinkReactivePower(std::vector<double>& powers, index_t start, int busNumber) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getLinkReactivePower(powers, start + cnt, busNumber);
    }
    auto Lsize = static_cast<index_t>(m_Links.size());
    ensureSizeAtLeast(powers, start + Lsize);
    for (index_t kk = 0; kk < Lsize; ++kk) {
        powers[start + cnt + kk] = m_Links[kk]->getReactivePower(busNumber);
    }
    cnt += Lsize;
    return cnt;
}

count_t Area::getBusGenerationReal(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getBusGenerationReal(powers, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(powers, start + bsize);

    for (index_t kk = 0; kk < bsize; ++kk) {
        powers[start + cnt + kk] = m_Buses[kk]->getGenerationReal();
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getBusGenerationReactive(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getBusGenerationReactive(powers, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(powers, start + bsize);
    for (index_t kk = 0; kk < bsize; ++kk) {
        powers[start + cnt + kk] = m_Buses[kk]->getGenerationReactive();
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getBusLoadReal(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getBusLoadReal(powers, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(powers, start + bsize);
    for (index_t kk = 0; kk < bsize; ++kk) {
        powers[start + cnt + kk] = m_Buses[kk]->getLoadReal();
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getBusLoadReactive(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getBusLoadReactive(powers, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(powers, start + bsize);
    for (index_t kk = 0; kk < bsize; ++kk) {
        powers[start + cnt + kk] = m_Buses[kk]->getLoadReactive();
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getLinkLoss(std::vector<double>& losses, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            cnt += area->getLinkLoss(losses, start + cnt);
        }
    }
    auto Lsize = static_cast<index_t>(m_Links.size());
    ensureSizeAtLeast(losses, start + Lsize);
    for (index_t kk = 0; kk < Lsize; ++kk) {
        losses[cnt + kk] = m_Links[kk]->getLoss();
    }
    return cnt + Lsize;
}

count_t Area::getBusName(stringVec& names, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getBusName(names, start + cnt);
    }
    auto bsize = static_cast<index_t>(m_Buses.size());
    ensureSizeAtLeast(names, start + bsize);
    auto nmloc = names.begin() + start + cnt;
    for (auto* bus : m_Buses) {
        *nmloc = bus->getName();
        ++nmloc;
    }
    cnt += bsize;
    return cnt;
}

count_t Area::getLinkName(stringVec& names, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getLinkName(names, static_cast<std::size_t>(start) + cnt);
    }
    auto Lsize = static_cast<index_t>(m_Links.size());
    ensureSizeAtLeast(names, static_cast<std::size_t>(start) + Lsize);
    auto nmloc = names.begin() + start + cnt;
    for (auto* link : m_Links) {
        *nmloc = link->getName();
        ++nmloc;
    }
    cnt += Lsize;
    return cnt;
}

count_t Area::getLinkBus(stringVec& names, index_t start, int busNumber) const
{
    count_t cnt = 0;
    for (auto* area : m_Areas) {
        cnt += area->getLinkBus(names, start + cnt, busNumber);
    }
    auto Lsize = static_cast<index_t>(m_Links.size());
    ensureSizeAtLeast(names, static_cast<std::size_t>(start) + Lsize);
    auto nmloc = names.begin() + start + cnt;
    for (auto* link : m_Links) {
        auto* bus = link->getBus(busNumber);
        if (bus != nullptr) {
            *nmloc = bus->getName();
        }

        ++nmloc;
    }

    cnt += Lsize;
    return cnt;
}

// single value return functions

double Area::getAdjustableCapacityUp(coreTime time) const
{
    double adjUp = 0.0;
    for (auto* area : m_Areas) {
        adjUp += area->getAdjustableCapacityUp(time);
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            adjUp += bus->getAdjustableCapacityUp(time);
        }
    }
    return adjUp;
}

double Area::getAdjustableCapacityDown(coreTime time) const
{
    double adjDown = 0.0;
    for (auto* area : m_Areas) {
        adjDown += area->getAdjustableCapacityDown(time);
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            adjDown += bus->getAdjustableCapacityDown(time);
        }
    }
    return adjDown;
}

double Area::getLoss() const
{
    double loss = 0.0;
    for (auto* area : m_Areas) {
        loss += area->getLoss();
    }
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            loss += link->getLoss();
        }
    }
    for (auto* link : m_externalLinks) {
        if (link->isEnabled()) {  // half of losses of the tie lines get attributed to the area
            loss += 0.5 * link->getLoss();
        }
    }
    return loss;
}

double Area::getGenerationReal() const
{
    double genP = 0.0;
    for (auto* area : m_Areas) {
        genP += area->getGenerationReal();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            genP += bus->getGenerationReal();
        }
    }
    return genP;
}

double Area::getGenerationReactive() const
{
    double genQ = 0.0;
    for (auto* area : m_Areas) {
        genQ += area->getGenerationReactive();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            genQ += bus->getGenerationReactive();
        }
    }
    return genQ;
}

double Area::getLoadReal() const
{
    double loadP = 0.0;
    for (auto* area : m_Areas) {
        loadP += area->getLoadReal();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            loadP += bus->getLoadReal();
        }
    }
    return loadP;
}

double Area::getLoadReactive() const
{
    double loadQ = 0.0;
    for (auto* area : m_Areas) {
        loadQ += area->getLoadReactive();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            loadQ += bus->getLoadReactive();
        }
    }
    return loadQ;
}

double Area::getAvgAngle() const
{
    double a = 0.0;
    double cnt = 0.0;
    for (auto* bus : m_Buses) {
        if (bus->hasInertialAngle()) {
            a += bus->getAngle();
            cnt += 1.0;
        }
    }
    return (a / cnt);
}

double Area::getAvgAngle(const stateData& sD, const solverMode& sMode) const
{
    double a = 0.0;
    double cnt = 0.0;
    for (auto* bus : m_Buses) {
        if (bus->hasInertialAngle()) {
            a += bus->getAngle(sD, sMode);
            cnt += 1.0;
        }
    }

    return (a / cnt);
}

double Area::getAvgFreq() const
{
    double a = 0.0;
    double cnt = 0.0;
    for (auto* bus : m_Buses) {
        if (bus->hasInertialAngle()) {
            a += bus->getFreq();
            cnt += 1.0;
        }
    }
    return (a / cnt);
}

// -------------------- Power Flow --------------------

// guessState the solution
void Area::guessState(coreTime time, double state[], double dstate_dt[], const solverMode& sMode)
{
    auto cobj = opObjectLists->begin(sMode);
    auto cend = opObjectLists->end(sMode);
    while (cobj != cend) {
        (*cobj)->guessState(time, state, dstate_dt, sMode);
        ++cobj;
    }
    // next do any internal control elements
}

void Area::getVariableType(double sdata[], const solverMode& sMode)
{
    auto ra = opObjectLists->begin(sMode);
    auto rend = opObjectLists->end(sMode);
    while (ra != rend) {
        (*ra)->getVariableType(sdata, sMode);
        ++ra;
    }

    // next do any internal area states
}

void Area::getTols(double tols[], const solverMode& sMode)
{
    auto ra = opObjectLists->begin(sMode);
    auto rend = opObjectLists->end(sMode);
    while (ra != rend) {
        (*ra)->getTols(tols, sMode);
        ++ra;
    }
    // next do any internal area states
}

// #define DEBUG_PRINT
void Area::rootTest(const IOdata& inputs,
                    const stateData& sD,
                    double roots[],
                    const solverMode& sMode)
{
    for (auto* ro : rootObjects) {
        ro->rootTest(inputs, sD, roots, sMode);
    }
#ifdef DEBUG_PRINT
    for (size_t kk = 0; kk < rootSize(sMode); ++kk) {
        std::println("t={} root[{}]={}", time, kk, roots[kk]);
    }
#endif
}

change_code Area::rootCheck(const IOdata& inputs,
                            const stateData& sD,
                            const solverMode& sMode,
                            check_level_t level)
{
    change_code ret = change_code::no_change;
    // root checks can trigger flag updates disable and just do the update once
    opFlags.set(disable_flag_updates);
    if (level >= check_level_t::low_voltage_check) {
        for (auto* obj : primaryObjects) {
            if (obj->isEnabled()) {
                auto iret = obj->rootCheck(inputs, sD, sMode, level);
                if (iret > ret) {
                    ret = iret;
                }
            }
        }
    } else {
        for (auto* ro : rootObjects) {
            if (ro->checkFlag(has_alg_roots)) {
                auto iret = ro->rootCheck(inputs, sD, sMode, level);
                if (iret > ret) {
                    ret = iret;
                }
            }
        }
    }
    opFlags.reset(disable_flag_updates);
    if (opFlags[flag_update_required]) {
        updateFlags();
    }
    return ret;
}

void Area::rootTrigger(coreTime time,
                       const IOdata& inputs,
                       const std::vector<int>& rootMask,
                       const solverMode& sMode)
{
    auto RF = vecFindne(rootMask, 0);
    size_t cloc = 0;
    size_t rs = rootSize(sMode);
    size_t rootOffset = offsets.getRootOffset(sMode);

    auto currentRootObject = rootObjects.begin();
    auto obend = rootObjects.end();
    auto ors = (*currentRootObject)->rootSize(sMode);
    opFlags.set(disable_flag_updates);  // root triggers can cause a flag change and the flag update
                                        // currently
    // checks the root object
    // TODO(PT) ::May be wise at some point to revisit the combination of the flags and root object
    // checking
    for (auto rc : RF) {
        if (rc < rootOffset + cloc) {
            continue;
        }
        if (rc >= rootOffset + rs) {
            break;
        }
        while (rc >= rootOffset + cloc + ors) {
            cloc += ors;
            ++currentRootObject;
            ors = (*currentRootObject)->rootSize(sMode);
        }
        (*currentRootObject)->rootTrigger(time, inputs, rootMask, sMode);
        cloc += ors;
        if ((++currentRootObject) == obend) {
            break;
        }
        ors = (*currentRootObject)->rootSize(sMode);
    }
    opFlags.reset(disable_flag_updates);
    if (opFlags[flag_update_required]) {
        updateFlags();
        opFlags.reset(flag_update_required);
    }
}

// pass the solution
void Area::setState(coreTime time,
                    const double state[],
                    const double dstate_dt[],
                    const solverMode& sMode)
{
    prevTime = time;

    // links come first
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            link->setState(time, state, dstate_dt, sMode);
        }
    }
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            area->setState(time, state, dstate_dt, sMode);
        }
    }

    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            bus->setState(time, state, dstate_dt, sMode);
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            rel->setState(time, state, dstate_dt, sMode);
        }
    }
    // next do any internal area states
}

void Area::getVoltageStates(double vStates[], const solverMode& sMode) const

{
    index_t Voffset;
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            area->getVoltageStates(vStates, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            Voffset = bus->getOutputLoc(sMode, voltageInLocation);
            if (Voffset != kNullLocation) {
                vStates[Voffset] = 2.0;
            }
        }
    }
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            if (link->voltageStateCount(sMode) > 0) {
                const auto& linkOffsets = link->getOffsets(sMode);
                Voffset = linkOffsets.vOffset;
                for (index_t kk = 0; kk < link->voltageStateCount(sMode); kk++) {
                    vStates[Voffset + kk] = 2.0;
                }
            }
        }
    }
}

void Area::getAngleStates(double aStates[], const solverMode& sMode) const

{
    index_t Aoffset;
    for (auto* area : m_Areas) {
        if (area->isEnabled()) {
            area->getAngleStates(aStates, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            Aoffset = bus->getOutputLoc(sMode, angleInLocation);
            if (Aoffset != kNullLocation) {
                aStates[Aoffset] = 1.0;
            }
        }
    }
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            if (link->angleStateCount(sMode) > 0) {
                const auto& linkOffsets = link->getOffsets(sMode);
                Aoffset = linkOffsets.aOffset;
                for (index_t kk = 0; kk < link->voltageStateCount(sMode); kk++) {
                    aStates[Aoffset + kk] = 1.0;
                }
            }
        }
    }
}

// residual

void Area::preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    opObjectLists->preEx(inputs, sD, sMode);
}

void Area::residual(const IOdata& inputs,
                    const stateData& sD,
                    double resid[],
                    const solverMode& sMode)
{
    opObjectLists->residual(inputs, sD, resid, sMode);

    // next do any internal states
}

void Area::algebraicUpdate(const IOdata& inputs,
                           const stateData& sD,
                           double update[],
                           const solverMode& sMode,
                           double alpha)
{
    opObjectLists->algebraicUpdate(inputs, sD, update, sMode, alpha);

    // next do any internal states
}

void Area::getStateName(stringVec& stNames,
                        const solverMode& sMode,
                        const std::string& prefix) const
{
    std::string prefix2;
    if (!isRoot()) {
        prefix2 = prefix + getName() + "::";
    } else {
        ensureSizeAtLeast(stNames, offsets.maxIndex(sMode) + 1);
    }
    auto obeg = opObjectLists->cbegin(sMode);
    auto oend = opObjectLists->cend(sMode);
    while (obeg != oend) {
        (*obeg)->getStateName(stNames, sMode, prefix2);
        ++obeg;
    }
}

void Area::delayedResidual(const IOdata& inputs,
                           const stateData& sD,
                           double resid[],
                           const solverMode& sMode)
{
    opObjectLists->delayedResidual(inputs, sD, resid, sMode);
}

void Area::delayedDerivative(const IOdata& inputs,
                             const stateData& sD,
                             double deriv[],
                             const solverMode& sMode)
{
    opObjectLists->delayedDerivative(inputs, sD, deriv, sMode);
}

void Area::delayedJacobian(const IOdata& inputs,
                           const stateData& sD,
                           matrixData<double>& md,
                           const IOlocs& inputLocs,
                           const solverMode& sMode)
{
    opObjectLists->delayedJacobian(inputs, sD, md, inputLocs, sMode);
}

void Area::delayedAlgebraicUpdate(const IOdata& inputs,
                                  const stateData& sD,
                                  double update[],
                                  const solverMode& sMode,
                                  double alpha)
{
    opObjectLists->delayedAlgebraicUpdate(inputs, sD, update, sMode, alpha);
}

void Area::derivative(const IOdata& inputs,
                      const stateData& sD,
                      double deriv[],
                      const solverMode& sMode)
{
    opObjectLists->derivative(inputs, sD, deriv, sMode);
    // next do any internal states
}

// Jacobian
void Area::jacobianElements(const IOdata& inputs,
                            const stateData& sD,
                            matrixData<double>& md,
                            const IOlocs& inputLocs,
                            const solverMode& sMode)
{
    opObjectLists->jacobianElements(inputs, sD, md, inputLocs, sMode);
    // next do any internal control elements
}

void Area::updateFlags(bool /*dynOnly*/)
{
    pFlowAdjustObjects.clear();
    opFlags &= (~flagMask);  // clear the cascading flags

    for (auto* obj : primaryObjects) {
        if (obj->isEnabled()) {
            opFlags |= obj->cascadingFlags();
            if (obj->checkFlag(has_powerflow_adjustments)) {
                pFlowAdjustObjects.push_back(obj);
            }
        }
    }
}

void Area::setOffsets(const solverOffsets& newOffsets, const solverMode& sMode)
{
    if (!(isStateCountLoaded(sMode))) {
        loadStateSizes(sMode);
    }
    offsets.setOffsets(newOffsets, sMode);
    solverOffsets no(newOffsets);
    no.localIncrement(offsets.getOffsets(sMode));

    for (auto* obj : primaryObjects) {
        obj->setOffsets(no, sMode);
        no.increment(obj->getOffsets(sMode));
    }
}

void Area::setOffset(index_t offset, const solverMode& sMode)
{
    if (!isEnabled()) {
        return;
    }
    for (auto* obj : primaryObjects) {
        obj->setOffset(offset, sMode);
        offset += obj->stateSize(sMode);
    }
    offsets.setOffset(offset, sMode);
}

void Area::setRootOffset(index_t Roffset, const solverMode& sMode)
{
    offsets.setRootOffset(Roffset, sMode);
    const auto& so = offsets.getOffsets(sMode);
    auto nR = so.local.algRoots + so.local.diffRoots;
    for (auto* ro : rootObjects) {
        ro->setRootOffset(Roffset + nR, sMode);
        nR += ro->rootSize(sMode);
    }
}

double Area::getTieFlowReal() const
{
    return (getGenerationReal() - getLoadReal() - getLoss());
}

double Area::getMasterAngle(const stateData& sD, const solverMode& sMode) const
{
    if (masterBus >= 0) {
        return m_Buses[masterBus]->getAngle(sD, sMode);
    }
    if (!isRoot()) {
        return static_cast<Area*>(getParent())->getMasterAngle(sD, sMode);
    }
    if (!m_Buses.empty()) {
        return m_Buses[0]->getAngle(sD, sMode);
    }
    return 0.0;
}

stateSizes Area::LocalStateSizes(const solverMode& /*sMode*/) const
{
    return offsets.local().local;
}

count_t Area::LocalJacobianCount(const solverMode& /*sMode*/) const
{
    return offsets.local().local.jacSize;
}

std::pair<count_t, count_t> Area::LocalRootCount(const solverMode& /*sMode*/) const
{
    const auto& lc = offsets.local().local;
    return std::make_pair(lc.algRoots, lc.diffRoots);
}

void Area::loadStateSizes(const solverMode& sMode)
{
    if (isStateCountLoaded(sMode)) {
        return;
    }
    auto& so = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        so.reset();
        so.setLoaded();
        return;
    }

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        so.stateReset();
    }
    auto selfSizes = LocalStateSizes(sMode);
    if (hasAlgebraic(sMode)) {
        so.local.aSize = selfSizes.aSize;
        so.local.vSize = selfSizes.vSize;
        so.local.algSize = selfSizes.algSize;
    }
    if (hasDifferential(sMode)) {
        so.local.diffSize = selfSizes.diffSize;
    }

    so.localStateLoad(false);
    for (auto& sub : primaryObjects) {
        if (sub->isEnabled()) {
            if (!(sub->isStateCountLoaded(sMode))) {
                sub->loadStateSizes(sMode);
            }
            if (sub->checkFlag(sampled_only)) {
                continue;
            }
            so.addStateSizes(sub->getOffsets(sMode));
        }
    }
    so.stateLoaded = true;
    opObjectLists->makeList(sMode, primaryObjects);
}

void Area::loadRootSizes(const solverMode& sMode)
{
    if (isRootCountLoaded(sMode)) {
        return;
    }
    auto& so = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        so.reset();
        so.setLoaded();
        return;
    }
    if (!isDynamic(sMode)) {
        so.rootCountReset();
        so.rootsLoaded = true;
        return;
    }

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        so.rootCountReset();
    }
    auto selfSizes = LocalRootCount(sMode);
    if (!(so.rootsLoaded)) {
        so.local.algRoots = selfSizes.first;
        so.local.diffRoots = selfSizes.second;
    }
    rootObjects.clear();
    for (auto& obj : primaryObjects) {
        if (!(obj->isRootCountLoaded(sMode))) {
            obj->loadRootSizes(sMode);
        }
        if (obj->checkFlag(has_roots)) {
            rootObjects.push_back(obj);
        }
        so.addRootSizes(obj->getOffsets(sMode));
    }
    so.rootsLoaded = true;
}

void Area::loadJacobianSizes(const solverMode& sMode)
{
    if (isJacobianCountLoaded(sMode)) {
        return;
    }
    auto& so = offsets.getOffsets(sMode);
    if (!isEnabled()) {
        so.reset();
        so.setLoaded();
        return;
    }

    if (!isLocal(sMode))  // don't reset if it is the local offsets
    {
        so.JacobianCountReset();
    }
    auto selfJacCount = LocalJacobianCount(sMode);
    if (!(so.jacobianLoaded)) {
        so.local.jacSize = selfJacCount;
    }

    for (auto& obj : primaryObjects) {
        if (!(obj->isJacobianCountLoaded(sMode))) {
            obj->loadJacobianSizes(sMode);
        }
        so.addJacobianSizes(obj->getOffsets(sMode));
    }
}

Area* getMatchingArea(Area* area, gridPrimary* src, gridPrimary* sec)
{
    if (area->isRoot()) {
        return nullptr;
    }

    if (isSameObject(area->getParent(), src))  // if this is true then things are easy
    {
        return sec->getArea(area->locIndex);
    }

    std::vector<index_t> lkind;
    auto* par = dynamic_cast<gridPrimary*>(area->getParent());
    if (par == nullptr) {
        return nullptr;
    }
    lkind.push_back(area->locIndex);

    while (!isSameObject(par, src)) {
        lkind.push_back(par->locIndex);
        par = dynamic_cast<gridPrimary*>(par->getParent());
        if (par == nullptr) {
            return nullptr;
        }
    }
    // now work our way backwards through the secondary
    par = sec;
    for (auto kk = lkind.size() - 1; kk > 0; --kk) {
        par = static_cast<gridPrimary*>(par->getArea(lkind[kk]));
    }
    return par->getArea(lkind[0]);
}

}  // namespace griddyn
