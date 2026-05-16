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
#include <vector>

namespace griddyn {
using units::unit;

static OptObjectFactory<GridAreaOpt, Area> opa("basic", "area");

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
    auto nobj = cloneBase<GridAreaOpt, GridOptObject>(this, obj);
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
    Link* Lnk;
    auto coof = CoreOptObjectFactory::instance();
    bool found;
    std::vector<GridOptObject*> newObj;
    GridOptObject* oo;

    index_t kk = 0;

    // make sure all areas have an opt object
    auto areaObj = area->getArea(kk);

    while (areaObj != nullptr) {
        found = false;
        for (auto oa : areaList) {
            if (areaObj->getID() == oa->getID()) {
                found = true;
                break;
            }
        }
        if (!found) {
            oo = coof->createObject(areaObj);
            newObj.push_back(oo);
        }
        ++kk;
        areaObj = area->getArea(kk);
    }
    for (auto no : newObj) {
        add(no);
    }
    newObj.clear();
    // make sure all buses have an opt object
    auto bus = area->getBus(kk);

    while (bus != nullptr) {
        found = false;
        for (auto oa : busList) {
            if (isSameObject(bus, oa)) {
                found = true;
                break;
            }
        }
        if (!found) {
            oo = coof->createObject(bus);
            newObj.push_back(oo);
        }
        ++kk;
        bus = area->getBus(kk);
    }
    for (auto no : newObj) {
        add(no);
    }
    newObj.clear();
    // make sure all links have an opt object
    Lnk = area->getLink(kk);

    while (Lnk != nullptr) {
        found = false;
        for (auto oa : linkList) {
            if (isSameObject(Lnk, oa)) {
                found = true;
                break;
            }
        }
        if (!found) {
            oo = coof->createObject(Lnk);
            newObj.push_back(oo);
        }
        ++kk;
        Lnk = area->getLink(kk);
    }
    for (auto no : newObj) {
        add(no);
    }
    newObj.clear();

    for (auto obj : objectList) {
        obj->dynInitializeA(flags);
    }
}

void GridAreaOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    oo.reset();
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

    for (auto obj : objectList) {
        obj->loadSizes(oMode);
        oo.addSizes(obj->offsets.getOffsets(oMode));
    }
}

void GridAreaOpt::setValues(const OptimizationData& of, const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->setValues(of, oMode);
    }
}
// for saving the state
void GridAreaOpt::guessState(double time, double val[], const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->guessState(time, val, oMode);
    }
}

void GridAreaOpt::getTols(double tols[], const OptimizationMode& oMode)

{
    for (auto obj : objectList) {
        obj->getTols(tols, oMode);
    }
}

void GridAreaOpt::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->getVariableType(sdata, oMode);
    }
}

void GridAreaOpt::valueBounds(double time,
                              double upperLimit[],
                              double lowerLimit[],
                              const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->valueBounds(time, upperLimit, lowerLimit, oMode);
    }
}

void GridAreaOpt::linearObj(const OptimizationData& of,
                            vectData<double>& linObj,
                            const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->linearObj(of, linObj, oMode);
    }
}
void GridAreaOpt::quadraticObj(const OptimizationData& of,
                               vectData<double>& linObj,
                               vectData<double>& quadObj,
                               const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->quadraticObj(of, linObj, quadObj, oMode);
    }
}

double GridAreaOpt::objValue(const OptimizationData& of, const OptimizationMode& oMode)
{
    double cost = 0;
    for (auto obj : objectList) {
        cost += obj->objValue(of, oMode);
    }

    return cost;
}

void GridAreaOpt::gradient(const OptimizationData& of,
                           double deriv[],
                           const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->gradient(of, deriv, oMode);
    }
}
void GridAreaOpt::jacobianElements(const OptimizationData& of,
                                   matrixData<double>& md,
                                   const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->jacobianElements(of, md, oMode);
    }
}
void GridAreaOpt::getConstraints(const OptimizationData& of,
                                 matrixData<double>& cons,
                                 double upperLimit[],
                                 double lowerLimit[],
                                 const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->getConstraints(of, cons, upperLimit, lowerLimit, oMode);
    }
}

void GridAreaOpt::constraintValue(const OptimizationData& of,
                                  double cVals[],
                                  const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->constraintValue(of, cVals, oMode);
    }
}

