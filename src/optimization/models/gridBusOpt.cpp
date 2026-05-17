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
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static OptObjectFactory<GridBusOpt, gridBus> opbus("basic", "bus");
// NOLINTBEGIN(bugprone-branch-clone)

using units::unit;

GridBusOpt::GridBusOpt(const std::string& objName): GridOptObject(objName) {}

GridBusOpt::GridBusOpt(CoreObject* obj, const std::string& objName):
    GridOptObject(objName), bus(dynamic_cast<gridBus*>(obj))
{
    if (bus != nullptr) {
        if (getName().empty()) {
            setName(bus->getName());
        }
        setUserID(bus->getUserID());
    }
}

CoreObject* GridBusOpt::clone(CoreObject* obj) const
{
    GridBusOpt* nobj;
    if (obj == nullptr) {
        nobj = new GridBusOpt();
    } else {
        nobj = dynamic_cast<GridBusOpt*>(obj);
        if (nobj == nullptr) {
            // if we can't cast the pointer clone at the next lower level
            GridOptObject::clone(obj);
            return obj;
        }
    }
    GridOptObject::clone(nobj);

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

void GridBusOpt::dynObjectInitializeA(std::uint32_t flags)
{
    for (auto* loadObject : loadList) {
        loadObject->dynObjectInitializeA(flags);
    }
    for (auto* genObject : genList) {
        genObject->dynObjectInitializeA(flags);
    }
}

void GridBusOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    offsetData.reset();
    switch (oMode.flowMode) {
        case FlowModel::NONE:
        case FlowModel::TRANSPORT:
            offsetData.local.constraintsSize = 1;
            break;
        case FlowModel::DC:
            offsetData.local.aSize = 1;
            offsetData.local.constraintsSize = 1;
            break;
        case FlowModel::AC:
            offsetData.local.aSize = 1;
            offsetData.local.vSize = 1;
            offsetData.local.constraintsSize = 2;
            break;
    }
    offsetData.localLoad();
    for (auto* loadObject : loadList) {
        loadObject->loadSizes(oMode);
        offsetData.addSizes(loadObject->offsets.getOffsets(oMode));
    }
    for (auto* genObject : genList) {
        genObject->loadSizes(oMode);
        offsetData.addSizes(genObject->offsets.getOffsets(oMode));
    }
    offsetData.loaded = true;
}

void GridBusOpt::setValues(const OptimizationData& optimizationData, const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->setValues(optimizationData, oMode);
    }
    for (auto* genObject : genList) {
        genObject->setValues(optimizationData, oMode);
    }
}
// for saving the state
void GridBusOpt::guessState(double time, double val[], const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->guessState(time, val, oMode);
    }
    for (auto* genObject : genList) {
        genObject->guessState(time, val, oMode);
    }
}

void GridBusOpt::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->getVariableType(sdata, oMode);
    }
    for (auto* genObject : genList) {
        genObject->getVariableType(sdata, oMode);
    }
}

void GridBusOpt::getTols(double tols[], const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->getTols(tols, oMode);
    }
    for (auto* genObject : genList) {
        genObject->getTols(tols, oMode);
    }
}

// void alert (CoreObject *object, int code);

void GridBusOpt::valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->valueBounds(time, upperLimit, lowerLimit, oMode);
    }
    for (auto* genObject : genList) {
        genObject->valueBounds(time, upperLimit, lowerLimit, oMode);
    }
}

void GridBusOpt::linearObj(const OptimizationData& optimizationData,
                           vectData<double>& linObj,
                           const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->linearObj(optimizationData, linObj, oMode);
    }
    for (auto* genObject : genList) {
        genObject->linearObj(optimizationData, linObj, oMode);
    }
}
void GridBusOpt::quadraticObj(const OptimizationData& optimizationData,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->quadraticObj(optimizationData, linObj, quadObj, oMode);
    }
    for (auto* genObject : genList) {
        genObject->quadraticObj(optimizationData, linObj, quadObj, oMode);
    }
}

double GridBusOpt::objValue(const OptimizationData& optimizationData, const OptimizationMode& oMode)
{
    double cost = 0;
    for (auto* loadObject : loadList) {
        cost += loadObject->objValue(optimizationData, oMode);
    }
    for (auto* genObject : genList) {
        cost += genObject->objValue(optimizationData, oMode);
    }
    return cost;
}

