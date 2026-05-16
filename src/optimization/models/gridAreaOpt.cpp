/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridAreaOpt.h"

#include "../optObjectFactory.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridBusOpt.h"
#include "gridLinkOpt.h"
#include "gridRelayOpt.h"
#include "griddyn/Area.h"
#include "griddyn/Link.h"
#include "griddyn/gridBus.h"
#include "utilities/vectData.hpp"
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using units::unit;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static OptObjectFactory<GridAreaOpt, Area> opa("basic", "area");
// NOLINTBEGIN(misc-no-recursion,bugprone-branch-clone)

GridAreaOpt::GridAreaOpt(const std::string& objName): GridOptObject(objName) {}

GridAreaOpt::GridAreaOpt(coreObject* obj, const std::string& objName):
    GridOptObject(objName), area(dynamic_cast<Area*>(obj))
{
    if (area != nullptr) {
        if (getName().empty()) {
            setName(area->getName());
        }
        setUserID(area->getUserID());
    }
}

// destructor
GridAreaOpt::~GridAreaOpt()
{
    for (auto& obj : objectList) {
        removeReference(obj, this);
    }
}

coreObject* GridAreaOpt::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<GridAreaOpt, GridOptObject>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    // now clone all the loads and generators
    // cloning the links from this component would be bad
    // clone the generators and loads

    for (size_t kk = 0; kk < busList.size(); ++kk) {
        if (kk >= nobj->busList.size()) {
            nobj->add(static_cast<GridBusOpt*>(busList[kk]->clone(nullptr)));
        } else {
            busList[kk]->clone(nobj->busList[kk]);
        }
    }
    for (size_t kk = 0; kk < areaList.size(); ++kk) {
        if (kk >= nobj->areaList.size()) {
            nobj->add(static_cast<GridAreaOpt*>(areaList[kk]->clone(nullptr)));
        } else {
            areaList[kk]->clone(nobj->areaList[kk]);
        }
    }
    for (size_t kk = 0; kk < linkList.size(); ++kk) {
        if (kk >= nobj->linkList.size()) {
            nobj->add(static_cast<GridLinkOpt*>(linkList[kk]->clone(nullptr)));
        } else {
            linkList[kk]->clone(nobj->linkList[kk]);
        }
    }
    for (size_t kk = 0; kk < relayList.size(); ++kk) {
        if (kk >= nobj->relayList.size()) {
            nobj->add(static_cast<GridRelayOpt*>(relayList[kk]->clone(nullptr)));
        } else {
            relayList[kk]->clone(nobj->relayList[kk]);
        }
    }
    return nobj;
}

void GridAreaOpt::dynObjectInitializeA(std::uint32_t flags)
{
    // first do a check to make sure all gridDyn areas are represented by gridDynOpt Area
    Link* linkObject = nullptr;
    auto coreOptFactory = CoreOptObjectFactory::instance();
    bool found;
    std::vector<GridOptObject*> newObj;
    GridOptObject* optObject;

    index_t areaIndex = 0;

    // make sure all areas have an opt object
    auto* areaObj = area->getArea(areaIndex);

    while (areaObj != nullptr) {
        found = false;
        for (auto* existingArea : areaList) {
            if (areaObj->getID() == existingArea->getID()) {
                found = true;
                break;
            }
        }
        if (!found) {
            optObject = coreOptFactory->createObject(areaObj);
            newObj.push_back(optObject);
        }
        ++areaIndex;
        areaObj = area->getArea(areaIndex);
    }
    for (auto* newObject : newObj) {
        add(newObject);
    }
    newObj.clear();
    // make sure all buses have an opt object
    auto* busObject = area->getBus(areaIndex);

    while (busObject != nullptr) {
        found = false;
        for (auto* existingBus : busList) {
            if (isSameObject(busObject, existingBus)) {
                found = true;
                break;
            }
        }
        if (!found) {
            optObject = coreOptFactory->createObject(busObject);
            newObj.push_back(optObject);
        }
        ++areaIndex;
        busObject = area->getBus(areaIndex);
    }
    for (auto* newObject : newObj) {
        add(newObject);
    }
    newObj.clear();
    // make sure all links have an opt object
    linkObject = area->getLink(areaIndex);

    while (linkObject != nullptr) {
        found = false;
        for (auto* existingLink : linkList) {
            if (isSameObject(linkObject, existingLink)) {
                found = true;
                break;
            }
        }
        if (!found) {
            optObject = coreOptFactory->createObject(linkObject);
            newObj.push_back(optObject);
        }
        ++areaIndex;
        linkObject = area->getLink(areaIndex);
    }
    for (auto* newObject : newObj) {
        add(newObject);
    }
    newObj.clear();

    for (auto* childObject : objectList) {
        childObject->dynInitializeA(flags);
    }
}

void GridAreaOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    offsetData.reset();
    switch (oMode.flowMode) {
        case FlowModel::NONE:
        default:
            break;
        case FlowModel::TRANSPORT:
            break;
        case FlowModel::DC:
        case FlowModel::AC:
            break;
    }

    for (auto* childObject : objectList) {
        childObject->loadSizes(oMode);
        offsetData.addSizes(childObject->offsets.getOffsets(oMode));
    }
}

void GridAreaOpt::setValues(const OptimizationData& optimizationData, const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->setValues(optimizationData, oMode);
    }
}
// for saving the state
void GridAreaOpt::guessState(double time, double val[], const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->guessState(time, val, oMode);
    }
}

void GridAreaOpt::getTols(double tols[], const OptimizationMode& oMode)

{
    for (auto* childObject : objectList) {
        childObject->getTols(tols, oMode);
    }
}

void GridAreaOpt::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->getVariableType(sdata, oMode);
    }
}

void GridAreaOpt::valueBounds(double time,
                              double upperLimit[],
                              double lowerLimit[],
                              const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->valueBounds(time, upperLimit, lowerLimit, oMode);
    }
}

void GridAreaOpt::linearObj(const OptimizationData& optimizationData,
                            vectData<double>& linObj,
                            const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->linearObj(optimizationData, linObj, oMode);
    }
}
void GridAreaOpt::quadraticObj(const OptimizationData& optimizationData,
                               vectData<double>& linObj,
                               vectData<double>& quadObj,
                               const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->quadraticObj(optimizationData, linObj, quadObj, oMode);
    }
}

double GridAreaOpt::objValue(const OptimizationData& optimizationData,
                             const OptimizationMode& oMode)
{
    double cost = 0;
    for (auto* childObject : objectList) {
        cost += childObject->objValue(optimizationData, oMode);
    }

    return cost;
}

void GridAreaOpt::gradient(const OptimizationData& optimizationData,
                           double deriv[],
                           const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->gradient(optimizationData, deriv, oMode);
    }
}
void GridAreaOpt::jacobianElements(const OptimizationData& optimizationData,
                                   matrixData<double>& matrixDataRef,
                                   const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->jacobianElements(optimizationData, matrixDataRef, oMode);
    }
}
void GridAreaOpt::getConstraints(const OptimizationData& optimizationData,
                                 matrixData<double>& cons,
                                 double upperLimit[],
                                 double lowerLimit[],
                                 const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->getConstraints(optimizationData, cons, upperLimit, lowerLimit, oMode);
    }
}

void GridAreaOpt::constraintValue(const OptimizationData& optimizationData,
                                  double cVals[],
                                  const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->constraintValue(optimizationData, cVals, oMode);
    }
}

void GridAreaOpt::constraintJacobianElements(const OptimizationData& optimizationData,
                                             matrixData<double>& matrixDataRef,
                                             const OptimizationMode& oMode)
{
    for (auto* childObject : objectList) {
        childObject->constraintJacobianElements(optimizationData, matrixDataRef, oMode);
    }
}
void GridAreaOpt::getObjName(stringVec& objNames,
                             const OptimizationMode& oMode,
                             const std::string& prefix)
{
    for (auto* childObject : objectList) {
        childObject->getObjName(objNames, oMode, prefix);
    }
}

void GridAreaOpt::disable()
{
    GridOptObject::disable();
    for (auto& link : linkList) {
        link->disable();
    }
}

void GridAreaOpt::setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode)
{
    offsets.setOffsets(newOffsets, oMode);
    OptimizationOffsets nextOffsets(offsets.getOffsets(oMode));
    nextOffsets.localLoad();

    for (auto* subareaObject : areaList) {
        subareaObject->setOffsets(nextOffsets, oMode);
        nextOffsets.increment(subareaObject->offsets.getOffsets(oMode));
    }
    for (auto* busObject : busList) {
        busObject->setOffsets(nextOffsets, oMode);
        nextOffsets.increment(busObject->offsets.getOffsets(oMode));
    }
    for (auto* linkObject : linkList) {
        linkObject->setOffsets(nextOffsets, oMode);
        nextOffsets.increment(linkObject->offsets.getOffsets(oMode));
    }
    for (auto* relayObject : relayList) {
        relayObject->setOffsets(nextOffsets, oMode);
        nextOffsets.increment(relayObject->offsets.getOffsets(oMode));
    }
}

