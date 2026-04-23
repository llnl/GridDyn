/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridBusOpt.h"

#include "../optObjectFactory.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridAreaOpt.h"
#include "gridGenOpt.h"
#include "gridLinkOpt.h"
#include "gridLoadOpt.h"
#include "griddyn/Generator.h"
#include "griddyn/gridBus.h"
#include "griddyn/loads/zipLoad.h"
#include "utilities/vectData.hpp"
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
static optObjectFactory<gridBusOpt, gridBus> opbus("basic", "bus");

using units::unit;

gridBusOpt::gridBusOpt(const std::string& objName): gridOptObject(objName) {}

gridBusOpt::gridBusOpt(coreObject* obj, const std::string& objName):
    gridOptObject(objName), bus(dynamic_cast<gridBus*>(obj))
{
    if (bus != nullptr) {
        if (getName().empty()) {
            setName(bus->getName());
        }
        setUserID(bus->getUserID());
    }
}

coreObject* gridBusOpt::clone(coreObject* obj) const
{
    gridBusOpt* nobj;
    if (obj == nullptr) {
        nobj = new gridBusOpt();
    } else {
        nobj = dynamic_cast<gridBusOpt*>(obj);
        if (nobj == nullptr) {
            // if we can't cast the pointer clone at the next lower level
            gridOptObject::clone(obj);
            return obj;
        }
    }
    gridOptObject::clone(nobj);

    // now clone all the loads and generators
    // cloning the links from this component would be bad
    // clone the generators and loads

    for (size_t kk = 0; kk < genList.size(); ++kk) {
        if (kk >= nobj->genList.size()) {
            nobj->add(static_cast<Generator*>(genList[kk]->clone(nullptr)));
        } else {
            genList[kk]->clone(nobj->genList[kk]);
        }
    }
    for (size_t kk = 0; kk < loadList.size(); ++kk) {
        if (kk >= nobj->loadList.size()) {
            nobj->add(static_cast<zipLoad*>(loadList[kk]->clone(nullptr)));
        } else {
            loadList[kk]->clone(nobj->loadList[kk]);
        }
    }
    return nobj;
}

void gridBusOpt::dynObjectInitializeA(std::uint32_t flags)
{
    for (auto ld : loadList) {
        ld->dynObjectInitializeA(flags);
    }
    for (auto gen : genList) {
        gen->dynObjectInitializeA(flags);
    }
}

void gridBusOpt::loadSizes(const optimMode& oMode)
{
    optimOffsets& oo = offsets.getOffsets(oMode);
    oo.reset();
    switch (oMode.flowMode) {
        case flowModel_t::none:
        case flowModel_t::transport:
            oo.local.constraintsSize = 1;
            break;
        case flowModel_t::dc:
            oo.local.aSize = 1;
            oo.local.constraintsSize = 1;
            break;
        case flowModel_t::ac:
            oo.local.aSize = 1;
            oo.local.vSize = 1;
            oo.local.constraintsSize = 2;
            break;
    }
    oo.localLoad();
    for (auto ld : loadList) {
        ld->loadSizes(oMode);
        oo.addSizes(ld->offsets.getOffsets(oMode));
    }
    for (auto gen : genList) {
        gen->loadSizes(oMode);
        oo.addSizes(gen->offsets.getOffsets(oMode));
    }
    oo.loaded = true;
}

void gridBusOpt::setValues(const optimData& of, const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->setValues(of, oMode);
    }
    for (auto gen : genList) {
        gen->setValues(of, oMode);
    }
}
// for saving the state
void gridBusOpt::guessState(double time, double val[], const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->guessState(time, val, oMode);
    }
    for (auto gen : genList) {
        gen->guessState(time, val, oMode);
    }
}

void gridBusOpt::getVariableType(double sdata[], const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->getVariableType(sdata, oMode);
    }
    for (auto gen : genList) {
        gen->getVariableType(sdata, oMode);
    }
}

void gridBusOpt::getTols(double tols[], const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->getTols(tols, oMode);
    }
    for (auto gen : genList) {
        gen->getTols(tols, oMode);
    }
}

// void alert (coreObject *object, int code);

void gridBusOpt::valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->valueBounds(time, upperLimit, lowerLimit, oMode);
    }
    for (auto gen : genList) {
        gen->valueBounds(time, upperLimit, lowerLimit, oMode);
    }
}

void gridBusOpt::linearObj(const optimData& of, vectData<double>& linObj, const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->linearObj(of, linObj, oMode);
    }
    for (auto gen : genList) {
        gen->linearObj(of, linObj, oMode);
    }
}
void gridBusOpt::quadraticObj(const optimData& of,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->quadraticObj(of, linObj, quadObj, oMode);
    }
    for (auto gen : genList) {
        gen->quadraticObj(of, linObj, quadObj, oMode);
    }
}

double gridBusOpt::objValue(const optimData& of, const optimMode& oMode)
{
    double cost = 0;
    for (auto ld : loadList) {
        cost += ld->objValue(of, oMode);
    }
    for (auto gen : genList) {
        cost += gen->objValue(of, oMode);
    }
    return cost;
}

