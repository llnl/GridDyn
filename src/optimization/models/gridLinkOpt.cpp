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

void gridLinkOpt::loadSizes(const optimMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    oo.reset();
    switch (oMode.flowMode) {
        case flowModel_t::none:
            oo.local.contSize = 0;
            break;
        case flowModel_t::transport:
            oo.local.contSize = 1;
            break;
        case flowModel_t::dc:
            oo.local.contSize = 0;
            oo.local.constraintsSize = 1;
            break;
        case flowModel_t::ac:
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

void gridLinkOpt::setValues(const optimData& /*of*/, const optimMode& /*oMode*/) {}

// for saving the state
void gridLinkOpt::guessState(double /*time*/, double /*val*/[], const optimMode& /*oMode*/) {}

void gridLinkOpt::getVariableType(double /*sdata*/[], const optimMode& /*oMode*/) {}

void gridLinkOpt::getTols(double /*tols*/[], const optimMode& /*oMode*/) {}
void gridLinkOpt::valueBounds(double /*time*/,
                              double /*upperLimit*/[],
                              double /*lowerLimit*/[],
                              const optimMode& /*oMode*/)
{
}

void gridLinkOpt::linearObj(const optimData& /*of*/,
                            vectData<double>& /*linObj*/,
                            const optimMode& /*oMode*/)
{
}

void gridLinkOpt::quadraticObj(const optimData& /*of*/,
                               vectData<double>& /*linObj*/,
                               vectData<double>& /*quadObj*/,
                               const optimMode& /*oMode*/)
{
}

void gridLinkOpt::constraintValue(const optimData& /*of*/,
                                  double /*cVals*/[],
                                  const optimMode& /*oMode*/)
{
}

void gridLinkOpt::constraintJacobianElements(const optimData& /*of*/,
                                             matrixData<double>& /*md*/,
                                             const optimMode& /*oMode*/)
{
}

double gridLinkOpt::objValue(const optimData& /*of*/, const optimMode& /*oMode*/)
{
    double cost = 0;

    return cost;
}

void gridLinkOpt::gradient(const optimData& /*of*/, double[] /*deriv*/, const optimMode& /*oMode*/)
{
}

void gridLinkOpt::jacobianElements(const optimData& /*of*/,
                                   matrixData<double>& /*md*/,
                                   const optimMode& /*oMode*/)
{
}

void gridLinkOpt::getConstraints(const optimData& /*of*/,
                                 matrixData<double>& /*cons*/,
                                 double /*upperLimit*/[],
                                 double /*lowerLimit*/[],
                                 const optimMode& /*oMode*/)
{
}

void gridLinkOpt::getObjName(stringVec& /*objNames*/,
                             const optimMode& /*oMode*/,
                             const std::string& /*prefix*/)
{
}

void gridLinkOpt::disable()
{
    coreObject::disable();
}

void gridLinkOpt::setOffsets(const optimOffsets& /*newOffset*/, const optimMode& /*oMode*/) {}

// destructor
gridLinkOpt::~gridLinkOpt() = default;

// set properties
void gridLinkOpt::set(const std::string& param, const std::string& val)
{
    if (param == "#") {
    } else {
        gridOptObject::set(param, val);
    }
}

void gridLinkOpt::set(const std::string& param, double val, unit unitType)
{
    if ((param == "voltagetolerance") || (param == "vtol")) {
    } else if ((param == "angletolerance") || (param == "atol")) {
    } else {
        gridOptObject::set(param, val, unitType);
    }
}

coreObject* gridLinkOpt::find(const std::string& objName) const
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

coreObject* gridLinkOpt::getSubObject(const std::string& typeName, index_t num) const
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

coreObject* gridLinkOpt::findByUserID(const std::string& typeName, index_t searchID) const
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

double gridLinkOpt::get(const std::string& param, units::unit unitType) const
{
    double val = kNullVal;
    if (param[0] != '#') {
        val = gridOptObject::get(param, unitType);
    }
    return val;
}

}  // namespace griddyn