void GridBusOpt::gradient(const OptimizationData& optimizationData,
                          double deriv[],
                          const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->gradient(optimizationData, deriv, oMode);
    }
    for (auto* genObject : genList) {
        genObject->gradient(optimizationData, deriv, oMode);
    }
}
void GridBusOpt::jacobianElements(const OptimizationData& optimizationData,
                                  matrixData<double>& matrixDataRef,
                                  const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->jacobianElements(optimizationData, matrixDataRef, oMode);
    }
    for (auto* genObject : genList) {
        genObject->jacobianElements(optimizationData, matrixDataRef, oMode);
    }
}
void GridBusOpt::getConstraints(const OptimizationData& optimizationData,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->getConstraints(optimizationData, cons, upperLimit, lowerLimit, oMode);
    }
    for (auto* genObject : genList) {
        genObject->getConstraints(optimizationData, cons, upperLimit, lowerLimit, oMode);
    }
}

void GridBusOpt::constraintValue(const OptimizationData& optimizationData,
                                 double cVals[],
                                 const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->constraintValue(optimizationData, cVals, oMode);
    }
    for (auto* genObject : genList) {
        genObject->constraintValue(optimizationData, cVals, oMode);
    }
}

void GridBusOpt::constraintJacobianElements(const OptimizationData& optimizationData,
                                            matrixData<double>& matrixDataRef,
                                            const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->constraintJacobianElements(optimizationData, matrixDataRef, oMode);
    }
    for (auto* genObject : genList) {
        genObject->constraintJacobianElements(optimizationData, matrixDataRef, oMode);
    }
}

void GridBusOpt::getObjectiveNames(stringVec& objectiveNames,
                                   const OptimizationMode& oMode,
                                   const std::string& prefix)
{
    for (auto* loadObject : loadList) {
        loadObject->getObjectiveNames(objectiveNames, oMode, prefix);
    }
    for (auto* genObject : genList) {
        genObject->getObjectiveNames(objectiveNames, oMode, prefix);
    }
}

void GridBusOpt::disable()
{
    GridOptObject::disable();
    for (auto& link : linkList) {
        link->disable();
    }
}

void GridBusOpt::setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode)
{
    offsets.setOffsets(newOffsets, oMode);
    OptimizationOffsets nextOffsets(offsets.getOffsets(oMode));
    nextOffsets.localLoad();
    for (auto* loadObject : loadList) {
        loadObject->setOffsets(nextOffsets, oMode);
        nextOffsets.increment(loadObject->offsets.getOffsets(oMode));
    }
    for (auto* genObject : genList) {
        genObject->setOffsets(nextOffsets, oMode);
        nextOffsets.increment(genObject->offsets.getOffsets(oMode));
    }
}

void GridBusOpt::setOffset(index_t offset, index_t constraintOffset, const OptimizationMode& oMode)
{
    for (auto* loadObject : loadList) {
        loadObject->setOffset(offset, constraintOffset, oMode);
        constraintOffset += loadObject->constraintSize(oMode);
        offset += loadObject->objSize(oMode);
    }
    for (auto* genObject : genList) {
        genObject->setOffset(offset, constraintOffset, oMode);
        constraintOffset += genObject->constraintSize(oMode);
        offset += genObject->objSize(oMode);
    }

    offsets.setConstraintOffset(constraintOffset, oMode);
    offsets.setOffset(offset, oMode);
}

// destructor
GridBusOpt::~GridBusOpt()
{
    for (auto& loadObject : loadList) {
        removeReference(loadObject, this);
    }

    for (auto& gen : genList) {
        removeReference(gen, this);
    }
}