void GridAreaOpt::setOffset(index_t offset, index_t constraintOffset, const OptimizationMode& oMode)
{
    for (auto* subareaObject : areaList) {
        subareaObject->setOffset(offset, constraintOffset, oMode);
        constraintOffset += subareaObject->constraintSize(oMode);
        offset += subareaObject->objSize(oMode);
    }
    for (auto* busObject : busList) {
        busObject->setOffset(offset, constraintOffset, oMode);
        constraintOffset += busObject->constraintSize(oMode);
        offset += busObject->objSize(oMode);
    }
    for (auto* linkObject : linkList) {
        linkObject->setOffset(offset, constraintOffset, oMode);
        constraintOffset += linkObject->constraintSize(oMode);
        offset += linkObject->objSize(oMode);
    }
    for (auto* relayObject : relayList) {
        relayObject->setOffset(offset, constraintOffset, oMode);
        constraintOffset += relayObject->constraintSize(oMode);
        offset += relayObject->objSize(oMode);
    }
    offsets.setConstraintOffset(constraintOffset, oMode);
    offsets.setOffset(offset, oMode);
}

void GridAreaOpt::add(coreObject* obj)
{
    if (dynamic_cast<Area*>(obj) != nullptr) {
        area = static_cast<Area*>(obj);
        if (getName().empty()) {
            setName(area->getName());
        }
        setUserID(area->getUserID());
        return;
    }
    auto* subareaObject = dynamic_cast<GridAreaOpt*>(obj);
    if (subareaObject != nullptr) {
        add(subareaObject);
        return;
    }

    auto* bus = dynamic_cast<GridBusOpt*>(obj);
    if (bus != nullptr) {
        add(bus);
        return;
    }

    auto* lnk = dynamic_cast<GridLinkOpt*>(obj);
    if (lnk != nullptr) {
        add(lnk);
        return;
    }
    auto* relay = dynamic_cast<GridRelayOpt*>(obj);
    if (relay != nullptr) {
        add(relay);
        return;
    }
    throw(unrecognizedObjectException(this));
}

void GridAreaOpt::remove(coreObject* obj)
{
    auto* subareaObject = dynamic_cast<GridAreaOpt*>(obj);
    if (subareaObject != nullptr) {
        remove(subareaObject);
        return;
    }

    auto* bus = dynamic_cast<GridBusOpt*>(obj);
    if (bus != nullptr) {
        remove(bus);
        return;
    }

    auto* lnk = dynamic_cast<GridLinkOpt*>(obj);
    if (lnk != nullptr) {
        remove(lnk);
        return;
    }
    auto* relay = dynamic_cast<GridRelayOpt*>(obj);
    if (relay != nullptr) {
        remove(relay);
        return;
    }
    throw(unrecognizedObjectException(this));
}

// TODO(phlpt): Make this work like Area.
void GridAreaOpt::add(GridBusOpt* bus)
{
    if (!isMember(bus)) {
        busList.push_back(bus);
        bus->setParent(this);
        bus->locIndex = static_cast<index_t>(busList.size()) - 1;
        optObList.insert(bus);
        objectList.push_back(bus);
    }
}

void GridAreaOpt::add(GridAreaOpt* areaObj)
{
    if (!isMember(areaObj)) {
        areaList.push_back(areaObj);
        areaObj->setParent(this);
        areaObj->locIndex = static_cast<index_t>(areaList.size()) - 1;
        optObList.insert(areaObj);
        objectList.push_back(areaObj);
    }
}

// add link
void GridAreaOpt::add(GridLinkOpt* lnk)
{
    if (!isMember(lnk)) {
        linkList.push_back(lnk);
        lnk->setParent(this);
        lnk->locIndex = static_cast<index_t>(linkList.size()) - 1;
        optObList.insert(lnk);
        objectList.push_back(lnk);
    }
}

// add link
void GridAreaOpt::add(GridRelayOpt* relay)
{
    if (!isMember(relay)) {
        relayList.push_back(relay);
        relay->setParent(this);
        relay->locIndex = static_cast<index_t>(relayList.size()) - 1;
        optObList.insert(relay);
        objectList.push_back(relay);
    }
}

