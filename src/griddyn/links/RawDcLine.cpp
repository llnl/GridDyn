/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RawDcLine.h"

#include "../GridBus.h"
#include "utilities/MatrixDataCompact.hpp"
#include <cmath>
#include <string>

namespace griddyn::links {
using units::convert;
using units::puMW;
using units::unit;

RawDcLine::RawDcLine(const std::string& objName): Link(objName) {}

index_t RawDcLine::fromReactiveOffset(const SolverMode& sMode) const
{
    return controlFromVoltage ? offsets.getAlgOffset(sMode) : kNullLocation;
}

index_t RawDcLine::toReactiveOffset(const SolverMode& sMode) const
{
    const auto offset = offsets.getAlgOffset(sMode);
    if (!controlToVoltage || (offset == kNullLocation)) {
        return kNullLocation;
    }
    return offset + (controlFromVoltage ? 1 : 0);
}

void RawDcLine::set(std::string_view param, double val, unit unitType)
{
    if (param == "from_vtarget") {
        fromVoltageTarget = val;
    } else if (param == "to_vtarget") {
        toVoltageTarget = val;
    } else if (param == "from_q") {
        fromReactivePower = convert(val, unitType, puMW, systemBasePower);
    } else if (param == "to_q") {
        toReactivePower = convert(val, unitType, puMW, systemBasePower);
    } else if (param == "from_voltage_control") {
        controlFromVoltage = (val > 0.5);
    } else if (param == "to_voltage_control") {
        controlToVoltage = (val > 0.5);
    } else {
        Link::set(param, val, unitType);
    }
}

double RawDcLine::get(std::string_view param, unit unitType) const
{
    if (param == "from_vtarget") {
        return fromVoltageTarget;
    }
    if (param == "to_vtarget") {
        return toVoltageTarget;
    }
    if (param == "from_q") {
        return convert(fromReactivePower, puMW, unitType, systemBasePower);
    }
    if (param == "to_q") {
        return convert(toReactivePower, puMW, unitType, systemBasePower);
    }
    if (param == "from_voltage_control") {
        return controlFromVoltage ? 1.0 : 0.0;
    }
    if (param == "to_voltage_control") {
        return controlToVoltage ? 1.0 : 0.0;
    }
    return Link::get(param, unitType);
}

void RawDcLine::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    Link::pFlowObjectInitializeA(time0, flags);
    offsets.local().local.algSize = localStateSizes(cPflowSolverMode).algSize;
    offsets.local().local.jacSize = localJacobianCount(cPflowSolverMode);
    updateLocalCache();
}

StateSizes RawDcLine::localStateSizes(const SolverMode& sMode) const
{
    StateSizes sizes;
    if (hasAlgebraic(sMode) && isConnected()) {
        sizes.algSize =
            static_cast<count_t>(controlFromVoltage) + static_cast<count_t>(controlToVoltage);
    }
    return sizes;
}

count_t RawDcLine::localJacobianCount(const SolverMode& sMode) const
{
    return localStateSizes(sMode).algSize;
}

void RawDcLine::updateLocalCache()
{
    if (!isEnabled() || !isConnected()) {
        linkFlows = {};
        return;
    }
    Link::updateLocalCache();
    linkFlows.P1 = Pset;
    linkFlows.P2 = Pset - (std::abs(Pset) * lossFraction);
    linkFlows.Q1 = fromReactivePower;
    linkFlows.Q2 = toReactivePower;
}

void RawDcLine::updateLocalCache(const IOdata& /*inputs*/,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode)
{
    if (!isEnabled() || !isConnected() || !stateDataValue.updateRequired(linkInfo.seqID)) {
        return;
    }
    Link::updateLocalCache(noInputs, stateDataValue, sMode);
    const auto fromOffset = fromReactiveOffset(sMode);
    const auto toOffset = toReactiveOffset(sMode);
    if (fromOffset != kNullLocation) {
        fromReactivePower = stateDataValue.state[fromOffset];
    }
    if (toOffset != kNullLocation) {
        toReactivePower = stateDataValue.state[toOffset];
    }
    linkFlows.P1 = Pset;
    linkFlows.P2 = Pset - (std::abs(Pset) * lossFraction);
    linkFlows.Q1 = fromReactivePower;
    linkFlows.Q2 = toReactivePower;
}