void gridBusOpt::gradient(const optimData& of, double deriv[], const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->gradient(of, deriv, oMode);
    }
    for (auto gen : genList) {
        gen->gradient(of, deriv, oMode);
    }
}
void gridBusOpt::jacobianElements(const optimData& of,
                                  matrixData<double>& md,
                                  const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->jacobianElements(of, md, oMode);
    }
    for (auto gen : genList) {
        gen->jacobianElements(of, md, oMode);
    }
}
void gridBusOpt::getConstraints(const optimData& of,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->getConstraints(of, cons, upperLimit, lowerLimit, oMode);
    }
    for (auto gen : genList) {
        gen->getConstraints(of, cons, upperLimit, lowerLimit, oMode);
    }
}

void gridBusOpt::constraintValue(const optimData& of, double cVals[], const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->constraintValue(of, cVals, oMode);
    }
    for (auto gen : genList) {
        gen->constraintValue(of, cVals, oMode);
    }
}

void gridBusOpt::constraintJacobianElements(const optimData& of,
                                            matrixData<double>& md,
                                            const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->constraintJacobianElements(of, md, oMode);
    }
    for (auto gen : genList) {
        gen->constraintJacobianElements(of, md, oMode);
    }
}

void gridBusOpt::getObjName(stringVec& objNames, const optimMode& oMode, const std::string& prefix)
{
    for (auto ld : loadList) {
        ld->getObjName(objNames, oMode, prefix);
    }
    for (auto gen : genList) {
        gen->getObjName(objNames, oMode, prefix);
    }
}

void gridBusOpt::disable()
{
    gridOptObject::disable();
    for (auto& link : linkList) {
        link->disable();
    }
}

void gridBusOpt::setOffsets(const optimOffsets& newOffsets, const optimMode& oMode)
{
    offsets.setOffsets(newOffsets, oMode);
    optimOffsets no(offsets.getOffsets(oMode));
    no.localLoad();
    for (auto ld : loadList) {
        ld->setOffsets(no, oMode);
        no.increment(ld->offsets.getOffsets(oMode));
    }
    for (auto gen : genList) {
        gen->setOffsets(no, oMode);
        no.increment(gen->offsets.getOffsets(oMode));
    }
}

void gridBusOpt::setOffset(index_t offset, index_t constraintOffset, const optimMode& oMode)
{
    for (auto ld : loadList) {
        ld->setOffset(offset, constraintOffset, oMode);
        constraintOffset += ld->constraintSize(oMode);
        offset += ld->objSize(oMode);
    }
    for (auto gen : genList) {
        gen->setOffset(offset, constraintOffset, oMode);
        constraintOffset += gen->constraintSize(oMode);
        offset += gen->objSize(oMode);
    }

    offsets.setConstraintOffset(constraintOffset, oMode);
    offsets.setOffset(offset, oMode);
}

// destructor
gridBusOpt::~gridBusOpt()
{
    for (auto& ld : loadList) {
        removeReference(ld, this);
    }

    for (auto& gen : genList) {
        removeReference(gen, this);
    }
}

void gridBusOpt::add(coreObject* obj)
{
    auto ld = dynamic_cast<gridLoadOpt*>(obj);
    if (ld != nullptr) {
        return add(ld);
    }

    auto gen = dynamic_cast<gridGenOpt*>(obj);
    if (gen != nullptr) {
        return add(gen);
    }

    auto lnk = dynamic_cast<gridLinkOpt*>(obj);
    if (lnk != nullptr) {
        return add(lnk);
    }

    if (dynamic_cast<gridBus*>(obj) != nullptr) {
        bus = static_cast<gridBus*>(obj);
        if (getName().empty()) {
            setName(bus->getName());
        }
        setUserID(bus->getUserID());
    } else {
        throw(unrecognizedObjectException(this));
    }
}

// add load
void gridBusOpt::add(gridLoadOpt* ld)
{
    coreObject* obj = find(ld->getName());
    if (obj == nullptr) {
        ld->locIndex = static_cast<index_t>(loadList.size());
        loadList.push_back(ld);
        ld->setParent(this);
    } else if (ld->getID() != obj->getID()) {
        throw(objectAddFailure(this));
    }
}

// add generator
void gridBusOpt::add(gridGenOpt* gen)
{
    coreObject* obj = find(gen->getName());
    if (obj == nullptr) {
        gen->locIndex = static_cast<index_t>(genList.size());
        genList.push_back(gen);
        gen->setParent(this);
    } else if (gen->getID() != obj->getID()) {
        throw(objectAddFailure(this));
    }
}

// add link
void gridBusOpt::add(gridLinkOpt* lnk)
{
    for (auto& links : linkList) {
        if (links->getID() == lnk->getID()) {
            return;
        }
    }
    linkList.push_back(lnk);
}

void gridBusOpt::remove(coreObject* obj)
{
    auto* ld = dynamic_cast<gridLoadOpt*>(obj);
    if (ld != nullptr) {
        return (remove(ld));
    }

    auto* gen = dynamic_cast<gridGenOpt*>(obj);
    if (gen != nullptr) {
        return (remove(gen));
    }

    auto* lnk = dynamic_cast<gridLinkOpt*>(obj);
    if (lnk != nullptr) {
        return (remove(lnk));
    }
}

