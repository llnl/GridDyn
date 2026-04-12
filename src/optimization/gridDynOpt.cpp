/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
static typeFactory<gridDynOptimization> gfo("simulation", stringVec{"optimization", "optim"});

gridDynOptimization::gridDynOptimization(const std::string& simName): gridDynSimulation(simName)
{
    // defaults
    areaOpt = new gridAreaOpt(this);
}

gridDynOptimization::~gridDynOptimization()
{
    delete areaOpt;
}

coreObject* gridDynOptimization::clone(coreObject* obj) const
{
    auto* sim = cloneBase<gridDynOptimization, gridDynSimulation>(this, obj);
    if (sim == nullptr) {
        return obj;
    }

    return sim;
}

void gridDynOptimization::setupOptOffsets(const optimMode& oMode, int setupMode)
{
    if (setupMode == 0) {  // no distinction between Voltage, angle, and others
        areaOpt->setOffset(1, 0, oMode);
        return;
    }
    optimOffsets baseOffset;
    if (setupMode == 1) {  // use all the distinct categories
        baseOffset.setOffset(1);
        baseOffset.constraintOffset = 0;
    } else if (setupMode == 2) {  // discriminate continuous and discrete objective variables
        baseOffset.constraintOffset = 0;
        baseOffset.contOffset = 1;
        baseOffset.intOffset = 0;
    }

    // call the area setOffset function to distribute the offsets
    areaOpt->setOffsets(baseOffset, oMode);
}

// --------------- set properties ---------------
void gridDynOptimization::set(const std::string& param, const std::string& val)
{
    if (param == "flags") {
        auto v = gmlc::utilities::stringOps::splitline(val);
        gmlc::utilities::stringOps::trim(v);
        for (auto& flagstr : v) {
            setFlag(flagstr, true);
        }
    } else if ((param == "defaultoptmode") || (param == "defaultopt")) {
        auto ocf = coreOptObjectFactory::instance();
        if (ocf->isValidType(val)) {
            defaultOptMode = val;
            ocf->setDefaultType(val);
        }
    } else if (param == "optimization_mode") {
        /*default_solution,
    dcflow_only, powerflow_only, iterated_powerflow, contingency_powerflow,
    steppedP, steppedPQ, dynamic, dyanmic_contingency,*/
        auto temp = gmlc::utilities::convertToLowerCase(val);
        if ((temp == "dcopf") || (temp == "opf")) {
            optimization_mode = DCOPF;
        } else if ((temp == "acopf") || (temp == "ac")) {
            optimization_mode = ACOPF;
        } else if (temp == "bidstack") {
            optimization_mode = bidstack;
        } else {
            LOG_WARNING("unknown optimization mode " + temp);
        }
    } else {
        gridDynSimulation::set(param, val);
    }
}

void gridDynOptimization::setFlag(const std::string& flag, bool val)
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

void gridDynOptimization::setFlags(size_t param, int val)
{
    if (param > 32) {
        throw(unrecognizedParameter("flag" + std::to_string(param)));
    }

    controlFlags.set(param, (val > 0));
}

void gridDynOptimization::set(const std::string& param, double val, units::unit unitType)
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

double gridDynOptimization::get(const std::string& param, units::unit unitType) const
{
    double val;
    if (param == "voltagetolerance") {
        val = tols.voltageTolerance;
    }
    if (param == "angletolerance") {
        val = tols.angleTolerance;
    } else {
        val = gridDynSimulation::get(param, unitType);
    }
    return val;
}

coreObject* gridDynOptimization::find(const std::string& objName) const
{
    if (objName == "optroot") {
        return areaOpt;
    }
    if (objName.substr(0, 3) == "opt") {
        return areaOpt->find(objName.substr(3));
    }
    return gridDynSimulation::find(objName);
}

coreObject* gridDynOptimization::getSubObject(const std::string& typeName, index_t num) const
{
    if (typeName.substr(0, 3) == "opt") {
        return areaOpt->getSubObject(typeName.substr(3), num);
    }
    return gridDynSimulation::getSubObject(typeName, num);
}
coreObject* gridDynOptimization::findByUserID(const std::string& typeName, index_t searchID) const
{
    if (typeName.substr(0, 3) == "opt") {
        return areaOpt->findByUserID(typeName.substr(3), searchID);
    }
    return gridDynSimulation::findByUserID(typeName, searchID);
}

gridOptObject* gridDynOptimization::getOptData(coreObject* obj)
{
    if (obj != nullptr) {
        coreObject* nobj = areaOpt->find(obj->getName());
        if (nobj != nullptr) {
            return static_cast<gridOptObject*>(nobj);
        }
        return nullptr;
    }
    return areaOpt;
}

gridOptObject* gridDynOptimization::makeOptObjectPath(coreObject* obj)
{
    gridOptObject* oo = getOptData(obj);
    if (oo != nullptr) {
        return oo;
    }
    if (!(obj->isRoot())) {
        auto oop = makeOptObjectPath(obj->getParent());
        oo = coreOptObjectFactory::instance()->createObject(obj);
        oop->add(oo);
        return oo;
    }
    return nullptr;
}

optimizerInterface* gridDynOptimization::updateOptimizer(const optimMode& oMode)
{
    oData[oMode.offsetIndex] = makeOptimizer(this, oMode);
    optimizerInterface* of = oData[oMode.offsetIndex].get();

    return of;
}

}  // namespace griddyn
