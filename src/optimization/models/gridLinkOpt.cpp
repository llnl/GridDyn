/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridLinkOpt.h"

#include "../optObjectFactory.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridAreaOpt.h"
#include "gridBusOpt.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "utilities/MatrixData.hpp"
#include "utilities/vectData.hpp"
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace griddyn {
static OptObjectFactory<GridLinkOpt, Link> gOpLink("basic", "link");
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

CoreObject* GridLinkOpt::sourceObject() const
{
    return link;
}

namespace {
    // Endpoint lookup follows nested optimization areas recursively.
    // NOLINTNEXTLINE(misc-no-recursion)
    GridBusOpt* findBusAdapter(GridOptObject* parent, const GridBus* sourceBus)
    {
        if (sourceBus == nullptr) {
            return nullptr;
        }
        for (index_t index = 0;; ++index) {
            auto* busAdapter = dynamic_cast<GridBusOpt*>(parent->getBus(index));
            if (busAdapter == nullptr) {
                break;
            }
            if (busAdapter->sourceBus() == sourceBus) {
                return busAdapter;
            }
        }
        for (index_t index = 0;; ++index) {
            auto* areaAdapter = parent->getArea(index);
            if (areaAdapter == nullptr) {
                break;
            }
            auto* busAdapter = findBusAdapter(areaAdapter, sourceBus);
            if (busAdapter != nullptr) {
                return busAdapter;
            }
        }
        return nullptr;
    }

    constexpr double kDcReactanceTolerance = 1e-12;
    constexpr double kDcAngleLimitTolerance = 1e-6;

    bool isFinitePhysicalValue(double value)
    {
        return std::isfinite(value) && (value != kNullVal) && (std::abs(value) < kHalfBigNum);
    }

    double optionalLinkValue(const Link* sourceLink,
                             std::string_view parameter,
                             double defaultValue)
    {
        if (sourceLink == nullptr) {
            return defaultValue;
        }
        const double value = sourceLink->get(parameter);
        return isFinitePhysicalValue(value) ? value : defaultValue;
    }

    bool isFinitePositiveLimit(double value)
    {
        return isFinitePhysicalValue(value) && (value > 0.0);
    }
}  // namespace

void GridLinkOpt::dynObjectInitializeA(std::uint32_t /*flags*/)
{
    if ((link == nullptr) || (getParent() == nullptr)) {
        return;
    }
    auto* parentOpt = dynamic_cast<GridOptObject*>(getParent());
    auto* bus1 = link->getBus(1);
    auto* bus2 = link->getBus(2);
    // Bind to the physical endpoint pointer.  Imported bus names and user IDs
    // need not be unique in every input format, so neither is a safe key here.
    B1 = findBusAdapter(parentOpt, bus1);
    B2 = findBusAdapter(parentOpt, bus2);
    if (B1 != nullptr) {
        B1->add(this);
    }
    if (B2 != nullptr) {
        B2->add(this);
    }
}

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
            offsetData.local.constraintsSize = 0;
            if (hasDcFlowLimit()) {
                ++offsetData.local.constraintsSize;
            }
            if (hasDcAngleLimit()) {
                ++offsetData.local.constraintsSize;
            }
            break;
        case FlowModel::AC:
            offsetData.local.contSize = 0;
            offsetData.local.constraintsSize = 1;
            break;
    }
    if ((oMode.flowMode == FlowModel::DC) && isActiveDcLink() && !isDcFlowValid()) {
        logging::error(this, "invalid reactance, tap, or angle limit for DC optimization flow");
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
        throw(UnrecognizedObjectException(this));
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

void GridLinkOpt::constraintValue(const OptimizationData& optimizationData,
                                  double cVals[],
                                  const OptimizationMode& oMode)
{
    if ((cVals == nullptr) || (optimizationData.val == nullptr) ||
        (oMode.flowMode != FlowModel::DC) || !isActiveDcLink()) {
        return;
    }

    const auto& linkOffsets = offsets.getOffsets(oMode);
    index_t row = linkOffsets.constraintOffset;
    if (hasDcFlowLimit()) {
        // The branch row uses the same signed from-to flow as the physical
        // link and the bus-balance equations.
        cVals[row++] = dcPowerFlow(B1, optimizationData, oMode);
    }
    if (hasDcAngleLimit()) {
        const auto& bus1Offsets = B1->offsets.getOffsets(oMode);
        const auto& bus2Offsets = B2->offsets.getOffsets(oMode);
        cVals[row] = optimizationData.val[bus1Offsets.aOffset] -
            optimizationData.val[bus2Offsets.aOffset] - dcPhaseShift();
    }
}

void GridLinkOpt::constraintJacobianElements(const OptimizationData& /*optimizationData*/,
                                             MatrixData<double>& matrixDataRef,
                                             const OptimizationMode& oMode)
{
    if ((oMode.flowMode != FlowModel::DC) || !isActiveDcLink()) {
        return;
    }

    const auto& linkOffsets = offsets.getOffsets(oMode);
    index_t row = linkOffsets.constraintOffset;
    if (hasDcFlowLimit()) {
        const double coefficient = dcFlowCoefficient();
        matrixDataRef.assign(row,
                             B1->offsets.getOffsets(oMode).aOffset,
                             coefficient);
        matrixDataRef.assign(row,
                             B2->offsets.getOffsets(oMode).aOffset,
                             -coefficient);
        ++row;
    }
    if (hasDcAngleLimit()) {
        matrixDataRef.assign(row, B1->offsets.getOffsets(oMode).aOffset, 1.0);
        matrixDataRef.assign(row, B2->offsets.getOffsets(oMode).aOffset, -1.0);
    }
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
                                   MatrixData<double>& /*matrixDataRef*/,
                                   const OptimizationMode& /*oMode*/)
{
}

void GridLinkOpt::getConstraints(const OptimizationData& /*optimizationData*/,
                                 MatrixData<double>& cons,
                                 double upperLimit[],
                                 double lowerLimit[],
                                 const OptimizationMode& oMode)
{
    if ((oMode.flowMode != FlowModel::DC) || !isActiveDcLink() ||
        (upperLimit == nullptr) || (lowerLimit == nullptr)) {
        return;
    }

    const auto& linkOffsets = offsets.getOffsets(oMode);
    index_t row = linkOffsets.constraintOffset;
    if (hasDcFlowLimit()) {
        const double rating = link->get("ratinga");
        const double coefficient = dcFlowCoefficient();
        // The callback evaluates coefficient * (theta1 - theta2 - shift),
        // while this legacy linear-row interface stores only A and bounds.
        // Move the fixed phase-shift term into the row bounds so both forms
        // represent the same inequality.
        const double phaseShiftTerm = coefficient * dcPhaseShift();
        lowerLimit[row] = -rating + phaseShiftTerm;
        upperLimit[row] = rating + phaseShiftTerm;
        cons.assign(row,
                    B1->offsets.getOffsets(oMode).aOffset,
                    coefficient);
        cons.assign(row,
                    B2->offsets.getOffsets(oMode).aOffset,
                    -coefficient);
        ++row;
    }
    if (hasDcAngleLimit()) {
        // As above, the callback evaluates theta1 - theta2 - shift.
        const double phaseShift = dcPhaseShift();
        lowerLimit[row] = link->get("minangle") + phaseShift;
        upperLimit[row] = link->get("maxangle") + phaseShift;
        cons.assign(row, B1->offsets.getOffsets(oMode).aOffset, 1.0);
        cons.assign(row, B2->offsets.getOffsets(oMode).aOffset, -1.0);
    }
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

void GridLinkOpt::setOffsets(const OptimizationOffsets& newOffsets,
                             const OptimizationMode& oMode)
{
    // Link rows are allocated locally, but their offsets are assigned by the
    // owning area's traversal just like bus and generator offsets.
    GridOptObject::setOffsets(newOffsets, oMode);
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

bool GridLinkOpt::isActiveDcLink() const
{
    return (link != nullptr) && link->isEnabled() && link->isConnected() &&
        (B1 != nullptr) && (B2 != nullptr) && B1->isEnabled() && B2->isEnabled();
}

bool GridLinkOpt::isDcFlowValid() const
{
    if (!isActiveDcLink()) {
        return true;
    }

    const double reactance = link->get("x");
    const double tap = optionalLinkValue(link, "tap", 1.0);
    const double phaseShift = optionalLinkValue(link, "tapangle", 0.0);
    if (!isFinitePhysicalValue(reactance) || (std::abs(reactance) < kDcReactanceTolerance) ||
        !isFinitePhysicalValue(tap) || (tap <= kDcReactanceTolerance) ||
        !isFinitePhysicalValue(phaseShift)) {
        return false;
    }

    if (dynamic_cast<const AcLine*>(link) != nullptr) {
        const double minimumAngle = link->get("minangle");
        const double maximumAngle = link->get("maxangle");
        if (!isFinitePhysicalValue(minimumAngle) || !isFinitePhysicalValue(maximumAngle) ||
            (minimumAngle > maximumAngle)) {
            return false;
        }
    }
    return true;
}

bool GridLinkOpt::hasDcFlowLimit() const
{
    return isActiveDcLink() && isDcFlowValid() &&
        isFinitePositiveLimit(link->get("ratinga"));
}

bool GridLinkOpt::hasDcAngleLimit() const
{
    if (!isActiveDcLink() || (dynamic_cast<const AcLine*>(link) == nullptr) ||
        !isDcFlowValid()) {
        return false;
    }

    const double minimumAngle = link->get("minangle");
    const double maximumAngle = link->get("maxangle");
    // MATPOWER/PYPOWER use +/-360 degrees to mean that the angle constraint
    // is not active.  GridDyn normally stores these limits in radians, but
    // accept the raw sentinel too so this boundary is robust to input units.
    const bool hasUnboundedSentinel =
        ((std::abs(minimumAngle + 2.0 * kPI) <= kDcAngleLimitTolerance) &&
         (std::abs(maximumAngle - 2.0 * kPI) <= kDcAngleLimitTolerance)) ||
        ((std::abs(minimumAngle + 360.0) <= kDcAngleLimitTolerance) &&
         (std::abs(maximumAngle - 360.0) <= kDcAngleLimitTolerance));
    return !hasUnboundedSentinel;
}

double GridLinkOpt::dcFlowCoefficient() const
{
    if (!isActiveDcLink()) {
        return 0.0;
    }
    if (!isDcFlowValid()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double reactance = link->get("x");
    const double tap = optionalLinkValue(link, "tap", 1.0);
    return 1.0 / (reactance * tap);
}

double GridLinkOpt::dcPhaseShift() const
{
    return isActiveDcLink() ? optionalLinkValue(link, "tapangle", 0.0) : 0.0;
}

double GridLinkOpt::dcPowerFlow(const GridBusOpt* sourceBus,
                                const OptimizationData& optimizationData,
                                const OptimizationMode& oMode) const
{
    if ((sourceBus == nullptr) || (optimizationData.val == nullptr) ||
        (oMode.flowMode != FlowModel::DC) || !isActiveDcLink()) {
        return 0.0;
    }
    if (!isDcFlowValid()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if ((sourceBus != B1) && (sourceBus != B2)) {
        return 0.0;
    }
    const auto& bus1Offsets = B1->offsets.getOffsets(oMode);
    const auto& bus2Offsets = B2->offsets.getOffsets(oMode);
    const double flow = dcFlowCoefficient() *
        (optimizationData.val[bus1Offsets.aOffset] -
         optimizationData.val[bus2Offsets.aOffset] - dcPhaseShift());
    return (sourceBus == B1) ? flow : -flow;
}

void GridLinkOpt::dcPowerFlowJacobian(const GridBusOpt* sourceBus,
                                      index_t constraintRow,
                                      MatrixData<double>& matrixDataRef,
                                      const OptimizationMode& oMode) const
{
    if ((sourceBus == nullptr) || (oMode.flowMode != FlowModel::DC) || (B1 == nullptr) ||
        (B2 == nullptr) || !isActiveDcLink() || !isDcFlowValid()) {
        return;
    }
    auto* otherBus = (sourceBus == B1) ? B2 : nullptr;
    if ((otherBus == nullptr) && (sourceBus == B2)) {
        otherBus = B1;
    }
    if (otherBus == nullptr) {
        return;
    }
    // The bus balance subtracts the signed branch flow. Hence the derivative
    // signs are the negative of the from-to flow derivatives.
    const double coefficient = dcFlowCoefficient();
    matrixDataRef.assign(constraintRow,
                         sourceBus->offsets.getOffsets(oMode).aOffset,
                         -coefficient);
    matrixDataRef.assign(constraintRow,
                         otherBus->offsets.getOffsets(oMode).aOffset,
                         coefficient);
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