void RawDcLine::outputPartialDerivatives(id_type_t busId,
                                         const StateData& stateDataValue,
                                         MatrixData<double>& matrixDataValue,
                                         const SolverMode& sMode)
{
    if (!isEnabled() || !isConnected()) {
        return;
    }
    updateLocalCache(noInputs, stateDataValue, sMode);
    if ((busId == B1->getID()) && controlFromVoltage) {
        matrixDataValue.assign(QOUT_LOCATION, fromReactiveOffset(sMode), 1.0);
    } else if ((busId == B2->getID()) && controlToVoltage) {
        matrixDataValue.assign(QOUT_LOCATION, toReactiveOffset(sMode), 1.0);
    }
}

count_t RawDcLine::outputDependencyCount(index_t num, const SolverMode& sMode) const
{
    if (!hasAlgebraic(sMode) || (num != QOUT_LOCATION) || !isConnected()) {
        return 0;
    }
    return static_cast<count_t>(controlFromVoltage) + static_cast<count_t>(controlToVoltage);
}

void RawDcLine::jacobianElements(const IOdata& /*inputs*/,
                                 const StateData& /*stateDataValue*/,
                                 MatrixData<double>& matrixDataValue,
                                 const IOlocs& /*inputLocs*/,
                                 const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode) || !isConnected()) {
        return;
    }
    if (controlFromVoltage) {
        matrixDataValue.assignCheckCol(fromReactiveOffset(sMode),
                                       B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION),
                                       -1.0);
    }
    if (controlToVoltage) {
        matrixDataValue.assignCheckCol(toReactiveOffset(sMode),
                                       B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION),
                                       -1.0);
    }
}

void RawDcLine::residual(const IOdata& inputs,
                         const StateData& stateDataValue,
                         double resid[],
                         const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    if (!hasAlgebraic(sMode) || !isConnected()) {
        return;
    }
    if (controlFromVoltage) {
        resid[fromReactiveOffset(sMode)] =
            fromVoltageTarget - B1->getVoltage(stateDataValue, sMode);
    }
    if (controlToVoltage) {
        resid[toReactiveOffset(sMode)] = toVoltageTarget - B2->getVoltage(stateDataValue, sMode);
    }
}

void RawDcLine::setState(CoreTime time,
                         const double state[],
                         const double /*dstateDt*/[],
                         const SolverMode& sMode)
{
    const auto fromOffset = fromReactiveOffset(sMode);
    const auto toOffset = toReactiveOffset(sMode);
    if (fromOffset != kNullLocation) {
        fromReactivePower = state[fromOffset];
    }
    if (toOffset != kNullLocation) {
        toReactivePower = state[toOffset];
    }
    prevTime = time;
    updateLocalCache();
}

void RawDcLine::guessState(CoreTime /*time*/,
                           double state[],
                           double /*dstateDt*/[],
                           const SolverMode& sMode)
{
    const auto fromOffset = fromReactiveOffset(sMode);
    const auto toOffset = toReactiveOffset(sMode);
    if (fromOffset != kNullLocation) {
        state[fromOffset] = fromReactivePower;
    }
    if (toOffset != kNullLocation) {
        state[toOffset] = toReactivePower;
    }
}

void RawDcLine::getStateName(stringVec& stNames,
                             const SolverMode& sMode,
                             const std::string& prefix) const
{
    const auto fromOffset = fromReactiveOffset(sMode);
    const auto toOffset = toReactiveOffset(sMode);
    const std::string statePrefix = prefix + getName() + ':';
    if (fromOffset != kNullLocation) {
        stNames[fromOffset] = statePrefix + "q_from";
    }
    if (toOffset != kNullLocation) {
        stNames[toOffset] = statePrefix + "q_to";
    }
}

}  // namespace griddyn::links
