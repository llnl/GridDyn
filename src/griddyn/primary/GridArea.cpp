/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "../GridArea.h"

#include "../GridBus.h"
#include "../GridDynSimulation.h"
#include "../Link.h"
#include "../Relay.h"
#include "../measurement/ObjectGrabbers.h"
#include "ListMaintainer.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectList.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/vectorOps.hpp"
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

std::atomic<count_t> GridArea::areaCounter{0};

static TypeFactory<GridArea>
    gAreaFactory("area", std::to_array<std::string_view>({"basic", "simple"}), "basic");

GridArea::GridArea(const std::string& objName): GridPrimary(objName)
{
    // default values
    setUserID(++areaCounter);
    updateName();
    opFlags.set(MULTIPART_CALCULATION_CAPABLE);
    obList = std::make_unique<CoreObjectList>();
    opObjectLists = std::make_unique<ListMaintainer>();
}

CoreObject* GridArea::clone(CoreObject* obj) const  // NOLINT(misc-no-recursion)
{
    auto* area = cloneBase<GridArea, GridPrimary>(this, obj);
    if (area == nullptr) {
        return obj;
    }

    area->masterBus = masterBus;
    area->fTarget = fTarget;
    // clone all the areas
    for (size_t kk = 0; kk < m_GridAreas.size(); kk++) {
        if (kk >= area->m_GridAreas.size()) {
            auto* gridArea = static_cast<GridArea*>(m_GridAreas[kk]->clone());
            area->add(gridArea);
        } else {
            m_GridAreas[kk]->clone(area->m_GridAreas[kk]);
        }
    }
    // clone all the buses
    for (size_t kk = 0; kk < m_Buses.size(); kk++) {
        if (kk >= area->m_Buses.size()) {
            auto* bus = static_cast<GridBus*>(m_Buses[kk]->clone());
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
                    static_cast<GridBus*>(findMatchingObject(m_Links[kk]->getBus(tt + 1), area));

                if (bus != nullptr) {
                    lnk->updateBus(bus, tt + 1);
                }
            }
            area->add(lnk);
        } else {
            m_Links[kk]->clone(area->m_Links[kk]);
            for (index_t tt = 0; tt < area->m_Links[kk]->terminalCount(); ++tt) {
                auto* bus =
                    static_cast<GridBus*>(findMatchingObject(m_Links[kk]->getBus(tt + 1), area));
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
        rel->updateObject(area, ObjectUpdateMode::MATCH);
    }
    return area;
}

void GridArea::updateObjectLinkages(CoreObject* newRoot)
{
    for (auto* obj : primaryObjects) {
        obj->updateObjectLinkages(newRoot);
    }
}

// destructor
GridArea::~GridArea()
{
    for (auto* obj : primaryObjects) {
        removeReference(obj, this);
    }
}

void GridArea::add(CoreObject* obj)
{
    if (obj == nullptr) {
        return;
    }
    if (dynamic_cast<GridBus*>(obj) != nullptr) {
        add(static_cast<GridBus*>(obj));
        return;
    }
    if (dynamic_cast<Link*>(obj) != nullptr) {
        add(static_cast<Link*>(obj));
        return;
    }
    if (dynamic_cast<GridArea*>(obj) != nullptr) {
        add(static_cast<GridArea*>(obj));
        return;
    }
    if (dynamic_cast<Relay*>(obj) != nullptr) {
        add(static_cast<Relay*>(obj));
        return;
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
void addObject(GridArea* area, X* obj, std::vector<X*>& objVector)
{
    if (!area->isMember(obj)) {
        auto insertRes = area->obList->insert(obj);
        if (!insertRes) {
            throw(ObjectAddFailure(area));
        }
        objVector.push_back(obj);
        obj->setParent(area);
        obj->addOwningReference();
        obj->locIndex = static_cast<index_t>(objVector.size()) - 1;

        obj->set("basepower", area->systemBasePower);
        obj->set("basefreq", area->systemBaseFrequency);
        area->primaryObjects.push_back(obj);
        obj->locIndex2 = static_cast<index_t>(area->primaryObjects.size()) - 1;
        if (area->checkFlag(POWERFLOW_INITIALIZED)) {
            area->alert(area, OBJECT_COUNT_INCREASE);
        }
    }
}

void GridArea::add(GridBus* bus)
{
    addObject(this, bus, m_Buses);
}

void GridArea::add(GridArea* area)
{
    addObject(this, area, m_GridAreas);
}

// add link
void GridArea::add(Link* lnk)
{
    addObject(this, lnk, m_Links);
}

// add link
void GridArea::add(Relay* relay)
{
    addObject(this, relay, m_Relays);
}

// --------------- remove components ---------------
void GridArea::remove(CoreObject* obj)
{
    if (obj == nullptr) {
        return;
    }
    if (dynamic_cast<GridBus*>(obj) != nullptr) {
        remove(static_cast<GridBus*>(obj));
        return;
    }
    if (dynamic_cast<Link*>(obj) != nullptr) {
        remove(static_cast<Link*>(obj));
        return;
    }
    if (dynamic_cast<GridArea*>(obj) != nullptr) {
        remove(static_cast<GridArea*>(obj));
        return;
    }
    if (dynamic_cast<Relay*>(obj) != nullptr) {
        remove(static_cast<Relay*>(obj));
        return;
    }
    // try removing from the objectHolder List
    if ((!isValidIndex(obj->locIndex, objectHolder)) ||
        (!isSameObject(objectHolder[obj->locIndex], obj))) {
        throw(ObjectRemoveFailure(this));
    }

    objectHolder[obj->locIndex]->setParent(nullptr);
    if (opFlags[BEING_DELETED]) {
        objectHolder[obj->locIndex] = nullptr;
    } else {
        objectHolder.erase(objectHolder.begin() + obj->locIndex);
        // now shift the indices
        for (auto kk = obj->locIndex; std::cmp_less(kk, objectHolder.size()); ++kk) {
            objectHolder[kk]->locIndex = kk;
        }
        obList->remove(obj);
    }
}

template<class X>
void removeObject(GridArea* area, X* obj, std::vector<X*>& objVector)
{
    if ((!isValidIndex(obj->locIndex, objVector)) ||
        (!isSameObject(objVector[obj->locIndex], obj))) {
        throw(ObjectRemoveFailure(area));
    }

    objVector[obj->locIndex]->setParent(nullptr);
    if (area->opFlags[BEING_DELETED]) {
        objVector[obj->locIndex] = nullptr;
    } else {
        if (area->checkFlag(POWERFLOW_INITIALIZED)) {
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
void GridArea::remove(GridBus* bus)
{
    removeObject(this, bus, m_Buses);
}

// remove link
void GridArea::remove(Link* lnk)
{
    removeObject(this, lnk, m_Links);
}

// remove area
void GridArea::remove(GridArea* area)
{
    removeObject(this, area, m_GridAreas);
}

// remove area
void GridArea::remove(Relay* relay)
{
    removeObject(this, relay, m_Relays);
}

void GridArea::alert(CoreObject* obj, int code)
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
            GridPrimary::alert(obj, code);
    }
}

GridBus* GridArea::getBus(index_t index) const
{
    return (isValidIndex(index, m_Buses)) ? m_Buses[index] : nullptr;
}

Link* GridArea::getLink(index_t index) const
{
    return (isValidIndex(index, m_Links)) ? m_Links[index] : nullptr;
}

GridArea* GridArea::getArea(index_t index) const
{
    return (isValidIndex(index, m_GridAreas)) ? m_GridAreas[index] : nullptr;
}

GridArea* GridArea::getGridArea(index_t index) const
{
    return getArea(index);
}

Relay* GridArea::getRelay(index_t index) const
{
    return (isValidIndex(index, m_Relays)) ? m_Relays[index] : nullptr;
}

Generator* GridArea::getGen(index_t index)  // NOLINT(misc-no-recursion)
{
    for (auto* areaObject : m_GridAreas) {
        const auto tcnt = static_cast<count_t>(areaObject->get("gencount"));
        if (index < tcnt) {
            return (areaObject->getGen(index));
        }
        index = index - tcnt;
    }
    for (auto* busObject : m_Buses) {
        const auto tcnt = static_cast<count_t>(busObject->get("gencount"));
        if (index < tcnt) {
            return busObject->getGen(index);
        }
        index = index - tcnt;
    }
    return nullptr;
}

CoreObject* GridArea::find(std::string_view objName) const  // NOLINT(misc-no-recursion)
{
    CoreObject* obj = obList->find(objName);
    if (obj == nullptr) {
        auto rlc = objName.find_first_of(":/?@#$!%");
        if (rlc != std::string::npos) {
            obj = locateObject(std::string{objName}, this, false, false);
        }
    }

    if (obj == nullptr) {
        // try searching the subareas
        for (const auto& area : m_GridAreas) {
            obj = area->find(objName);
            if (obj != nullptr) {
                break;
            }
        }
    }
    return obj;
}

CoreObject* GridArea::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "bus") {
        return getBus(num);
    }
    if (typeName == "link") {
        return getLink(num);
    }
    if (typeName == "area") {
        return getGridArea(num);
    }
    if (typeName == "relay") {
        return getRelay(num);
    }
    if ((typeName == "object") || (typeName == "subobject")) {
        return (isValidIndex(num, primaryObjects)) ? primaryObjects[num] : nullptr;
    }
    return nullptr;
}

void GridArea::setAll(std::string_view type,
                      std::string_view param,
                      double val,
                      units::unit unitType)  // NOLINT(misc-no-recursion)
{
    if (type == "all") {
        set(param, val, unitType);
        for (auto& area : m_GridAreas) {
            area->setAll(type, param, val, unitType);
        }
        for (auto& obj : primaryObjects) {
            try {
                obj->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                continue;
            }
        }
    }
    if (type == "area") {
        try {
            set(param, val, unitType);
        }
        catch (const UnrecognizedParameter&) {
            static_cast<void>(0);
        }
        for (auto& area : m_GridAreas) {
            area->setAll(type, param, val, unitType);
        }
    } else if (type == "bus") {
        for (auto& bus : m_Buses) {
            try {
                bus->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                continue;
            }
        }
    } else if (type == "link") {
        for (auto& lnk : m_Links) {
            try {
                lnk->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                continue;
            }
        }
    } else if (type == "relay") {
        for (auto& rel : m_Relays) {
            try {
                rel->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                continue;
            }
        }
    } else if ((type == "gen") || (type == "load") || (type == "generator") ||
               (type == "secondary")) {
        for (auto& bus : m_Buses) {
            bus->setAll(type, param, val, unitType);
        }
        for (auto& area : m_GridAreas) {
            area->setAll(type, param, val, unitType);
        }
    }
}

CoreObject* GridArea::findByUserID(std::string_view typeName,
                                   index_t searchID) const  // NOLINT(misc-no-recursion)
{
    if ((typeName == "area") && (searchID == getUserID())) {
        return const_cast<GridArea*>(this);
    }
    if ((typeName == "gen") || (typeName == "load") || (typeName == "generator")) {
        // this is potentially computationally expensive, wouldn't recommend doing this search in a
        // big system
        for (auto* bus : m_Buses) {
            CoreObject* obj = bus->findByUserID(typeName, searchID);
            if (obj != nullptr) {
                return obj;
            }
        }
        for (auto* area : m_GridAreas) {
            CoreObject* obj = area->findByUserID(typeName, searchID);
            if (obj != nullptr) {
                return obj;
            }
        }
        return nullptr;
    }
    auto possObjs = obList->find(searchID);
    if (possObjs.empty()) {
        for (auto* area : m_GridAreas) {
            CoreObject* obj = area->findByUserID(typeName, searchID);
            if (obj != nullptr) {
                return obj;
            }
        }
        return nullptr;
    }
    if (typeName == "bus") {
        for (auto* possibleObject : possObjs) {
            if (isValidIndex(possibleObject->locIndex, m_Buses)) {
                if (isSameObject(possibleObject, m_Buses[possibleObject->locIndex])) {
                    return possibleObject;
                }
            }
        }
    } else if (typeName == "link") {
        for (auto* possibleObject : possObjs) {
            if (isValidIndex(possibleObject->locIndex, m_Links)) {
                if (isSameObject(possibleObject, m_Links[possibleObject->locIndex])) {
                    return possibleObject;
                }
            }
        }
    } else if (typeName == "area") {
        for (auto* possibleObject : possObjs) {
            if (isValidIndex(possibleObject->locIndex, m_GridAreas)) {
                if (isSameObject(possibleObject, m_GridAreas[possibleObject->locIndex])) {
                    return possibleObject;
                }
            }
        }
    } else if (typeName == "relay") {
        for (auto* possibleObject : possObjs) {
            if (isValidIndex(possibleObject->locIndex, m_Relays)) {
                if (isSameObject(possibleObject, m_Relays[possibleObject->locIndex])) {
                    return possibleObject;
                }
            }
        }
    }
    // if we haven't found something try the subareas
    for (auto* area : m_GridAreas) {
        CoreObject* obj = area->findByUserID(typeName, searchID);
        if (obj != nullptr) {
            return obj;
        }
    }
    return nullptr;
}

// check bus members
bool GridArea::isMember(const CoreObject* object) const
{
    return obList->isMember(object);
}

// reset the bus parameters
void GridArea::reset(ResetLevels level)
{
    for (auto* obj : primaryObjects) {
        obj->reset(level);
    }
}

// dynInitializeB states
void GridArea::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    for (auto* obj : primaryObjects) {
        obj->pFlowInitializeA(time0, flags);
    }
}

void GridArea::pFlowObjectInitializeB()
{
    std::vector<GridPrimary*> lateBObjects;

    // links need to be initialized first so the initial power flow can be computed through the
    // buses
    for (auto* link : m_Links) {
        if (link->checkFlag(LATE_B_INITIALIZE)) {
            lateBObjects.push_back(link);
        } else {
            link->pFlowInitializeB();
        }
    }

    for (auto* area : m_GridAreas) {
        if (area->checkFlag(LATE_B_INITIALIZE)) {
            lateBObjects.push_back(area);
        } else {
            area->pFlowInitializeB();
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->checkFlag(LATE_B_INITIALIZE)) {
            lateBObjects.push_back(bus);
        } else {
            bus->pFlowInitializeB();
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->checkFlag(LATE_B_INITIALIZE)) {
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

void GridArea::updateLocalCache()  // NOLINT(misc-no-recursion)
{
    // links should come first
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            link->updateLocalCache();
        }
    }
    for (auto* area : m_GridAreas) {
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

void GridArea::updateLocalCache(const IOdata& inputs,
                                const StateData& stateDataValue,
                                const SolverMode& sMode)  // NOLINT(misc-no-recursion)
{
    // links should come first
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            link->updateLocalCache(inputs, stateDataValue, sMode);
        }
    }
    for (auto* area : m_GridAreas) {
        if (area->isEnabled()) {
            area->updateLocalCache(inputs, stateDataValue, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            bus->updateLocalCache(inputs, stateDataValue, sMode);
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            rel->updateLocalCache(inputs, stateDataValue, sMode);
        }
    }
}

ChangeCode GridArea::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, CheckLevel level)
{
    auto ret = ChangeCode::NO_CHANGE;
    opFlags.set(DISABLE_FLAG_UPDATES);  // this is so the adjustment object list can't get reset in
                                        // the middle of
    // this computation
    if (level < CheckLevel::LOW_VOLTAGE_CHECK) {
        for (auto* obj : pFlowAdjustObjects) {
            auto iret = obj->powerFlowAdjust(inputs, flags, level);
            ret = std::max(iret, ret);
        }
    } else {
        for (auto* obj : primaryObjects) {
            if (obj->isEnabled()) {
                auto iret = obj->powerFlowAdjust(inputs, flags, level);
                ret = std::max(iret, ret);
            }
        }
    }
    // unset the lock
    opFlags.reset(DISABLE_FLAG_UPDATES);
    if (opFlags[FLAG_UPDATE_REQUIRED]) {
        updateFlags();
    }
    return ret;
}

void GridArea::pFlowCheck(std::vector<Violation>& violationVector)
{
    for (auto* obj : primaryObjects) {
        obj->pFlowCheck(violationVector);
    }
}

// dynInitializeB states for dynamic solution
void GridArea::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    for (auto* obj : primaryObjects) {
        if (obj->isEnabled()) {
            obj->dynInitializeA(time0, flags);
        }
    }
}

// dynInitializeB states for dynamic solution part 2  //final clean up
void GridArea::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    std::vector<GridPrimary*> lateBObjects;

    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            if (link->checkFlag(LATE_B_INITIALIZE)) {
                lateBObjects.push_back(link);
            } else {
                link->dynInitializeB(inputs, desiredOutput, fieldSet);
            }
        }
    }
    for (auto* area : m_GridAreas) {
        if (area->isEnabled()) {
            if (area->checkFlag(LATE_B_INITIALIZE)) {
                lateBObjects.push_back(area);
            } else {
                area->dynInitializeB(inputs, desiredOutput, fieldSet);
            }
        }
    }
    double pmx = 0;
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            if (bus->checkFlag(LATE_B_INITIALIZE)) {
                lateBObjects.push_back(bus);
            } else {
                bus->dynInitializeB(inputs, desiredOutput, fieldSet);
                const double bmx = bus->getMaxGenReal();
                if (bmx > pmx) {
                    pmx = bmx;
                    masterBus = bus->locIndex;
                }
            }
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            if (rel->checkFlag(LATE_B_INITIALIZE)) {
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
void GridArea::updateTheta(CoreTime /*time*/) {}

void GridArea::converge(CoreTime time,
                        double state[],
                        double dstateDt[],
                        const SolverMode& sMode,
                        ConvergeMode mode,
                        double tol)
{
    if (opFlags[REVERSE_CONVERGE]) {
        auto reverseIter = opObjectLists->rbegin(sMode);
        auto rend = opObjectLists->rend(sMode);
        while (reverseIter != rend) {
            (*reverseIter)->converge(time, state, dstateDt, sMode, mode, tol);
            ++reverseIter;
        }
    } else {
        auto forwardIter = opObjectLists->begin(sMode);
        auto fend = opObjectLists->end(sMode);
        while (forwardIter != fend) {
            (*forwardIter)->converge(time, state, dstateDt, sMode, mode, tol);
            ++forwardIter;
        }
    }
    // Toggle the reverse indicator every time
    if (opFlags[DIRECTION_OSCILLATE]) {
        opFlags.flip(REVERSE_CONVERGE);
    }
}

void GridArea::setFlag(std::string_view flag, bool val)
{
    if (flag == "reverse_converge") {
        opFlags.set(REVERSE_CONVERGE, val);
    } else if (flag == "direction_oscillate") {
        opFlags.set(DIRECTION_OSCILLATE, val);
    } else {
        GridPrimary::setFlag(flag, val);
    }
}

// set properties
void GridArea::set(std::string_view param, std::string_view val)
{
    GridPrimary::set(param, val);
}

static stringVec gLocNumStrings{};
static const stringVec LOC_STR_STRINGS{};
static const stringVec FLAG_STRINGS{};

void GridArea::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<GridArea, GridComponent>(
        this, pstr, gLocNumStrings, LOC_STR_STRINGS, FLAG_STRINGS, pstype);
}

