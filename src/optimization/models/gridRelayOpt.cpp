/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridRelayOpt.h"

#include "../optObjectFactory.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridAreaOpt.h"
#include "griddyn/Relay.h"
#include "utilities/vectData.hpp"
#include <cmath>
#include <string>
#include <utility>

namespace griddyn {
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static OptObjectFactory<GridRelayOpt, Relay> opRelay("basic", "relay");

using units::unit;

GridRelayOpt::GridRelayOpt(const std::string& objName): GridOptObject(objName) {}

GridRelayOpt::GridRelayOpt(CoreObject* obj, const std::string& objName):
    GridOptObject(objName), relay(dynamic_cast<Relay*>(obj))
{
    if (relay != nullptr) {
        if (getName().empty()) {
            setName(relay->getName());
        }
        setUserID(relay->getUserID());
    }
}

CoreObject* GridRelayOpt::clone(CoreObject* obj) const
{
    GridRelayOpt* nobj;
    if (obj == nullptr) {
        nobj = new GridRelayOpt();
    } else {
        nobj = dynamic_cast<GridRelayOpt*>(obj);
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

void GridRelayOpt::add(CoreObject* obj)
{
    if (dynamic_cast<Relay*>(obj) != nullptr) {
        relay = static_cast<Relay*>(obj);
        if (getName().empty()) {
            setName(relay->getName());
        }
        setUserID(relay->getUserID());
    } else {
        throw(unrecognizedObjectException(this));
    }
}

count_t GridRelayOpt::objSize(const OptimizationMode& /*oMode*/)
{
    const count_t objs = 0;

    return objs;
}
count_t GridRelayOpt::contObjSize(const OptimizationMode& /*oMode*/)
{
    const count_t objs = 0;

    return objs;
}

count_t GridRelayOpt::intObjSize(const OptimizationMode& /*oMode*/)
{
    const count_t objs = 0;

    return objs;
}

count_t GridRelayOpt::constraintSize(const OptimizationMode& oMode)
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

void GridRelayOpt::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void GridRelayOpt::remove(CoreObject* /*obj*/) {}

void GridRelayOpt::setValues(const OptimizationData& /*of*/, const OptimizationMode& /*oMode*/) {}
// for saving the state
void GridRelayOpt::guessState(double /*time*/, double /*val*/[], const OptimizationMode& /*oMode*/)
{
}

void GridRelayOpt::getVariableType(double /*sdata*/[], const OptimizationMode& /*oMode*/) {}

void GridRelayOpt::getTols(double /*tols*/[], const OptimizationMode& /*oMode*/) {}
void GridRelayOpt::valueBounds(double /*time*/,
                               double /*upperLimit*/[],
                               double /*lowerLimit*/[],
                               const OptimizationMode& /*oMode*/)
{
}

void GridRelayOpt::linearObj(const OptimizationData& /*of*/,
                             vectData<double>& /*linObj*/,
                             const OptimizationMode& /*oMode*/)
{
}
void GridRelayOpt::quadraticObj(const OptimizationData& /*of*/,
                                vectData<double>& /*linObj*/,
                                vectData<double>& /*quadObj*/,
                                const OptimizationMode& /*oMode*/)
{
}

void GridRelayOpt::constraintValue(const OptimizationData& /*of*/,
                                   double /*cVals*/[],
                                   const OptimizationMode& /*oMode*/)
{
}
void GridRelayOpt::constraintJacobianElements(const OptimizationData& /*of*/,
                                              matrixData<double>& /*md*/,
                                              const OptimizationMode& /*oMode*/)
{
}

double GridRelayOpt::objValue(const OptimizationData& /*of*/, const OptimizationMode& /*oMode*/)
{
    const double cost = 0;

    return cost;
}

void GridRelayOpt::gradient(const OptimizationData& /*of*/,
                            double /*deriv*/[],
                            const OptimizationMode& /*oMode*/)
{
}
void GridRelayOpt::jacobianElements(const OptimizationData& /*of*/,
                                    matrixData<double>& /*md*/,
                                    const OptimizationMode& /*oMode*/)
{
}
void GridRelayOpt::getConstraints(const OptimizationData& /*of*/,
                                  matrixData<double>& /*cons*/,
                                  double /*upperLimit*/[],
                                  double /*lowerLimit*/[],
                                  const OptimizationMode& /*oMode*/)
{
}
void GridRelayOpt::getObjectiveNames(stringVec& /*objectiveNames*/,
                                     const OptimizationMode& /*oMode*/,
                                     const std::string& /*prefix*/)
{
}

void GridRelayOpt::disable()
{
    GridOptObject::disable();
}

void GridRelayOpt::setOffsets(const OptimizationOffsets& /*newOffset*/,
                              const OptimizationMode& /*oMode*/)
{
}

// destructor
GridRelayOpt::~GridRelayOpt() = default;

// set properties
void GridRelayOpt::set(std::string_view param, std::string_view val)
{
    if (param == "#") {
    } else {
        GridOptObject::set(param, val);
    }
}

void GridRelayOpt::set(std::string_view param, double val, unit unitType)
{
    if (!((param == "voltagetolerance") || (param == "vtol") || (param == "angleetolerance") ||
          (param == "atol"))) {
        GridOptObject::set(param, val, unitType);
    }
}

CoreObject* GridRelayOpt::find(std::string_view objName) const
{
    CoreObject* obj = nullptr;
    if ((objName == getName()) || (objName == "relay")) {
        return const_cast<GridRelayOpt*>(this);
    }

    return obj;
}

CoreObject* GridRelayOpt::getSubObject([[maybe_unused]] std::string_view typeName,
                                       index_t /*num*/) const
{
    return nullptr;
    /*
    if (typeName == "target")
    {
        return nullptr;
    }

    else
    {
        return nullptr;
    }
    */
}

CoreObject* GridRelayOpt::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName == "relay") {
        if (searchID == getUserID()) {
            return const_cast<GridRelayOpt*>(this);
        }
    }
    return nullptr;
}

double GridRelayOpt::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param[0] == '#') {
    } else {
        val = GridOptObject::get(param, unitType);
    }
    return val;
}

}  // namespace griddyn

