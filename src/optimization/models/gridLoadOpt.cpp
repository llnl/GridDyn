/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridLoadOpt.h"

#include "../optObjectFactory.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridBusOpt.h"
#include "griddyn/loads/zipLoad.h"
#include "utilities/vectData.hpp"
#include <cmath>
#include <string>
#include <utility>

namespace griddyn {
using units::unit;

static optObjectFactory<gridLoadOpt, zipLoad> opLoad("basic", "load");

gridLoadOpt::gridLoadOpt(const std::string& objName): gridOptObject(objName) {}

gridLoadOpt::gridLoadOpt(coreObject* obj, const std::string& objName):
    gridOptObject(objName), load(dynamic_cast<zipLoad*>(obj))
{
    if (load != nullptr) {
        if (getName().empty()) {
            setName(load->getName());
        }
        setUserID(load->getUserID());
    }
}

coreObject* gridLoadOpt::clone(coreObject* obj) const
{
    gridLoadOpt* nobj;
    if (obj == nullptr) {
        nobj = new gridLoadOpt();
    } else {
        nobj = dynamic_cast<gridLoadOpt*>(obj);
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

    return nobj;
}

void gridLoadOpt::add(coreObject* obj)
{
    if (dynamic_cast<zipLoad*>(obj) != nullptr) {
        load = static_cast<zipLoad*>(obj);
        if (getName().empty()) {
            setName(load->getName());
        }
        setUserID(load->getUserID());
    } else {
        throw(unrecognizedObjectException(this));
    }
}

count_t gridLoadOpt::objSize(const optimMode& /*oMode*/)
{
    count_t objs = 0;

    return objs;
}
count_t gridLoadOpt::contObjSize(const optimMode& /*oMode*/)
{
    count_t objs = 0;

    return objs;
}

count_t gridLoadOpt::intObjSize(const optimMode& /*oMode*/)
{
    count_t objs = 0;

    return objs;
}

count_t gridLoadOpt::constraintSize(const optimMode& oMode)
{
    count_t objs = 0;
    switch (oMode.linMode) {
        case linearityMode_t::linear:
        case linearityMode_t::quadratic:
        case linearityMode_t::nonlinear:
        default:
            objs = 0;
    }

    return objs;
}

void gridLoadOpt::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void gridLoadOpt::setValues(const optimData& /*of*/, const optimMode& /*oMode*/) {}
// for saving the state
void gridLoadOpt::guessState(double /*time*/, double /*val*/[], const optimMode& /*oMode*/) {}

void gridLoadOpt::getVariableType(double /*sdata*/[], const optimMode& /*oMode*/) {}

void gridLoadOpt::getTols(double /*tols*/[], const optimMode& /*oMode*/) {}
void gridLoadOpt::valueBounds(double /*time*/,
                              double /*upperLimit*/[],
                              double /*lowerLimit*/[],
                              const optimMode& /*oMode*/)
{
}

void gridLoadOpt::linearObj(const optimData& /*of*/,
                            vectData<double>& /*linObj*/,
                            const optimMode& /*oMode*/)
{
}
void gridLoadOpt::quadraticObj(const optimData& /*of*/,
                               vectData<double>& /*linObj*/,
                               vectData<double>& /*quadObj*/,
                               const optimMode& /*oMode*/)
{
}

void gridLoadOpt::constraintValue(const optimData& /*of*/,
                                  double /*cVals*/[],
                                  const optimMode& /*oMode*/)
{
}
void gridLoadOpt::constraintJacobianElements(const optimData& /*of*/,
                                             matrixData<double>& /*md*/,
                                             const optimMode& /*oMode*/)
{
}

double gridLoadOpt::objValue(const optimData& /*of*/, const optimMode& /*oMode*/)
{
    double cost = 0;

    return cost;
}

void gridLoadOpt::gradient(const optimData& /*of*/, double /*deriv*/[], const optimMode& /*oMode*/)
{
}
void gridLoadOpt::jacobianElements(const optimData& /*of*/,
                                   matrixData<double>& /*md*/,
                                   const optimMode& /*oMode*/)
{
}
void gridLoadOpt::getConstraints(const optimData& /*of*/,
                                 matrixData<double>& /*cons*/,
                                 double /*upperLimit*/[],
                                 double /*lowerLimit*/[],
                                 const optimMode& /*oMode*/)
{
}
void gridLoadOpt::getObjName(stringVec& /*objNames*/,
                             const optimMode& /*oMode*/,
                             const std::string& /*prefix*/)
{
}

// destructor
gridLoadOpt::~gridLoadOpt() = default;

// set properties
void gridLoadOpt::set(const std::string& param, const std::string& val)
{
    if (param[0] == '#') {
    } else {
        gridOptObject::set(param, val);
    }
}

void gridLoadOpt::set(const std::string& param, double val, unit unitType)
{
    if (param[0] == '#') {
    } else {
        gridOptObject::set(param, val, unitType);
    }
}

double gridLoadOpt::get(const std::string& param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "#") {
    } else {
        val = gridOptObject::get(param, unitType);
    }
    return val;
}

gridOptObject* gridLoadOpt::getBus(index_t /*index*/) const
{
    return bus;
}

gridOptObject* gridLoadOpt::getArea(index_t index) const
{
    return bus->getArea(index);
}

}  // namespace griddyn