// --------------- remove components ---------------
// remove bus
void GridAreaOpt::remove(GridBusOpt* bus)
{
    if (busList[bus->locIndex]->getID() == bus->getID()) {
        busList[bus->locIndex]->setParent(nullptr);
        busList.erase(busList.begin() + bus->locIndex);
        for (auto kk = bus->locIndex; std::cmp_less(kk, busList.size()); ++kk) {
            busList[kk]->locIndex = kk;
        }
        auto oLb = objectList.begin();
        auto oLe = objectList.end();
        auto bid = bus->getID();
        while (oLb != oLe) {
            if ((*oLb)->getID() == bid) {
                objectList.erase(oLb);
            }
            break;
        }
    }
}

// remove link
void GridAreaOpt::remove(GridLinkOpt* lnk)
{
    if (linkList[lnk->locIndex]->getID() == lnk->getID()) {
        linkList[lnk->locIndex]->setParent(nullptr);
        linkList.erase(linkList.begin() + lnk->locIndex);

        for (auto kk = lnk->locIndex; std::cmp_less(kk, linkList.size()); ++kk) {
            linkList[kk]->locIndex = kk;
        }
        auto oLb = objectList.begin();
        auto oLe = objectList.end();
        auto lid = lnk->getID();
        while (oLb != oLe) {
            if ((*oLb)->getID() == lid) {
                objectList.erase(oLb);
            }
            break;
        }
    }
}

// remove area
void GridAreaOpt::remove(GridAreaOpt* areaObj)
{
    if (areaList[areaObj->locIndex]->getID() == areaObj->getID()) {
        areaList[areaObj->locIndex]->setParent(nullptr);
        areaList.erase(areaList.begin() + areaObj->locIndex);
        for (auto kk = areaObj->locIndex; std::cmp_less(kk, areaList.size()); ++kk) {
            areaList[kk]->locIndex = kk;
        }
        auto oLb = objectList.begin();
        auto oLe = objectList.end();
        auto aid = areaObj->getID();
        while (oLb != oLe) {
            if ((*oLb)->getID() == aid) {
                objectList.erase(oLb);
            }
            break;
        }
    }
}

// remove area
void GridAreaOpt::remove(GridRelayOpt* relay)
{
    if (relayList[relay->locIndex]->getID() == relay->getID()) {
        relayList[relay->locIndex]->setParent(nullptr);
        relayList.erase(relayList.begin() + relay->locIndex);
        for (auto kk = relay->locIndex; std::cmp_less(kk, relayList.size()); ++kk) {
            relayList[kk]->locIndex = kk;
        }
        auto oLb = objectList.begin();
        auto oLe = objectList.end();
        auto rid = relay->getID();
        while (oLb != oLe) {
            if ((*oLb)->getID() == rid) {
                objectList.erase(oLb);
            }
            break;
        }
    }
}

void GridAreaOpt::setAll(const std::string& type,
                         const std::string& param,
                         double val,
                         units::unit unitType)
{
    if (type == "area") {
        set(param, val, unitType);
        for (auto& subarea : areaList) {
            subarea->setAll(type, param, val, unitType);
        }
    } else if (type == "bus") {
        for (auto& bus : busList) {
            bus->set(param, val, unitType);
        }
    } else if (type == "link") {
        for (auto& lnk : linkList) {
            lnk->set(param, val, unitType);
        }
    } else if (type == "relay") {
        for (auto& rel : relayList) {
            rel->set(param, val, unitType);
        }
    } else if ((type == "gen") || (type == "load") || (type == "generator")) {
        for (auto& bus : busList) {
            bus->setAll(type, param, val, unitType);
        }
        for (auto& subarea : areaList) {
            subarea->setAll(type, param, val, unitType);
        }
    }
}

// set properties
void GridAreaOpt::set(std::string_view param, std::string_view val)
{
    if (param[0] == '#') {
    } else {
        GridOptObject::set(param, val);
    }
}

void GridAreaOpt::set(std::string_view param, double val, unit unitType)
{
    if ((param == "voltagetolerance") || (param == "vtol")) {
    } else if ((param == "angletolerance") || (param == "atol")) {
    } else {
        GridOptObject::set(param, val, unitType);
    }
}

