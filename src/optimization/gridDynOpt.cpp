/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynOpt.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gridOptObjects.h"
#include "models/gridAreaOpt.h"
#include "optObjectFactory.h"
// system headers

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace griddyn {
static typeFactory<GridDynOptimization> gfo(  // NOLINT(bugprone-throwing-static-initialization)
    "simulation",
    std::to_array<std::string_view>({"optimization", "optim"}));

GridDynOptimization::GridDynOptimization(const std::string& simName):
    gridDynSimulation(simName), mOptimizationMode(DEFAULT_OPTIMIZATION)
{
    // defaults
    mAreaOpt = new GridAreaOpt(this);
}

GridDynOptimization::~GridDynOptimization()
{
    delete mAreaOpt;
}

coreObject* GridDynOptimization::clone(coreObject* obj) const
{
    auto* sim = cloneBase<GridDynOptimization, gridDynSimulation>(this, obj);
    if (sim == nullptr) {
        return obj;
    }

    return sim;
}

void GridDynOptimization::setupOptOffsets(const OptimizationMode& oMode, int setupMode)
{
    if (setupMode == 0) {  // no distinction between Voltage, angle, and others
        mAreaOpt->setOffset(1, 0, oMode);
        return;
    }
    OptimizationOffsets baseOffset;
    if (setupMode == 1) {  // use all the distinct categories
        baseOffset.setOffset(1);
        baseOffset.constraintOffset = 0;
    } else if (setupMode == 2) {  // discriminate continuous and discrete objective variables
        baseOffset.constraintOffset = 0;
        baseOffset.contOffset = 1;
        baseOffset.intOffset = 0;
    }

    // call the area setOffset function to distribute the offsets
    mAreaOpt->setOffsets(baseOffset, oMode);
}

// --------------- set properties ---------------
void GridDynOptimization::set(std::string_view param, std::string_view val)
{
    if (param == "flags") {
        auto flagTokens = gmlc::utilities::stringOps::splitline(val);
        gmlc::utilities::stringOps::trim(flagTokens);
        for (auto& flagString : flagTokens) {
            setFlag(flagString, true);
        }
    } else if ((param == "defaultoptmode") || (param == "defaultopt")) {
        auto optFactory = CoreOptObjectFactory::instance();
        if (optFactory->isValidType(val)) {
            mDefaultOptMode = val;
            optFactory->setDefaultType(val);
        }
    } else if (param == "optimization_mode") {
        /*default_solution,
    dcflow_only, powerflow_only, iterated_powerflow, contingency_powerflow,
    steppedP, steppedPQ, dynamic, dyanmic_contingency,*/
        auto temp = gmlc::utilities::convertToLowerCase(val);
        if ((temp == "dcopf") || (temp == "opf")) {
            mOptimizationMode = DCOPF;
        } else if ((temp == "acopf") || (temp == "ac")) {
            mOptimizationMode = ACOPF;
        } else if (temp == "bidstack") {
            mOptimizationMode = BIDSTACK;
        } else {
            logging::warning(this, "unknown optimization mode {}", temp);
        }
    } else {
        gridDynSimulation::set(param, val);
    }
}

void GridDynOptimization::setFlag(std::string_view flag, bool val)
{
    // int nval = static_cast<int> (val);
    /*
    constraints_disabled = 1,
    sparse_solver = 2,
    threads_enabled = 3,
    ignore_voltage_limits = 4,
    power_adjust_enabled = 5,
    dcFlow_initialization = 6,*/
    if (!flag.empty()) {
        gridDynSimulation::setFlag(flag, val);
    }
}

void GridDynOptimization::setFlags(size_t param, int val)
{
    if (param > 32) {
        throw(unrecognizedParameter("flag" + std::to_string(param)));
    }

    controlFlags.set(param, (val > 0));
}

void GridDynOptimization::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "optimtol") {
        tols.rtol = val;
    } else {
        // out = setFlags (param, val);
        try {
            gridDynSimulation::set(param, val, unitType);
        }
        catch (const unrecognizedParameter&) {
            setFlag(param, (val > 0.1));
        }
    }
}

double GridDynOptimization::get(std::string_view param, units::unit unitType) const
{
    if (param == "voltagetolerance") {
        return tols.voltageTolerance;
    }
    if (param == "angletolerance") {
        return tols.angleTolerance;
    }
    return gridDynSimulation::get(param, unitType);
}

coreObject* GridDynOptimization::find(std::string_view objName) const
{
    if (objName == "optroot") {
        return mAreaOpt;
    }
    if (objName.starts_with("opt")) {
        return mAreaOpt->find(objName.substr(3));
    }
    return gridDynSimulation::find(objName);
}

coreObject* GridDynOptimization::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName.starts_with("opt")) {
        return mAreaOpt->getSubObject(typeName.substr(3), num);
    }
    return gridDynSimulation::getSubObject(typeName, num);
}
coreObject* GridDynOptimization::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName.starts_with("opt")) {
        return mAreaOpt->findByUserID(typeName.substr(3), searchID);
    }
    return gridDynSimulation::findByUserID(typeName, searchID);
}

GridOptObject* GridDynOptimization::getOptimizationObject(coreObject* obj)
{
    if (obj != nullptr) {
        coreObject* nextObject = mAreaOpt->find(obj->getName());
        if (nextObject != nullptr) {
            return static_cast<GridOptObject*>(nextObject);
        }
        return nullptr;
    }
    return mAreaOpt;
}

// NOLINTNEXTLINE(misc-no-recursion)
GridOptObject* GridDynOptimization::makeOptimizationObjectPath(coreObject* obj)
{
    GridOptObject* optObject = getOptimizationObject(obj);
    if (optObject != nullptr) {
        return optObject;
    }
    if (!(obj->isRoot())) {
        auto* parentOptObject = makeOptimizationObjectPath(obj->getParent());
        optObject = CoreOptObjectFactory::instance()->createObject(obj);
        parentOptObject->add(optObject);
        return optObject;
    }
    return nullptr;
}

OptimizerInterface* GridDynOptimization::updateOptimizer(const OptimizationMode& oMode)
{
    mOptimizerData[oMode.offsetIndex] = makeOptimizer(this, oMode);
    OptimizerInterface* optimizer = mOptimizerData[oMode.offsetIndex].get();

    return optimizer;
}

}  // namespace griddyn
