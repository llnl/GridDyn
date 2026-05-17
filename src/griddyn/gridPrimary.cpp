/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridPrimary.h"

#include "core/coreObjectTemplates.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>

namespace griddyn {
gridPrimary::gridPrimary(const std::string& objName): GridComponent(objName) {}
CoreObject* gridPrimary::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<gridPrimary, GridComponent>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->zone = zone;
    return nobj;
}

void gridPrimary::pFlowInitializeA(coreTime time0, std::uint32_t flags)
{
    GridComponent::pFlowInitializeA(time0, flags);
}

void gridPrimary::pFlowInitializeB()
{
    GridComponent::pFlowInitializeB();
}
void gridPrimary::dynInitializeA(coreTime time0, std::uint32_t flags)
{
    GridComponent::dynInitializeA(time0, flags);
}

void gridPrimary::dynInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (isEnabled()) {
        GridComponent::dynInitializeB(inputs, desiredOutput, fieldSet);
        updateLocalCache();
    }
}

void gridPrimary::set(std::string_view param, std::string_view val)
{
    GridComponent::set(param, val);
}
void gridPrimary::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "zone") || (param == "zone number")) {
        zone = static_cast<int>(val);
    } else {
        GridComponent::set(param, val, unitType);
    }
}

double gridPrimary::get(std::string_view param, units::unit unitType) const
{
    if (param == "zone") {
        return static_cast<double>(zone);
    }
    return GridComponent::get(param, unitType);
}

void gridPrimary::converge(coreTime /*time*/,
                           double /*state*/[],
                           double /*dstate_dt*/[],
                           const solverMode& /*sMode*/,
                           converge_mode /*mode*/,
                           double /*tol*/)
{
}

void gridPrimary::setState(coreTime time,
                           const double state[],
                           const double dstate_dt[],
                           const solverMode& sMode)
{
    GridComponent::setState(time, state, dstate_dt, sMode);
    // update local computations
    updateLocalCache();
}

void gridPrimary::delayedResidual(const IOdata& inputs,
                                  const stateData& sD,
                                  double resid[],
                                  const solverMode& sMode)
{
    residual(inputs, sD, resid, sMode);
}

void gridPrimary::delayedDerivative(const IOdata& inputs,
                                    const stateData& sD,
                                    double deriv[],
                                    const solverMode& sMode)
{
    derivative(inputs, sD, deriv, sMode);
}

void gridPrimary::delayedAlgebraicUpdate(const IOdata& inputs,
                                         const stateData& sD,
                                         double update[],
                                         const solverMode& sMode,
                                         double alpha)
{
    algebraicUpdate(inputs, sD, update, sMode, alpha);
}

void gridPrimary::delayedJacobian(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode)
{
    jacobianElements(inputs, sD, md, inputLocs, sMode);
}

void gridPrimary::pFlowCheck(std::vector<Violation>& /*Violation_vector*/) {}
void gridPrimary::updateLocalCache() {}
gridBus* gridPrimary::getBus(index_t /*num*/) const
{
    return nullptr;
}
Link* gridPrimary::getLink(index_t /*num*/) const
{
    return nullptr;
}
Area* gridPrimary::getArea(index_t /*num*/) const
{
    return nullptr;
}
Relay* gridPrimary::getRelay(index_t /*num*/) const
{
    return nullptr;
}
}  // namespace griddyn