void GridAreaOpt::constraintJacobianElements(const OptimizationData& of,
                                             matrixData<double>& md,
                                             const OptimizationMode& oMode)
{
    for (auto obj : objectList) {
        obj->constraintJacobianElements(of, md, oMode);
    }
}
void GridAreaOpt::getObjName(stringVec& objNames,
                             const OptimizationMode& oMode,
                             const std::string& prefix)
{
    for (auto obj : objectList) {
        obj->getObjName(objNames, oMode, prefix);
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
    OptimizationOffsets no(offsets.getOffsets(oMode));
    no.localLoad();

    for (auto sa : areaList) {
        sa->setOffsets(no, oMode);
        no.increment(sa->offsets.getOffsets(oMode));
    }
    for (auto bus : busList) {
        bus->setOffsets(no, oMode);
        no.increment(bus->offsets.getOffsets(oMode));
    }
    for (auto link : linkList) {
        link->setOffsets(no, oMode);
        no.increment(link->offsets.getOffsets(oMode));
    }
    for (auto relay : relayList) {
        relay->setOffsets(no, oMode);
        no.increment(relay->offsets.getOffsets(oMode));
    }
}

void GridAreaOpt::setOffset(index_t offset, index_t constraintOffset, const OptimizationMode& oMode)
{
    for (auto sa : areaList) {
        sa->setOffset(offset, constraintOffset, oMode);
        constraintOffset += sa->constraintSize(oMode);
        offset += sa->objSize(oMode);
    }
    for (auto bus : busList) {
        bus->setOffset(offset, constraintOffset, oMode);
        constraintOffset += bus->constraintSize(oMode);
        offset += bus->objSize(oMode);
    }
    for (auto link : linkList) {
        link->setOffset(offset, constraintOffset, oMode);
        constraintOffset += link->constraintSize(oMode);
        offset += link->objSize(oMode);
    }
    for (auto relay : relayList) {
        relay->setOffset(offset, constraintOffset, oMode);
        constraintOffset += relay->constraintSize(oMode);
        offset += relay->objSize(oMode);
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
    auto* sa = dynamic_cast<GridAreaOpt*>(obj);
    if (sa != nullptr) {
        return add(sa);
    }

    auto* bus = dynamic_cast<GridBusOpt*>(obj);
    if (bus != nullptr) {
        return add(bus);
    }

    auto* lnk = dynamic_cast<GridLinkOpt*>(obj);
    if (lnk != nullptr) {
        return add(lnk);
    }
    auto* relay = dynamic_cast<GridRelayOpt*>(obj);
    if (relay != nullptr) {
        return add(relay);
    }
    throw(unrecognizedObjectException(this));
}

void GridAreaOpt::remove(coreObject* obj)
{
    auto* sa = dynamic_cast<GridAreaOpt*>(obj);
    if (sa != nullptr) {
        return remove(sa);
    }

    auto* bus = dynamic_cast<GridBusOpt*>(obj);
    if (bus != nullptr) {
        return remove(bus);
    }

    auto* lnk = dynamic_cast<GridLinkOpt*>(obj);
    if (lnk != nullptr) {
        return remove(lnk);
    }
    auto* relay = dynamic_cast<GridRelayOpt*>(obj);
    if (relay != nullptr) {
        return remove(relay);
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
        for (auto kk = bus->locIndex; kk < static_cast<index_t>(busList.size()); ++kk) {
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

        for (auto kk = lnk->locIndex; kk < static_cast<index_t>(linkList.size()); ++kk) {
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
        for (auto kk = areaObj->locIndex; kk < static_cast<index_t>(areaList.size()); ++kk) {
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
        for (auto kk = relay->locIndex; kk < static_cast<index_t>(relayList.size()); ++kk) {
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
    for (auto ob : busList) {
        if (objName == ob->getName()) {
            obj = ob;
            break;
        }
    }
    if (obj == nullptr) {
        for (auto ob : areaList) {
            if (objName == ob->getName()) {
                obj = ob;
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
    coreObject* A1;
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
        for (auto& bus : busList) {
            A1 = bus->findByUserID(typeName, searchID);
            if (A1) {
                return A1;
            }
        }
    }
    // if we haven't found it yet search the subareas
    for (auto* subarea : areaList) {
        A1 = subarea->findByUserID(typeName, searchID);
        if (A1) {
            return A1;
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