void GridArea::set(std::string_view param, double val, unit unitType)
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
        GridPrimary::set(param, val, unitType);
    }
}

double GridArea::get(std::string_view param, unit unitType) const  // NOLINT(misc-no-recursion)
{
    double val = 0.0;
    size_t vali = 0;
    if (param == "buscount") {
        vali = m_Buses.size();
    } else if (param == "linkcount") {
        vali = m_Links.size();
    } else if (param == "areacount") {
        vali = m_GridAreas.size();
    } else if (param == "relaycount") {
        vali = m_Relays.size();
    } else if (param == "totalbuscount") {
        for (auto* gridArea : m_GridAreas) {
            val += gridArea->get(param);
        }
        for (auto* linkObject : m_Links) {
            val += linkObject->get("buscount");
        }
        val += static_cast<double>(m_Buses.size());
    } else if (param == "totallinkcount") {
        for (auto* gridArea : m_GridAreas) {
            val += gridArea->get(param);
        }
        for (auto* linkObject : m_Links) {
            val += linkObject->get("linkcount");
        }
        // links should return 1 from getting link count so don't need to add the links size again.
    } else if (param == "totalareacount") {
        for (auto* gridArea : m_GridAreas) {
            val += gridArea->get(param);
        }
        val += m_GridAreas.size();
    } else if (param == "totalrelaycount") {
        for (auto* gridArea : m_GridAreas) {
            val += gridArea->get(param);
        }
        for (auto* linkObject : m_Links) {
            val += linkObject->get("relaycount");
        }
        val += m_Relays.size();
    } else if ((param == "gencount") || (param == "loadcount")) {
        for (auto* obj : primaryObjects) {
            double objCount = obj->get(param);
            val += (objCount != kNullVal ? objCount : 0.0);
        }
    } else if (param == "subobjectcount") {
        vali = primaryObjects.size();
    } else if (auto fptr = getObjectFunction(this, param).first) {
        auto unit = getObjectFunction(this, param).second;
        CoreObject* tobj = const_cast<GridArea*>(this);
        val = convert(fptr(tobj), unit, unitType);
    } else {
        return GridPrimary::get(param, unitType);
    }
    return (vali != 0) ? (static_cast<double>(vali)) : val;
}

