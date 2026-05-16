/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridLinkOpt.h"

#include "../optObjectFactory.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridAreaOpt.h"
#include "gridBusOpt.h"
#include "griddyn/Link.h"
#include "utilities/vectData.hpp"
#include <cmath>
#include <string>
#include <utility>

namespace griddyn {
static optObjectFactory<gridLinkOpt, Link> opLink("basic", "link");

using units::unit;

gridLinkOpt::gridLinkOpt(const std::string& objName): gridOptObject(objName) {}

gridLinkOpt::gridLinkOpt(coreObject* obj, const std::string& objName):
    gridOptObject(objName), link(dynamic_cast<Link*>(obj))
{
    if (link != nullptr) {
        if (getName().empty()) {
            setName(link->getName());
        }
        setUserID(link->getUserID());
    }
}

coreObject* gridLinkOpt::clone(coreObject* obj) const
{
    gridLinkOpt* nobj;
    if (obj == nullptr) {
        nobj = new gridLinkOpt();
    } else {
        nobj = dynamic_cast<gridLinkOpt*>(obj);
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

void gridLinkOpt::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void gridLinkOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    oo.reset();
    switch (oMode.flowMode) {
        case FlowModel::NONE:
            oo.local.contSize = 0;
            break;
        case FlowModel::TRANSPORT:
            oo.local.contSize = 1;
            break;
        case FlowModel::DC:
            oo.local.contSize = 0;
            oo.local.constraintsSize = 1;
            break;
        case FlowModel::AC:
            oo.local.contSize = 0;
            oo.local.constraintsSize = 1;
            break;
    }
    oo.localLoad(true);
}

void gridLinkOpt::add(coreObject* obj)
{
    auto* tmpLink = dynamic_cast<Link*>(obj);
    if (tmpLink != nullptr) {
        link = tmpLink;
        if (getName().empty()) {
            setName(link->getName());
        }
        setUserID(link->getUserID());
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void gridLinkOpt::remove(coreObject* /*obj*/) {}

void gridLinkOpt::setValues(const OptimizationData& /*of*/, const OptimizationMode& /*oMode*/) {}

// for saving the state
void gridLinkOpt::guessState(double /*time*/, double /*val*/[], const OptimizationMode& /*oMode*/) {}

void gridLinkOpt::getVariableType(double /*sdata*/[], const OptimizationMode& /*oMode*/) {}

void gridLinkOpt::getTols(double /*tols*/[], const OptimizationMode& /*oMode*/) {}
void gridLinkOpt::valueBounds(double /*time*/,
                              double /*upperLimit*/[],
                              double /*lowerLimit*/[],
                              const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::linearObj(const OptimizationData& /*of*/,
                            vectData<double>& /*linObj*/,
                            const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::quadraticObj(const OptimizationData& /*of*/,
                               vectData<double>& /*linObj*/,
                               vectData<double>& /*quadObj*/,
                               const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::constraintValue(const OptimizationData& /*of*/,
                                  double /*cVals*/[],
                                  const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::constraintJacobianElements(const OptimizationData& /*of*/,
                                             matrixData<double>& /*md*/,
                                             const OptimizationMode& /*oMode*/)
{
}

double gridLinkOpt::objValue(const OptimizationData& /*of*/, const OptimizationMode& /*oMode*/)
{
    double cost = 0;

    return cost;
}

void gridLinkOpt::gradient(const OptimizationData& /*of*/, double[] /*deriv*/, const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::jacobianElements(const OptimizationData& /*of*/,
                                   matrixData<double>& /*md*/,
                                   const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::getConstraints(const OptimizationData& /*of*/,
                                 matrixData<double>& /*cons*/,
                                 double /*upperLimit*/[],
                                 double /*lowerLimit*/[],
                                 const OptimizationMode& /*oMode*/)
{
}

void gridLinkOpt::getObjName(stringVec& /*objNames*/,
                             const OptimizationMode& /*oMode*/,
                             const std::string& /*prefix*/)
{
}

void gridLinkOpt::disable()
{
    coreObject::disable();
}

void gridLinkOpt::setOffsets(const OptimizationOffsets& /*newOffset*/, const OptimizationMode& /*oMode*/) {}

// destructor
gridLinkOpt::~gridLinkOpt() = default;

// set properties
void gridLinkOpt::set(std::string_view param, std::string_view val)
{
    if (param == "#") {
    } else {
        gridOptObject::set(param, val);
    }
}

void gridLinkOpt::set(std::string_view param, double val, unit unitType)
{
    if ((param == "voltagetolerance") || (param == "vtol")) {
    } else if ((param == "angletolerance") || (param == "atol")) {
    } else {
        gridOptObject::set(param, val, unitType);
    }
}

coreObject* gridLinkOpt::find(std::string_view objName) const
{
    if ((objName == getName()) || (objName == "link")) {
        return const_cast<gridLinkOpt*>(this);
    }
    if ((objName == "b1") || (objName == "bus1") || (objName == "bus")) {
        return B1;
    }
    if ((objName == "b2") || (objName == "bus2")) {
        return B2;
    }

    return (coreObject::find(objName));
}

coreObject* gridLinkOpt::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "bus") {
        if (num == 1) {
            return B1;
        }
        if (num == 2) {
            return B2;
        }
    }
    return nullptr;
}

coreObject* gridLinkOpt::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName == "bus") {
        if (B1->getUserID() == searchID) {
            return B1;
        }
        if (B2->getUserID() == searchID) {
            return B2;
        }
    }

    return nullptr;
}

gridOptObject* gridLinkOpt::getBus(index_t index) const
{
    if (index == 1) {
        return B1;
    }
    if (index == 2) {
        return B2;
    }
    return nullptr;
}

gridOptObject* gridLinkOpt::getArea(index_t /*index*/) const
{
    return dynamic_cast<gridOptObject*>(getParent());
}

double gridLinkOpt::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param[0] != '#') {
        val = gridOptObject::get(param, unitType);
    }
    return val;
}

}  // namespace griddyn
