/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "VoltageCompensatorIeeeVC.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>

namespace griddyn::voltagecompensators {
VoltageCompensatorIeeeVC::VoltageCompensatorIeeeVC(const std::string& objName):
    VoltageCompensator(objName)
{
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 0;
    offsets.local().local.jacSize = 8;
}

CoreObject* VoltageCompensatorIeeeVC::clone(CoreObject* obj) const
{
    auto* compensatorClone = cloneBase<VoltageCompensatorIeeeVC, VoltageCompensator>(this, obj);
    if (compensatorClone != nullptr) {
        compensatorClone->RC = RC;
        compensatorClone->XC = XC;
    }
    return (compensatorClone != nullptr) ? compensatorClone : obj;
}

void VoltageCompensatorIeeeVC::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 0;
    offsets.local().local.jacSize = 8;
    prevTime = time0;
}

void VoltageCompensatorIeeeVC::dynObjectInitializeB(const IOdata& inputs,
                                                    const IOdata& /*desiredOutput*/,
                                                    IOdata& fieldSet)
{
    if (inputs.size() < voltageCompensatorInputCount) {
        throw InvalidParameterValue("IEEEVC input vector");
    }
    m_state[0] = compensatedVoltage(inputs);
    fieldSet.resize(1);
    fieldSet[0] = m_state[0];
}

void VoltageCompensatorIeeeVC::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}

void VoltageCompensatorIeeeVC::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "rc") {
        RC = val;
    } else if (param == "xc") {
        XC = val;
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

double VoltageCompensatorIeeeVC::get(std::string_view param, units::unit unitType) const
{
    if (param == "rc") {
        return RC;
    }
    if (param == "xc") {
        return XC;
    }
    return GridSubModel::get(param, unitType);
}

double VoltageCompensatorIeeeVC::compensatedVoltage(const IOdata& inputs) const
{
    const double directCurrent = inputs[voltageCompensatorIdInLocation];
    const double quadratureCurrent = inputs[voltageCompensatorIqInLocation];
    const double directVoltage = inputs[voltageCompensatorVdInLocation];
    const double quadratureVoltage = inputs[voltageCompensatorVqInLocation];
    const double realPart = directVoltage + (RC * directCurrent) - (XC * quadratureCurrent);
    const double imaginaryPart = quadratureVoltage + (RC * quadratureCurrent) + (XC * directCurrent);
    return std::hypot(realPart, imaginaryPart);
}

void VoltageCompensatorIeeeVC::residual(const IOdata& inputs,
                                        const StateData& stateData,
                                        double resid[],
                                        const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    locations.destLoc[0] = locations.algStateLoc[0] - compensatedVoltage(inputs);
}

void VoltageCompensatorIeeeVC::algebraicUpdate(const IOdata& inputs,
                                               const StateData& stateData,
                                               double update[],
                                               const SolverMode& sMode,
                                               double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    locations.destLoc[0] = compensatedVoltage(inputs);
}

void VoltageCompensatorIeeeVC::jacobianElements(const IOdata& inputs,
                                                const StateData& stateData,
                                                MatrixData<double>& matrixData,
                                                const IOlocs& inputLocs,
                                                const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double directCurrent = inputs[voltageCompensatorIdInLocation];
    const double quadratureCurrent = inputs[voltageCompensatorIqInLocation];
    const double directVoltage = inputs[voltageCompensatorVdInLocation];
    const double quadratureVoltage = inputs[voltageCompensatorVqInLocation];
    const double realPart = directVoltage + (RC * directCurrent) - (XC * quadratureCurrent);
    const double imaginaryPart = quadratureVoltage + (RC * quadratureCurrent) + (XC * directCurrent);
    const double magnitude = std::hypot(realPart, imaginaryPart);
    const double inverseMagnitude = (magnitude > 1e-12) ? 1.0 / magnitude : 0.0;
    const auto row = locations.algOffset;
    matrixData.assign(row, row, 1.0);
    matrixData.assignCheckCol(row,
                              inputLocs[voltageCompensatorIdInLocation],
                              -inverseMagnitude * ((realPart * RC) + (imaginaryPart * XC)));
    matrixData.assignCheckCol(row,
                              inputLocs[voltageCompensatorIqInLocation],
                              -inverseMagnitude * ((-realPart * XC) + (imaginaryPart * RC)));
    matrixData.assignCheckCol(row,
                              inputLocs[voltageCompensatorVdInLocation],
                              -realPart * inverseMagnitude);
    matrixData.assignCheckCol(row,
                              inputLocs[voltageCompensatorVqInLocation],
                              -imaginaryPart * inverseMagnitude);
}

index_t VoltageCompensatorIeeeVC::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "vct") || (field == "voltage_compensated") || (field == "output")) {
        return getOutputLoc(sMode, 0);
    }
    return kInvalidLocation;
}
}  // namespace griddyn::voltagecompensators