void GridArea::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    // update the tie lines first
    for (auto* gL : m_Links) {
        if (gL->isEnabled()) {
            gL->timestep(time, inputs, sMode);
        }
    }
    for (auto* gA : m_GridAreas) {
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

count_t GridArea::getBusVector(std::vector<GridBus*>& busVector, index_t start) const
{
    auto cnt = static_cast<count_t>(m_Buses.size());
    if (cnt > 0) {
        ensureSizeAtLeast(busVector, start + cnt);
        std::copy(m_Buses.begin(), m_Buses.end(), busVector.begin() + start);
    }
    for (auto* area : m_GridAreas) {
        cnt += area->getBusVector(busVector, start + cnt);
    }
    return cnt;
}

count_t GridArea::getLinkVector(std::vector<Link*>& linkVector, index_t start) const
{
    auto cnt = static_cast<count_t>(m_Links.size());
    if (cnt > 0) {
        ensureSizeAtLeast(linkVector, static_cast<std::size_t>(start) + cnt);
        std::copy(m_Links.begin(), m_Links.end(), linkVector.begin() + start);
    }
    for (auto* area : m_GridAreas) {
        cnt += area->getLinkVector(linkVector, start + cnt);
    }
    return cnt;
}

count_t GridArea::getVoltage(std::vector<double>& voltages, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getVoltage(std::vector<double>& voltages,
                             const double state[],
                             const SolverMode& sMode,
                             index_t start) const
{
    count_t cnt = 0;

    for (auto* area : m_GridAreas) {
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

count_t GridArea::getAngle(std::vector<double>& angles, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getAngle(std::vector<double>& angles,
                           const double state[],
                           const SolverMode& sMode,
                           index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getFreq(std::vector<double>& frequencies, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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
count_t GridArea::getFreq(std::vector<double> &F, const double state[], const SolverMode &sMode,
index_t start) const
{
    count_t cnt = 0;
    for (auto &area : m_GridAreas)
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

count_t GridArea::getLinkRealPower(std::vector<double>& powers, index_t start, int busNumber) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t
    GridArea::getLinkReactivePower(std::vector<double>& powers, index_t start, int busNumber) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getBusGenerationReal(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getBusGenerationReactive(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getBusLoadReal(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getBusLoadReactive(std::vector<double>& powers, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getLinkLoss(std::vector<double>& losses, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getBusName(stringVec& names, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getLinkName(stringVec& names, index_t start) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

count_t GridArea::getLinkBus(stringVec& names, index_t start, int busNumber) const
{
    count_t cnt = 0;
    for (auto* area : m_GridAreas) {
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

double GridArea::getAdjustableCapacityUp(CoreTime time) const
{
    double adjUp = 0.0;
    for (auto* area : m_GridAreas) {
        adjUp += area->getAdjustableCapacityUp(time);
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            adjUp += bus->getAdjustableCapacityUp(time);
        }
    }
    return adjUp;
}

double GridArea::getAdjustableCapacityDown(CoreTime time) const
{
    double adjDown = 0.0;
    for (auto* area : m_GridAreas) {
        adjDown += area->getAdjustableCapacityDown(time);
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            adjDown += bus->getAdjustableCapacityDown(time);
        }
    }
    return adjDown;
}

double GridArea::getLoss() const
{
    double loss = 0.0;
    for (auto* area : m_GridAreas) {
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

double GridArea::getGenerationReal() const
{
    double genP = 0.0;
    for (auto* area : m_GridAreas) {
        genP += area->getGenerationReal();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            genP += bus->getGenerationReal();
        }
    }
    return genP;
}

double GridArea::getGenerationReactive() const
{
    double genQ = 0.0;
    for (auto* area : m_GridAreas) {
        genQ += area->getGenerationReactive();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            genQ += bus->getGenerationReactive();
        }
    }
    return genQ;
}

double GridArea::getLoadReal() const
{
    double loadP = 0.0;
    for (auto* area : m_GridAreas) {
        loadP += area->getLoadReal();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            loadP += bus->getLoadReal();
        }
    }
    return loadP;
}

double GridArea::getLoadReactive() const
{
    double loadQ = 0.0;
    for (auto* area : m_GridAreas) {
        loadQ += area->getLoadReactive();
    }
    for (auto* bus : m_Buses) {
        if (bus->isConnected()) {
            loadQ += bus->getLoadReactive();
        }
    }
    return loadQ;
}

double GridArea::getAvgAngle() const
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

double GridArea::getAvgAngle(const StateData& stateDataValue, const SolverMode& sMode) const
{
    double a = 0.0;
    double cnt = 0.0;
    for (auto* bus : m_Buses) {
        if (bus->hasInertialAngle()) {
            a += bus->getAngle(stateDataValue, sMode);
            cnt += 1.0;
        }
    }

    return (a / cnt);
}

double GridArea::getAvgFreq() const
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
void GridArea::guessState(CoreTime time, double state[], double dstateDt[], const SolverMode& sMode)
{
    auto cobj = opObjectLists->begin(sMode);
    auto cend = opObjectLists->end(sMode);
    while (cobj != cend) {
        (*cobj)->guessState(time, state, dstateDt, sMode);
        ++cobj;
    }
    // next do any internal control elements
}

void GridArea::getVariableType(double sdata[], const SolverMode& sMode)
{
    auto ra = opObjectLists->begin(sMode);
    auto rend = opObjectLists->end(sMode);
    while (ra != rend) {
        (*ra)->getVariableType(sdata, sMode);
        ++ra;
    }

    // next do any internal area states
}

void GridArea::getTols(double tols[], const SolverMode& sMode)
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
void GridArea::rootTest(const IOdata& inputs,
                        const StateData& stateDataValue,
                        double roots[],
                        const SolverMode& sMode)
{
    for (auto* ro : rootObjects) {
        ro->rootTest(inputs, stateDataValue, roots, sMode);
    }
#ifdef DEBUG_PRINT
    for (size_t kk = 0; kk < rootSize(sMode); ++kk) {
        std::println("t={} root[{}]={}", time, kk, roots[kk]);
    }
#endif
}

ChangeCode GridArea::rootCheck(const IOdata& inputs,
                               const StateData& stateDataValue,
                               const SolverMode& sMode,
                               CheckLevel level)
{
    ChangeCode ret = ChangeCode::NO_CHANGE;
    // root checks can trigger flag updates disable and just do the update once
    opFlags.set(DISABLE_FLAG_UPDATES);
    if (level >= CheckLevel::LOW_VOLTAGE_CHECK) {
        for (auto* obj : primaryObjects) {
            if (obj->isEnabled()) {
                auto iret = obj->rootCheck(inputs, stateDataValue, sMode, level);
                if (iret > ret) {
                    ret = iret;
                }
            }
        }
    } else {
        for (auto* ro : rootObjects) {
            if (ro->checkFlag(HAS_ALG_ROOTS)) {
                auto iret = ro->rootCheck(inputs, stateDataValue, sMode, level);
                if (iret > ret) {
                    ret = iret;
                }
            }
        }
    }
    opFlags.reset(DISABLE_FLAG_UPDATES);
    if (opFlags[FLAG_UPDATE_REQUIRED]) {
        updateFlags();
    }
    return ret;
}

void GridArea::rootTrigger(CoreTime time,
                           const IOdata& inputs,
                           const std::vector<int>& rootMask,
                           const SolverMode& sMode)
{
    auto RF = vecFindne(rootMask, 0);
    size_t cloc = 0;
    size_t rs = rootSize(sMode);
    size_t rootOffset = offsets.getRootOffset(sMode);

    auto currentRootObject = rootObjects.begin();
    auto obend = rootObjects.end();
    auto ors = (*currentRootObject)->rootSize(sMode);
    opFlags.set(DISABLE_FLAG_UPDATES);  // root triggers can cause a flag change and the flag update
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
    opFlags.reset(DISABLE_FLAG_UPDATES);
    if (opFlags[FLAG_UPDATE_REQUIRED]) {
        updateFlags();
        opFlags.reset(FLAG_UPDATE_REQUIRED);
    }
}

// pass the solution
void GridArea::setState(CoreTime time,
                        const double state[],
                        const double dstateDt[],
                        const SolverMode& sMode)
{
    prevTime = time;

    // links come first
    for (auto* link : m_Links) {
        if (link->isEnabled()) {
            link->setState(time, state, dstateDt, sMode);
        }
    }
    for (auto* area : m_GridAreas) {
        if (area->isEnabled()) {
            area->setState(time, state, dstateDt, sMode);
        }
    }

    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            bus->setState(time, state, dstateDt, sMode);
        }
    }
    for (auto* rel : m_Relays) {
        if (rel->isEnabled()) {
            rel->setState(time, state, dstateDt, sMode);
        }
    }
    // next do any internal area states
}

void GridArea::getVoltageStates(double vStates[], const SolverMode& sMode) const

{
    index_t Voffset;
    for (auto* area : m_GridAreas) {
        if (area->isEnabled()) {
            area->getVoltageStates(vStates, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            Voffset = bus->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
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

void GridArea::getAngleStates(double aStates[], const SolverMode& sMode) const

{
    index_t Aoffset;
    for (auto* area : m_GridAreas) {
        if (area->isEnabled()) {
            area->getAngleStates(aStates, sMode);
        }
    }
    for (auto* bus : m_Buses) {
        if (bus->isEnabled()) {
            Aoffset = bus->getOutputLoc(sMode, ANGLE_IN_LOCATION);
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

void GridArea::preEx(const IOdata& inputs, const StateData& stateDataValue, const SolverMode& sMode)
{
    opObjectLists->preEx(inputs, stateDataValue, sMode);
}

void GridArea::residual(const IOdata& inputs,
                        const StateData& stateDataValue,
                        double resid[],
                        const SolverMode& sMode)
{
    opObjectLists->residual(inputs, stateDataValue, resid, sMode);

    // next do any internal states
}

void GridArea::algebraicUpdate(const IOdata& inputs,
                               const StateData& stateDataValue,
                               double update[],
                               const SolverMode& sMode,
                               double alpha)
{
    opObjectLists->algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);

    // next do any internal states
}

void GridArea::getStateName(stringVec& stNames,
                            const SolverMode& sMode,
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

void GridArea::delayedResidual(const IOdata& inputs,
                               const StateData& stateDataValue,
                               double resid[],
                               const SolverMode& sMode)
{
    opObjectLists->delayedResidual(inputs, stateDataValue, resid, sMode);
}

void GridArea::delayedDerivative(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double deriv[],
                                 const SolverMode& sMode)
{
    opObjectLists->delayedDerivative(inputs, stateDataValue, deriv, sMode);
}

void GridArea::delayedJacobian(const IOdata& inputs,
                               const StateData& stateDataValue,
                               MatrixData<double>& matrixDataValue,
                               const IOlocs& inputLocs,
                               const SolverMode& sMode)
{
    opObjectLists->delayedJacobian(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
}

void GridArea::delayedAlgebraicUpdate(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      double update[],
                                      const SolverMode& sMode,
                                      double alpha)
{
    opObjectLists->delayedAlgebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
}

void GridArea::derivative(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double deriv[],
                          const SolverMode& sMode)
{
    opObjectLists->derivative(inputs, stateDataValue, deriv, sMode);
    // next do any internal states
}

// Jacobian
void GridArea::jacobianElements(const IOdata& inputs,
                                const StateData& stateDataValue,
                                MatrixData<double>& matrixDataValue,
                                const IOlocs& inputLocs,
                                const SolverMode& sMode)
{
    opObjectLists->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    // next do any internal control elements
}

void GridArea::updateFlags(bool /*dynOnly*/)
{
    pFlowAdjustObjects.clear();
    opFlags &= (~flagMask);  // clear the cascading flags

    for (auto* obj : primaryObjects) {
        if (obj->isEnabled()) {
            opFlags |= obj->cascadingFlags();
            if (obj->checkFlag(HAS_POWERFLOW_ADJUSTMENTS)) {
                pFlowAdjustObjects.push_back(obj);
            }
        }
    }
}

void GridArea::setOffsets(const SolverOffsets& newOffsets, const SolverMode& sMode)
{
    if (!(isStateCountLoaded(sMode))) {
        loadStateSizes(sMode);
    }
    offsets.setOffsets(newOffsets, sMode);
    SolverOffsets no(newOffsets);
    no.localIncrement(offsets.getOffsets(sMode));

    for (auto* obj : primaryObjects) {
        obj->setOffsets(no, sMode);
        no.increment(obj->getOffsets(sMode));
    }
}

void GridArea::setOffset(index_t offset, const SolverMode& sMode)
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

void GridArea::setRootOffset(index_t rootOffset, const SolverMode& sMode)
{
    offsets.setRootOffset(rootOffset, sMode);
    const auto& so = offsets.getOffsets(sMode);
    auto nR = so.local.algRoots + so.local.diffRoots;
    for (auto* ro : rootObjects) {
        ro->setRootOffset(rootOffset + nR, sMode);
        nR += ro->rootSize(sMode);
    }
}

double GridArea::getTieFlowReal() const
{
    return (getGenerationReal() - getLoadReal() - getLoss());
}

double GridArea::getMasterAngle(const StateData& stateDataValue, const SolverMode& sMode) const
{
    if (masterBus >= 0) {
        return m_Buses[masterBus]->getAngle(stateDataValue, sMode);
    }
    if (!isRoot()) {
        return static_cast<GridArea*>(getParent())->getMasterAngle(stateDataValue, sMode);
    }
    if (!m_Buses.empty()) {
        return m_Buses[0]->getAngle(stateDataValue, sMode);
    }
    return 0.0;
}

StateSizes GridArea::localStateSizes(const SolverMode& /*sMode*/) const
{
    return offsets.local().local;
}

count_t GridArea::localJacobianCount(const SolverMode& /*sMode*/) const
{
    return offsets.local().local.jacSize;
}

std::pair<count_t, count_t> GridArea::LocalRootCount(const SolverMode& /*sMode*/) const
{
    const auto& lc = offsets.local().local;
    return std::make_pair(lc.algRoots, lc.diffRoots);
}

void GridArea::loadStateSizes(const SolverMode& sMode)
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
    auto selfSizes = localStateSizes(sMode);
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
            if (sub->checkFlag(SAMPLED_ONLY)) {
                continue;
            }
            so.addStateSizes(sub->getOffsets(sMode));
        }
    }
    so.stateLoaded = true;
    opObjectLists->makeList(sMode, primaryObjects);
}

void GridArea::loadRootSizes(const SolverMode& sMode)
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
        if (obj->checkFlag(HAS_ROOTS)) {
            rootObjects.push_back(obj);
        }
        so.addRootSizes(obj->getOffsets(sMode));
    }
    so.rootsLoaded = true;
}

void GridArea::loadJacobianSizes(const SolverMode& sMode)
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
        so.jacobianCountReset();
    }
    auto selfJacCount = localJacobianCount(sMode);
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

GridArea* getMatchingGridArea(GridArea* area, GridPrimary* src, GridPrimary* sec)
{
    if (area->isRoot()) {
        return nullptr;
    }

    if (isSameObject(area->getParent(), src))  // if this is true then things are easy
    {
        return sec->getGridArea(area->locIndex);
    }

    std::vector<index_t> lkind;
    auto* par = dynamic_cast<GridPrimary*>(area->getParent());
    if (par == nullptr) {
        return nullptr;
    }
    lkind.push_back(area->locIndex);

    while (!isSameObject(par, src)) {
        lkind.push_back(par->locIndex);
        par = dynamic_cast<GridPrimary*>(par->getParent());
        if (par == nullptr) {
            return nullptr;
        }
    }
    // now work our way backwards through the secondary
    par = sec;
    for (auto kk = lkind.size() - 1; kk > 0; --kk) {
        par = static_cast<GridPrimary*>(par->getGridArea(lkind[kk]));
    }
    return par->getGridArea(lkind[0]);
}

}  // namespace griddyn
