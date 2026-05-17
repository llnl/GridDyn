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
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static OptObjectFactory<GridLinkOpt, Link> opLink("basic", "link");
// NOLINTBEGIN(bugprone-branch-clone)

using units::unit;

GridLinkOpt::GridLinkOpt(const std::string& objName):
    GridOptObject(objName), B1(nullptr), B2(nullptr), rampUpLimit(0.0), rampDownLimit(0.0)
{
}

GridLinkOpt::GridLinkOpt(CoreObject* obj, const std::string& objName):
    GridOptObject(objName), B1(nullptr), B2(nullptr), link(dynamic_cast<Link*>(obj)),
    rampUpLimit(0.0), rampDownLimit(0.0)
{
    if (link != nullptr) {
        if (getName().empty()) {
            setName(link->getName());
        }
        setUserID(link->getUserID());
    }
}

CoreObject* GridLinkOpt::clone(CoreObject* obj) const
{
    GridLinkOpt* nobj;
    if (obj == nullptr) {
        nobj = new GridLinkOpt();
    } else {
        nobj = dynamic_cast<GridLinkOpt*>(obj);
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

void GridLinkOpt::dynObjectInitializeA(std::uint32_t /*flags*/) {}

void GridLinkOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& offsetData = offsets.getOffsets(oMode);
    offsetData.reset();
    switch (oMode.flowMode) {
        case FlowModel::NONE:
            offsetData.local.contSize = 0;
            break;
        case FlowModel::TRANSPORT:
            offsetData.local.contSize = 1;
            break;
        case FlowModel::DC:
            offsetData.local.contSize = 0;
            offsetData.local.constraintsSize = 1;
            break;
        case FlowModel::AC:
            offsetData.local.contSize = 0;
            offsetData.local.constraintsSize = 1;
            break;
    }
    offsetData.localLoad(true);
}

void GridLinkOpt::add(CoreObject* obj)
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

void GridLinkOpt::remove(CoreObject* /*obj*/) {}

void GridLinkOpt::setValues(const OptimizationData& /*optimizationData*/,
                            const OptimizationMode& /*oMode*/)
{
}

// for saving the state
void GridLinkOpt::guessState(double /*time*/, double /*val*/[], const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::getVariableType(double /*sdata*/[], const OptimizationMode& /*oMode*/) {}

void GridLinkOpt::getTols(double /*tols*/[], const OptimizationMode& /*oMode*/) {}
void GridLinkOpt::valueBounds(double /*time*/,
                              double /*upperLimit*/[],
                              double /*lowerLimit*/[],
                              const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::linearObj(const OptimizationData& /*optimizationData*/,
                            vectData<double>& /*linObj*/,
                            const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::quadraticObj(const OptimizationData& /*optimizationData*/,
                               vectData<double>& /*linObj*/,
                               vectData<double>& /*quadObj*/,
                               const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::constraintValue(const OptimizationData& /*optimizationData*/,
                                  double /*cVals*/[],
                                  const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::constraintJacobianElements(const OptimizationData& /*optimizationData*/,
                                             matrixData<double>& /*matrixDataRef*/,
                                             const OptimizationMode& /*oMode*/)
{
}

double GridLinkOpt::objValue(const OptimizationData& /*optimizationData*/,
                             const OptimizationMode& /*oMode*/)
{
    const double cost = 0;

    return cost;
}

void GridLinkOpt::gradient(const OptimizationData& /*optimizationData*/,
                           double grad[] /*grad*/,
                           const OptimizationMode& /*oMode*/)
{
    static_cast<void>(grad);
}

void GridLinkOpt::jacobianElements(const OptimizationData& /*optimizationData*/,
                                   matrixData<double>& /*matrixDataRef*/,
                                   const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::getConstraints(const OptimizationData& /*optimizationData*/,
                                 matrixData<double>& /*cons*/,
                                 double /*upperLimit*/[],
                                 double /*lowerLimit*/[],
                                 const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::getObjectiveNames(stringVec& /*objectiveNames*/,
                                    const OptimizationMode& /*oMode*/,
                                    const std::string& /*prefix*/)
{
}

void GridLinkOpt::disable()
{
    CoreObject::disable();
}

void GridLinkOpt::setOffsets(const OptimizationOffsets& /*newOffset*/,
                             const OptimizationMode& /*oMode*/)
{
}

// destructor
GridLinkOpt::~GridLinkOpt() = default;

// set properties
void GridLinkOpt::set(std::string_view param, std::string_view val)
{
    if (param == "#") {
    } else {
        GridOptObject::set(param, val);
    }
}

void GridLinkOpt::set(std::string_view param, double val, unit unitType)
{
    if ((param == "voltagetolerance") || (param == "vtol")) {
    } else if ((param == "angletolerance") || (param == "atol")) {
    } else {
        GridOptObject::set(param, val, unitType);
    }
}

CoreObject* GridLinkOpt::find(std::string_view objName) const
{
    if ((objName == getName()) || (objName == "link")) {
        return const_cast<GridLinkOpt*>(this);
    }
    if ((objName == "b1") || (objName == "bus1") || (objName == "bus")) {
        return B1;
    }
    if ((objName == "b2") || (objName == "bus2")) {
        return B2;
    }

    return (CoreObject::find(objName));
}

CoreObject* GridLinkOpt::getSubObject(std::string_view typeName, index_t num) const
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

CoreObject* GridLinkOpt::findByUserID(std::string_view typeName, index_t searchID) const
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

GridOptObject* GridLinkOpt::getBus(index_t index) const
{
    if (index == 1) {
        return B1;
    }
    if (index == 2) {
        return B2;
    }
    return nullptr;
}

GridOptObject* GridLinkOpt::getArea(index_t /*index*/) const
{
    return dynamic_cast<GridOptObject*>(getParent());
}

double GridLinkOpt::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param[0] != '#') {
        val = GridOptObject::get(param, unitType);
    }
    return val;
}

}  // namespace griddyn
// NOLINTEND(bugprone-branch-clone)