void GridBusOpt::add(CoreObject* obj)
{
    auto* loadObject = dynamic_cast<GridLoadOpt*>(obj);
    if (loadObject != nullptr) {
        add(loadObject);
        return;
    }

    auto* genObject = dynamic_cast<GridGenOpt*>(obj);
    if (genObject != nullptr) {
        add(genObject);
        return;
    }

    auto* linkObject = dynamic_cast<GridLinkOpt*>(obj);
    if (linkObject != nullptr) {
        add(linkObject);
        return;
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
void GridBusOpt::add(GridLoadOpt* loadObject)
{
    const CoreObject* obj = find(loadObject->getName());
    if (obj == nullptr) {
        loadObject->locIndex = static_cast<index_t>(loadList.size());
        loadList.push_back(loadObject);
        loadObject->setParent(this);
    } else if (loadObject->getID() != obj->getID()) {
        throw(objectAddFailure(this));
    }
}

// add generator
void GridBusOpt::add(GridGenOpt* gen)
{
    const CoreObject* obj = find(gen->getName());
    if (obj == nullptr) {
        gen->locIndex = static_cast<index_t>(genList.size());
        genList.push_back(gen);
        gen->setParent(this);
    } else if (gen->getID() != obj->getID()) {
        throw(objectAddFailure(this));
    }
}

// add link
void GridBusOpt::add(GridLinkOpt* lnk)
{
    for (auto& links : linkList) {
        if (links->getID() == lnk->getID()) {
            return;
        }
    }
    linkList.push_back(lnk);
}

void GridBusOpt::remove(CoreObject* obj)
{
    auto* loadObject = dynamic_cast<GridLoadOpt*>(obj);
    if (loadObject != nullptr) {
        remove(loadObject);
        return;
    }

    auto* genObject = dynamic_cast<GridGenOpt*>(obj);
    if (genObject != nullptr) {
        remove(genObject);
        return;
    }

    auto* linkObject = dynamic_cast<GridLinkOpt*>(obj);
    if (linkObject != nullptr) {
        remove(linkObject);
        return;
    }
}

// remove load
void GridBusOpt::remove(GridLoadOpt* loadObject)
{
    for (size_t kk = 0; kk < loadList.size(); ++kk) {
        if (loadObject == loadList[kk]) {
            loadList[kk]->setParent(nullptr);
            loadList.erase(loadList.begin() + kk);
            break;
        }
    }
}

// remove generator
void GridBusOpt::remove(GridGenOpt* gen)
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
void GridBusOpt::remove(GridLinkOpt* lnk)
{
    for (size_t kk = 0; kk < linkList.size(); ++kk) {
        if (lnk == linkList[kk]) {
            linkList.erase(linkList.begin() + kk);
            break;
        }
    }
}

void GridBusOpt::setAll(const std::string& type,
                        const std::string& param,
                        double val,
                        units::unit unitType)
{
    if ((type == "gen") || (type == "generator")) {
        for (auto& gen : genList) {
            gen->set(param, val, unitType);
        }
    } else if (type == "load") {
        for (auto* loadObject : loadList) {
            loadObject->set(param, val, unitType);
        }
    }
}

// set properties
void GridBusOpt::set(std::string_view param, std::string_view val)
{
    if (param[0] != '#') {
        GridOptObject::set(param, val);
    }
}

void GridBusOpt::set(std::string_view param, double val, unit unitType)
{
    if ((param == "voltagetolerance") || (param == "vtol")) {
    } else if ((param == "angletolerance") || (param == "atol")) {
    } else {
        GridOptObject::set(param, val, unitType);
    }
}

CoreObject* GridBusOpt::find(std::string_view objName) const
{
    CoreObject* obj = nullptr;
    if ((objName == getName()) || (objName == "bus")) {
        return const_cast<GridBusOpt*>(this);
    }
    for (auto* genObject : genList) {
        if (objName == genObject->getName()) {
            obj = genObject;
            break;
        }
    }
    if (obj == nullptr) {
        for (auto* loadObject : loadList) {
            if (objName == loadObject->getName()) {
                obj = loadObject;
                break;
            }
        }
    }
    return obj;
}

CoreObject* GridBusOpt::getSubObject(std::string_view typeName, index_t num) const
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

CoreObject* GridBusOpt::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName == "load") {
        for (const auto* loadObject : loadList) {
            if (loadObject->getUserID() == searchID) {
                return const_cast<GridLoadOpt*>(loadObject);
            }
        }
    } else if ((typeName == "gen") || (typeName == "generator")) {
        for (const auto& genObject : genList) {
            if (genObject->getUserID() == searchID) {
                return genObject;
            }
        }
    }
    return nullptr;
}

GridOptObject* GridBusOpt::getLink(index_t index) const
{
    return (isValidIndex(index, linkList)) ? linkList[index] : nullptr;
}

GridOptObject* GridBusOpt::getLoad(index_t index) const
{
    return (isValidIndex(index, loadList)) ? loadList[index] : nullptr;
}

GridOptObject* GridBusOpt::getGen(index_t index) const
{
    return (isValidIndex(index, genList)) ? genList[index] : nullptr;
}

double GridBusOpt::get(std::string_view param, units::unit unitType) const
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
        fval = CoreObject::get(param, unitType);
    }
    return (ival != kInvalidCount) ? static_cast<double>(ival) : fval;
}

}  // namespace griddyn
// NOLINTEND(bugprone-branch-clone)