// remove load
void gridBusOpt::remove(gridLoadOpt* ld)
{
    for (size_t kk = 0; kk < loadList.size(); ++kk) {
        if (ld == loadList[kk]) {
            loadList[kk]->setParent(nullptr);
            loadList.erase(loadList.begin() + kk);
            break;
        }
    }
}

// remove generator
void gridBusOpt::remove(gridGenOpt* gen)
{
    for (size_t kk = 0; kk < genList.size(); ++kk) {
        if (gen == genList[kk]) {
            genList[kk]->setParent(nullptr);
            genList.erase(genList.begin() + kk);
            break;
        }
    }
}

// remove link
void gridBusOpt::remove(gridLinkOpt* lnk)
{
    for (size_t kk = 0; kk < linkList.size(); ++kk) {
        if (lnk == linkList[kk]) {
            linkList.erase(linkList.begin() + kk);
            break;
        }
    }
}

void gridBusOpt::setAll(const std::string& type,
                        const std::string& param,
                        double val,
                        units::unit unitType)
{
    if ((type == "gen") || (type == "generator")) {
        for (auto& gen : genList) {
            gen->set(param, val, unitType);
        }
    } else if (type == "load") {
        for (auto& ld : loadList) {
            ld->set(param, val, unitType);
        }
    }
}

// set properties
void gridBusOpt::set(std::string_view param, std::string_view val)
{
    if (param[0] != '#') {
        gridOptObject::set(param, val);
    }
}

void gridBusOpt::set(std::string_view param, double val, unit unitType)
{
    if ((param == "voltagetolerance") || (param == "vtol")) {
    } else if ((param == "angletolerance") || (param == "atol")) {
    } else {
        gridOptObject::set(param, val, unitType);
    }
}

coreObject* gridBusOpt::find(std::string_view objName) const
{
    coreObject* obj = nullptr;
    if ((objName == getName()) || (objName == "bus")) {
        return const_cast<gridBusOpt*>(this);
    }
    for (auto ob : genList) {
        if (objName == ob->getName()) {
            obj = ob;
            break;
        }
    }
    if (obj == nullptr) {
        for (auto ob : loadList) {
            if (objName == ob->getName()) {
                obj = ob;
                break;
            }
        }
    }
    return obj;
}

coreObject* gridBusOpt::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "link") {
        return getLink(num - 1);
    }
    if (typeName == "load") {
        return getLoad(num - 1);
    }
    if ((typeName == "gen") || (typeName == "generator")) {
        return getGen(num - 1);
    }
    return nullptr;
}

coreObject* gridBusOpt::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName == "load") {
        for (auto& LD : loadList) {
            if (LD->getUserID() == searchID) {
                return LD;
            }
        }
    } else if ((typeName == "gen") || (typeName == "generator")) {
        for (auto& gen : genList) {
            if (gen->getUserID() == searchID) {
                return gen;
            }
        }
    }
    return nullptr;
}

gridOptObject* gridBusOpt::getLink(index_t x) const
{
    return (isValidIndex(x, linkList)) ? linkList[x] : nullptr;
}

gridOptObject* gridBusOpt::getLoad(index_t x) const
{
    return (isValidIndex(x, loadList)) ? loadList[x] : nullptr;
}

gridOptObject* gridBusOpt::getGen(index_t x) const
{
    return (isValidIndex(x, genList)) ? genList[x] : nullptr;
}

double gridBusOpt::get(std::string_view param, units::unit unitType) const
{
    double fval = kNullVal;
    count_t ival = kInvalidCount;
    if (param == "gencount") {
        ival = static_cast<count_t>(genList.size());
    } else if (param == "linkcount") {
        ival = static_cast<count_t>(linkList.size());
    } else if (param == "loadcount") {
        ival = static_cast<count_t>(loadList.size());
    } else {
        fval = coreObject::get(param, unitType);
    }
    return (ival != kInvalidCount) ? static_cast<double>(ival) : fval;
}

gridBusOpt* getMatchingBus(gridBusOpt* bus, gridOptObject* src, gridOptObject* sec)
{
    if (bus->isRoot()) {
        return nullptr;
    }
    if (isSameObject(bus->getParent(), src))  // if this is true then things are easy
    {
        return static_cast<gridBusOpt*>(sec->getBus(bus->locIndex));
    }
    auto* par = dynamic_cast<gridOptObject*>(bus->getParent());
    if (par == nullptr) {
        return nullptr;
    }
    std::vector<index_t> lkind;
    lkind.push_back(bus->locIndex);
    while (par->getID() != src->getID()) {
        lkind.push_back(par->locIndex);
        par = dynamic_cast<gridOptObject*>(par->getParent());
        if (par == nullptr) {
            return nullptr;
        }
    }
    // now work our way backwards through the secondary
    par = sec;
    for (auto kk = lkind.size() - 1; kk > 0; --kk) {
        par = par->getArea(lkind[kk]);
    }
    return static_cast<gridBusOpt*>(par->getBus(lkind[0]));
}

}  // namespace griddyn