coreObject* GridAreaOpt::find(std::string_view objName) const
{
    coreObject* obj = nullptr;
    if (objName == getName()) {
        return const_cast<GridAreaOpt*>(this);
    }
    for (auto* busObject : busList) {
        if (objName == busObject->getName()) {
            obj = busObject;
            break;
        }
    }
    if (obj == nullptr) {
        for (auto* areaObject : areaList) {
            if (objName == areaObject->getName()) {
                obj = areaObject;
                break;
            }
        }
    }
    return obj;
}

coreObject* GridAreaOpt::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "link") {
        return getLink(num - 1);
    }
    if (typeName == "bus") {
        return getBus(num - 1);
    }
    if (typeName == "area") {
        return getArea(num - 1);
    }
    if (typeName == "relay") {
        return getRelay(num - 1);
    }
    return nullptr;
}

coreObject* GridAreaOpt::findByUserID(std::string_view typeName, index_t searchID) const
{
    coreObject* foundObject = nullptr;
    if (typeName == "area") {
        for (auto* subarea : areaList) {
            if (subarea->getUserID() == searchID) {
                return subarea;
            }
        }
    }
    if (typeName == "bus") {
        for (auto* bus : busList) {
            if (bus->getUserID() == searchID) {
                return bus;
            }
        }
    } else if (typeName == "link") {
        for (auto* lnk : linkList) {
            if (lnk->getUserID() == searchID) {
                return lnk;
            }
        }
    } else if (typeName == "relay") {
        for (auto* rel : relayList) {
            if (rel->getUserID() == searchID) {
                return rel;
            }
        }
    } else if (typeName == "gen" || typeName == "load" || typeName == "generator") {
        for (const auto& busObject : busList) {
            foundObject = busObject->findByUserID(typeName, searchID);
            if (foundObject != nullptr) {
                return foundObject;
            }
        }
    }
    // if we haven't found it yet search the subareas
    for (auto* subarea : areaList) {
        foundObject = subarea->findByUserID(typeName, searchID);
        if (foundObject != nullptr) {
            return foundObject;
        }
    }
    return nullptr;
}

// check bus members
bool GridAreaOpt::isMember(coreObject* obj) const
{
    return optObList.isMember(obj);
}

GridOptObject* GridAreaOpt::getBus(index_t index) const
{
    return (isValidIndex(index, busList)) ? busList[index] : nullptr;
}

GridOptObject* GridAreaOpt::getLink(index_t index) const
{
    return (isValidIndex(index, linkList)) ? linkList[index] : nullptr;
}

GridOptObject* GridAreaOpt::getArea(index_t index) const
{
    return (isValidIndex(index, areaList)) ? areaList[index] : nullptr;
}

GridOptObject* GridAreaOpt::getRelay(index_t index) const
{
    return (isValidIndex(index, relayList)) ? relayList[index] : nullptr;
}

double GridAreaOpt::get(std::string_view param, units::unit unitType) const
{
    double fval = kNullVal;
    size_t ival = kNullLocation;
    if (param == "buscount") {
        ival = busList.size();
    } else if (param == "linkcount") {
        ival = linkList.size();
    } else if (param == "areacount") {
        ival = areaList.size();
    } else if (param == "relaycount") {
        ival = relayList.size();
    } else {
        fval = coreObject::get(param, unitType);
    }
    return (ival != kNullLocation) ? static_cast<double>(ival) : fval;
}

GridAreaOpt* getMatchingArea(GridAreaOpt* area, GridOptObject* src, GridOptObject* sec)
{
    if (area->isRoot()) {
        return nullptr;
    }

    if (isSameObject(area->getParent(), src))  // if this is true then things are easy
    {
        return static_cast<GridAreaOpt*>(sec->getArea(area->locIndex));
    }

    std::vector<int> lkind;
    auto* par = dynamic_cast<GridOptObject*>(area->getParent());
    if (par == nullptr) {
        return nullptr;
    }
    lkind.push_back(area->locIndex);
    while (par->getID() != src->getID()) {
        lkind.push_back(par->locIndex);
        par = dynamic_cast<GridOptObject*>(par->getParent());
        if (par == nullptr) {
            return nullptr;
        }
    }
    // now work our way backwards through the secondary
    par = sec;
    for (auto kk = lkind.size() - 1; kk > 0; --kk) {
        par = static_cast<GridOptObject*>(par->getArea(lkind[kk]));
    }
    return static_cast<GridAreaOpt*>(par->getArea(lkind[0]));
}

}  // namespace griddyn
// NOLINTEND(misc-no-recursion,bugprone-branch-clone)
