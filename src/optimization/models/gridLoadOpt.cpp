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

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static OptObjectFactory<GridLoadOpt, zipLoad> opLoad("basic", "load");

GridLoadOpt::GridLoadOpt(const std::string& objName): GridOptObject(objName) {}

GridLoadOpt::GridLoadOpt(CoreObject* obj, const std::string& objName):
    GridOptObject(objName), load(dynamic_cast<zipLoad*>(obj))
{
    if (load != nullptr) {
        if (getName().empty()) {
            setName(load->getName());
        }
        setUserID(load->getUserID());
    }
}

CoreObject* GridLoadOpt::clone(CoreObject* obj) const
{
    GridLoadOpt* nobj;
    if (obj == nullptr) {
        nobj = new GridLoadOpt();
    } else {
        nobj = dynamic_cast<GridLoadOpt*>(obj);
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

    return nobj;
}

void GridLoadOpt::add(CoreObject* obj)
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

count_t GridLoadOpt::objSize(const OptimizationMode& /*oMode*/)
{
    const count_t objs = 0;

    return objs;
}
count_t GridLoadOpt::contObjSize(const OptimizationMode& /*oMode*/)
{
    const count_t objs = 0;

    return objs;
}

count_t GridLoadOpt::intObjSize(const OptimizationMode& /*oMode*/)
{
    const count_t objs = 0;

    return objs;
}

count_t GridLoadOpt::constraintSize(const OptimizationMode& oMode)
{
    count_t objs = 0;
    switch (oMode.linMode) {
        case LinearityMode::LINEAR:
        case LinearityMode::QUADRATIC:
        case LinearityMode::NONLINEAR:
        default:
            objs = 0;
    }

    return objs;
}

void GridLoadOpt::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void GridLoadOpt::setValues(const OptimizationData& /*of*/, const OptimizationMode& /*oMode*/) {}
// for saving the state
void GridLoadOpt::guessState(double /*time*/, double /*val*/[], const OptimizationMode& /*oMode*/)
{
}

void GridLoadOpt::getVariableType(double /*sdata*/[], const OptimizationMode& /*oMode*/) {}

void GridLoadOpt::getTols(double /*tols*/[], const OptimizationMode& /*oMode*/) {}
void GridLoadOpt::valueBounds(double /*time*/,
                              double /*upperLimit*/[],
                              double /*lowerLimit*/[],
                              const OptimizationMode& /*oMode*/)
{
}

void GridLoadOpt::linearObj(const OptimizationData& /*of*/,
                            vectData<double>& /*linObj*/,
                            const OptimizationMode& /*oMode*/)
{
}
void GridLoadOpt::quadraticObj(const OptimizationData& /*of*/,
                               vectData<double>& /*linObj*/,
                               vectData<double>& /*quadObj*/,
                               const OptimizationMode& /*oMode*/)
{
}

void GridLoadOpt::constraintValue(const OptimizationData& /*of*/,
                                  double /*cVals*/[],
                                  const OptimizationMode& /*oMode*/)
{
}
void GridLoadOpt::constraintJacobianElements(const OptimizationData& /*of*/,
                                             matrixData<double>& /*md*/,
                                             const OptimizationMode& /*oMode*/)
{
}

double GridLoadOpt::objValue(const OptimizationData& /*of*/, const OptimizationMode& /*oMode*/)
{
    const double cost = 0;

    return cost;
}

void GridLoadOpt::gradient(const OptimizationData& /*of*/,
                           double /*deriv*/[],
                           const OptimizationMode& /*oMode*/)
{
}
void GridLoadOpt::jacobianElements(const OptimizationData& /*of*/,
                                   matrixData<double>& /*md*/,
                                   const OptimizationMode& /*oMode*/)
{
}
void GridLoadOpt::getConstraints(const OptimizationData& /*of*/,
                                 matrixData<double>& /*cons*/,
                                 double /*upperLimit*/[],
                                 double /*lowerLimit*/[],
                                 const OptimizationMode& /*oMode*/)
{
}
void GridLoadOpt::getObjectiveNames(stringVec& /*objectiveNames*/,
                                    const OptimizationMode& /*oMode*/,
                                    const std::string& /*prefix*/)
{
}

// destructor
GridLoadOpt::~GridLoadOpt() = default;

// set properties
void GridLoadOpt::set(std::string_view param, std::string_view val)
{
    if (param[0] == '#') {
    } else {
        GridOptObject::set(param, val);
    }
}

void GridLoadOpt::set(std::string_view param, double val, unit unitType)
{
    if (param[0] == '#') {
    } else {
        GridOptObject::set(param, val, unitType);
    }
}

double GridLoadOpt::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "#") {
    } else {
        val = GridOptObject::get(param, unitType);
    }
    return val;
}

GridOptObject* GridLoadOpt::getBus(index_t /*index*/) const
{
    return bus;
}

GridOptObject* GridLoadOpt::getArea(index_t index) const
{
    return bus->getArea(index);
}

}  // namespace griddyn
