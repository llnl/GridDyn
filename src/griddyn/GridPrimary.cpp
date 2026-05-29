/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridPrimary.h"

#include "core/CoreObjectTemplates.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>

namespace griddyn {
GridPrimary::GridPrimary(const std::string& objName): GridComponent(objName) {}
CoreObject* GridPrimary::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<GridPrimary, GridComponent>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->zone = zone;
    return nobj;
}

void GridPrimary::pFlowInitializeA(coreTime time0, std::uint32_t flags)
{
    GridComponent::pFlowInitializeA(time0, flags);
}

void GridPrimary::pFlowInitializeB()
{
    GridComponent::pFlowInitializeB();
}
void GridPrimary::dynInitializeA(coreTime time0, std::uint32_t flags)
{
    GridComponent::dynInitializeA(time0, flags);
}

void GridPrimary::dynInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    if (isEnabled()) {
        GridComponent::dynInitializeB(inputs, desiredOutput, fieldSet);
        updateLocalCache();
    }
}

void GridPrimary::set(std::string_view param, std::string_view val)
{
    GridComponent::set(param, val);
}
void GridPrimary::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "zone") || (param == "zone number")) {
        zone = static_cast<int>(val);
    } else {
        GridComponent::set(param, val, unitType);
    }
}

double GridPrimary::get(std::string_view param, units::unit unitType) const
{
    if (param == "zone") {
        return static_cast<double>(zone);
    }
    return GridComponent::get(param, unitType);
}

void GridPrimary::converge(coreTime /*time*/,
                           double /*state*/[],
                           double /*dstateDt*/[],
                           const SolverMode& /*sMode*/,
                           ConvergeMode /*mode*/,
                           double /*tol*/)
{
}

void GridPrimary::setState(coreTime time,
                           const double state[],
                           const double dstateDt[],
                           const SolverMode& sMode)
{
    GridComponent::setState(time, state, dstateDt, sMode);
    // update local computations
    updateLocalCache();
}

void GridPrimary::delayedResidual(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  double resid[],
                                  const SolverMode& sMode)
{
    residual(inputs, stateDataValue, resid, sMode);
}

void GridPrimary::delayedDerivative(const IOdata& inputs,
                                    const stateData& stateDataValue,
                                    double deriv[],
                                    const SolverMode& sMode)
{
    derivative(inputs, stateDataValue, deriv, sMode);
}

void GridPrimary::delayedAlgebraicUpdate(const IOdata& inputs,
                                         const stateData& stateDataValue,
                                         double update[],
                                         const SolverMode& sMode,
                                         double alpha)
{
    algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
}

void GridPrimary::delayedJacobian(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  matrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode)
{
    jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
}

void GridPrimary::pFlowCheck(std::vector<Violation>& /*Violation_vector*/) {}
void GridPrimary::updateLocalCache() {}
GridBus* GridPrimary::getBus(index_t /*num*/) const
{
    return nullptr;
}
Link* GridPrimary::getLink(index_t /*num*/) const
{
    return nullptr;
}
GridArea* GridPrimary::getArea(index_t /*num*/) const
{
    return nullptr;
}
Relay* GridPrimary::getRelay(index_t /*num*/) const
{
    return nullptr;
}
}  // namespace griddyn
