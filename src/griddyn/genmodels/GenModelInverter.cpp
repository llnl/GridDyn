/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelInverter.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace griddyn::genmodels {
GenModelInverter::GenModelInverter(const std::string& objName): GenModel(objName) {}
CoreObject* GenModelInverter::clone(CoreObject* obj) const
{
    auto* genModelClone = cloneBase<GenModelInverter, GenModel>(this, obj);
    if (genModelClone == nullptr) {
        return obj;
    }

    genModelClone->minAngle = minAngle;
    genModelClone->maxAngle = maxAngle;

    return genModelClone;
}

void GenModelInverter::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.algSize = 1;
    offsets.local().local.jacSize = 4;
    offsets.local().local.algRoots = 1;
}
// initial conditions
void GenModelInverter::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    double* genModelState = m_state.data();
    const double voltage = inputs[voltageInLocation];
    const std::complex<double> outCurrent(desiredOutput[PoutLocation] / voltage,
                                          -desiredOutput[QoutLocation] / voltage);
    const auto impedance = std::complex<double>(Rs, Xd);
    const auto internalVoltage = impedance * outCurrent + voltage;

    genModelState[0] = std::arg(internalVoltage);

    fieldSet[genModelEftInLocation] = std::abs(internalVoltage) - 1.0;

    double loss = 0;
    if (Rs != 0) {
        const double cosA = cos(genModelState[0]);
        const double voltageLoss1 = voltage * voltage * g;
        const double voltageLoss2 = 2.0 * voltage * g * std::abs(internalVoltage) * cosA;
        const double voltageLoss3 = std::abs(internalVoltage) * std::abs(internalVoltage) * g;
        loss = voltageLoss1 + voltageLoss2 + voltageLoss3;
    }

    fieldSet[genModelPmechInLocation] = desiredOutput[PoutLocation] + loss;  // Pmt

    bus = static_cast<GridBus*>(find("bus"));
}

void GenModelInverter::algebraicUpdate(const IOdata& inputs,
                                       const stateData& /*stateDataValue*/,
                                       double update[],
                                       const solverMode& sMode,
                                       double /*alpha*/)
{
    auto offset = offsets.getAlgOffset(sMode);

    // double angle = std::atan2(g, b);

    const double mechanicalPower = inputs[genModelPmechInLocation];
    if (opFlags[at_angle_limits]) {
        if (mechanicalPower > 0) {
            update[offset] = maxAngle;
        } else {
            update[offset] = minAngle;
        }
    } else {
        // Get the exciter field
        const double voltage = inputs[voltageInLocation];
        const double exciterField = inputs[genModelEftInLocation] + 1.0;
        if (Rs != 0.0) {
            const double impedanceMagnitude = std::hypot(2.0 * g, b);
            const double gamma = std::atan(b / (2.0 * g)) - (kPI / 2);
            const double voltageLoss1 = voltage * voltage * g;
            const double voltageLoss3 = exciterField * exciterField * g;
            const double powerRatio = (mechanicalPower - voltageLoss1 - voltageLoss3) /
                (exciterField * voltage * impedanceMagnitude);
            if (std::abs(powerRatio) >= 1.0) {
                update[offset] = (powerRatio >= 1.0) ? kPI / 2.0 : -kPI / 2.0;
            } else {
                update[offset] = std::asin(powerRatio) + gamma;
            }
        } else {
            const double powerRatio = mechanicalPower / (exciterField * voltage * b);
            if (std::abs(powerRatio) >= 1.0) {
                update[offset] = (powerRatio >= 1.0) ? kPI / 2.0 : -kPI / 2.0;
            } else {
                update[offset] = std::asin(powerRatio);
            }
        }
    }
}

// residual

void GenModelInverter::residual(const IOdata& inputs,
                                const stateData& stateDataValue,
                                double resid[],
                                const solverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(stateDataValue, resid, sMode, this);

    const double angle = *Loc.algStateLoc;
    // printf("time=%f, angle=%f\n", sD.time, angle);
    const double mechanicalPower = inputs[genModelPmechInLocation];
    if (opFlags[at_angle_limits]) {
        if (mechanicalPower > 0) {
            Loc.destLoc[0] = maxAngle - angle;
        } else {
            Loc.destLoc[0] = minAngle - angle;
        }
    } else {
        // Get the exciter field
        const double exciterField = inputs[genModelEftInLocation] + 1.0;

        const double voltage = inputs[voltageInLocation];

        const double powerNoResistance = exciterField * voltage * b * sin(angle);
        if (Rs != 0.0) {
            const double cosA = cos(angle);
            const double voltageLoss1 = voltage * voltage * g;
            const double voltageLoss2 = 2.0 * voltage * g * exciterField * cosA;
            const double voltageLoss3 = exciterField * exciterField * g;
            const double loss = voltageLoss1 + voltageLoss2 + voltageLoss3;
            Loc.destLoc[0] = mechanicalPower - powerNoResistance - loss;
        } else {
            Loc.destLoc[0] = mechanicalPower - powerNoResistance;
        }
    }
}

double GenModelInverter::getFreq(const stateData& stateDataValue,
                                 const solverMode& sMode,
                                 index_t* freqOffset) const
{
    // there is no inertia in this gen model so it can't compute a frequency and
    // must use the bus frequency
    if (freqOffset != nullptr) {
        *freqOffset = bus->getOutputLoc(sMode, frequencyInLocation);
    }
    return bus->getFreq(stateDataValue, sMode);
}

double GenModelInverter::getAngle(const stateData& stateDataValue,
                                  const solverMode& sMode,
                                  index_t* angleOffset) const
{
    auto offset = offsets.getAlgOffset(sMode);
    if (angleOffset != nullptr) {
        *angleOffset = offset;
    }

    return (!stateDataValue.empty()) ? stateDataValue.state[offset] : m_state[0];
}

IOdata GenModelInverter::getOutputs(const IOdata& inputs,
                                    const stateData& stateDataValue,
                                    const solverMode& sMode) const
{
    auto Loc = offsets.getLocations(stateDataValue, sMode, this);

    IOdata out(2);
    const double voltage = inputs[voltageInLocation];
    const double exciterField = inputs[genModelEftInLocation] + 1.0;
    const double cosineAngle = cos(Loc.algStateLoc[0]);
    const double sineAngle = sin(Loc.algStateLoc[0]);

    out[PoutLocation] = realPowerCompute(voltage, exciterField, cosineAngle, sineAngle);
    out[QoutLocation] = reactivePowerCompute(voltage, exciterField, cosineAngle, sineAngle);

    return out;
}

double GenModelInverter::realPowerCompute(double voltage,
                                          double exciterField,
                                          double cosA,
                                          double sinA) const
{
    return (voltage * voltage * g) - (voltage * g * exciterField * cosA) -
        (voltage * exciterField * b * sinA);
}

double GenModelInverter::reactivePowerCompute(double voltage,
                                              double exciterField,
                                              double cosA,
                                              double sinA) const
{
    return (voltage * voltage * b) - (voltage * exciterField * b * cosA) +
        (voltage * exciterField * g * sinA);
}

double GenModelInverter::getOutput(const IOdata& inputs,
                                   const stateData& stateDataValue,
                                   const solverMode& sMode,
                                   index_t outNum) const
{
    auto Loc = offsets.getLocations(stateDataValue, sMode, this);
    const double cosineAngle = cos(Loc.algStateLoc[0]);
    const double sineAngle = sin(Loc.algStateLoc[0]);
    const double voltage = inputs[voltageInLocation];
    const double exciterField = inputs[genModelEftInLocation] + 1.0;

    if (outNum == PoutLocation) {
        return realPowerCompute(voltage, exciterField, cosineAngle, sineAngle);
    }
    if (outNum == QoutLocation) {
        return reactivePowerCompute(voltage, exciterField, cosineAngle, sineAngle);
    }
    return kNullVal;
}

void GenModelInverter::ioPartialDerivatives(const IOdata& inputs,
                                            const stateData& stateDataValue,
                                            matrixData<double>& matrixDataValue,
                                            const IOlocs& inputLocs,
                                            const solverMode& sMode)
{
    auto Loc = offsets.getLocations(stateDataValue, sMode, this);

    const double voltage = inputs[voltageInLocation];

    const double cosineAngle = cos(Loc.algStateLoc[0]);
    const double sineAngle = sin(Loc.algStateLoc[0]);

    // out[PoutLocation] = V*V*g - V*g*Eft*cAng - V*Eft*b*sAng;
    // out[QoutLocation] = V*V*b - V*Eft*b*cAng + V*Eft*g*sAng;

    if (inputLocs[genModelEftInLocation] != kNullLocation) {
        matrixDataValue.assign(PoutLocation,
                               inputLocs[genModelEftInLocation],
                               (-voltage * g * cosineAngle) - (voltage * b * sineAngle));
        matrixDataValue.assign(QoutLocation,
                               inputLocs[genModelEftInLocation],
                               (-voltage * b * cosineAngle) + (voltage * g * sineAngle));
    }

    if (inputLocs[voltageInLocation] != kNullLocation) {
        const double exciterField = inputs[genModelEftInLocation] + 1.0;
        matrixDataValue.assign(PoutLocation,
                               inputLocs[voltageInLocation],
                               (2.0 * voltage * g) - (g * exciterField * cosineAngle) -
                                   (exciterField * b * sineAngle));
        matrixDataValue.assign(QoutLocation,
                               inputLocs[voltageInLocation],
                               (2.0 * voltage * b) - (exciterField * b * cosineAngle) +
                                   (voltage * exciterField * g * sineAngle));
    }
}

void GenModelInverter::jacobianElements(const IOdata& inputs,
                                        const stateData& stateDataValue,
                                        matrixData<double>& matrixDataValue,
                                        const IOlocs& inputLocs,
                                        const solverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(stateDataValue, sMode, this);
    auto offset = Loc.algOffset;
    if (opFlags[at_angle_limits]) {
        matrixDataValue.assign(offset, offset, -1.0);
    } else {
        const double voltage = inputs[voltageInLocation];
        const double exciterField = inputs[genModelEftInLocation] + 1.0;
        const double cosineAngle = cos(Loc.algStateLoc[0]);
        const double sineAngle = sin(Loc.algStateLoc[0]);

        // rva[0] = Pmt -V*V*g - Eft*Eft*g - 2.0*V * Eft*g*cos(gm[0]) - V *
        // Eft*b*sin(gm[0]);

        matrixDataValue.assign(offset,
                               offset,
                               (2.0 * voltage * exciterField * g * sineAngle) -
                                   (voltage * exciterField * b * cosineAngle));

        matrixDataValue.assignCheckCol(offset, inputLocs[genModelPmechInLocation], 1.0);
        matrixDataValue.assignCheckCol(offset,
                                       inputLocs[genModelEftInLocation],
                                       (-2.0 * exciterField * g) -
                                           (2.0 * voltage * g * cosineAngle) -
                                           (voltage * b * sineAngle));
        matrixDataValue.assignCheckCol(offset,
                                       inputLocs[voltageInLocation],
                                       (-2.0 * voltage * g) -
                                           (2.0 * exciterField * g * cosineAngle) -
                                           (exciterField * b * sineAngle));
    }
}

void GenModelInverter::outputPartialDerivatives(const IOdata& inputs,
                                                const stateData& stateDataValue,
                                                matrixData<double>& matrixDataValue,
                                                const solverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(stateDataValue, sMode, this);

    const double voltage = inputs[voltageInLocation];
    const double exciterField = inputs[genModelEftInLocation] + 1.0;
    const double cosineAngle = cos(Loc.algStateLoc[0]);
    const double sineAngle = sin(Loc.algStateLoc[0]);

    // out[PoutLocation] = V*V*g - V*g*Eft*cAng - V*Eft*b*sAng;
    // out[QoutLocation] = V*V*b - V*Eft*b*cAng + V*Eft*g*sAng;

    matrixDataValue.assign(PoutLocation,
                           Loc.algOffset,
                           (voltage * g * exciterField * sineAngle) -
                               (voltage * exciterField * b * cosineAngle));
    matrixDataValue.assign(QoutLocation,
                           Loc.algOffset,
                           (voltage * exciterField * b * sineAngle) +
                               (voltage * exciterField * g * cosineAngle));
}

count_t GenModelInverter::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 1;
}
stringVec GenModelInverter::localStateNames() const
{
    static const stringVec genModelNames{"angle"};
    return genModelNames;
}
// set parameters
void GenModelInverter::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}

void GenModelInverter::set(std::string_view param, double val, units::unit unitType)
{
    if (param.length() == 1) {
        switch (param[0]) {
            case 'x':
                Xd = val;
                reCalcImpedences();
                break;
            case 'r':
                Rs = val;
                reCalcImpedences();
                break;
            default:
                throw(unrecognizedParameter(param));
        }

        return;
    }

    if ((param == "xd") || (param == "xs")) {
        Xd = val;
        reCalcImpedences();
    } else if (param == "maxangle") {
        maxAngle = units::convert(val, unitType, units::rad);
    } else if (param == "minangle") {
        minAngle = units::convert(val, unitType, units::rad);
    } else if (param == "rs") {
        Rs = val;
        reCalcImpedences();
    } else {
        GenModel::set(param, val, unitType);
    }
}

void GenModelInverter::reCalcImpedences()
{
    const double admittanceMagnitude = 1.0 / ((Rs * Rs) + (Xd * Xd));
    b = Xd * admittanceMagnitude;
    g = Rs * admittanceMagnitude;
}

void GenModelInverter::rootTest(const IOdata& inputs,
                                const stateData& stateDataValue,
                                double roots[],
                                const solverMode& sMode)
{
    if (rootSize(sMode) > 0) {
        auto rootOffset = offsets.getRootOffset(sMode);
        auto stateOffset = offsets.getAlgOffset(sMode);
        const double angle = stateDataValue.state[stateOffset];
        if (opFlags[at_angle_limits]) {
            if (inputs[genModelPmechInLocation] > 0) {
                const double pmax = -realPowerCompute(inputs[genModelEftInLocation],
                                                      inputs[voltageInLocation],
                                                      cos(maxAngle),
                                                      sin(maxAngle));
                roots[rootOffset] = inputs[genModelPmechInLocation] + 0.0001 - pmax;
            } else {
                const double pmin = -realPowerCompute(inputs[genModelEftInLocation],
                                                      inputs[voltageInLocation],
                                                      cos(minAngle),
                                                      sin(minAngle));
                roots[rootOffset] = pmin - inputs[genModelPmechInLocation] + 0.0001;
            }
        } else {
            roots[rootOffset] = std::min(angle - minAngle, maxAngle - angle);
        }
    }
}

void GenModelInverter::rootTrigger(coreTime /*time*/,
                                   const IOdata& inputs,
                                   const std::vector<int>& rootMask,
                                   const solverMode& sMode)
{
    if (rootSize(sMode) > 0) {
        auto rootOffset = offsets.getRootOffset(sMode);
        if (rootMask[rootOffset] > 0) {
            if (opFlags[at_angle_limits]) {
                opFlags.reset(at_angle_limits);
                logging::debug(this, "reset angle limit");
                algebraicUpdate(inputs, emptyStateData, m_state.data(), sMode, 1.0);
            } else {
                opFlags.set(at_angle_limits);
                logging::debug(this, "angle at limits");
                if (inputs[genModelPmechInLocation] > 0) {
                    m_state[0] = maxAngle;
                } else {
                    m_state[0] = minAngle;
                }
            }
        }
    }
}

change_code GenModelInverter::rootCheck(const IOdata& inputs,
                                        const stateData& stateDataValue,
                                        const solverMode& sMode,
                                        check_level_t /*level*/)
{
    if (rootSize(sMode) > 0) {
        auto Loc = offsets.getLocations(stateDataValue, sMode, this);
        const double angle = Loc.algStateLoc[0];
        if (opFlags[at_angle_limits]) {
            if (inputs[genModelPmechInLocation] > 0) {
                const double pmax = -realPowerCompute(inputs[genModelEftInLocation],
                                                      inputs[voltageInLocation],
                                                      cos(maxAngle),
                                                      sin(maxAngle));
                if (inputs[genModelPmechInLocation] - pmax < -0.0001) {
                    opFlags.reset(at_angle_limits);
                    logging::debug(this, "reset angle limit-from root check");
                    algebraicUpdate(inputs, emptyStateData, m_state.data(), sMode, 1.0);
                    return change_code::jacobian_change;
                }
            } else {
                const double pmin = -realPowerCompute(inputs[genModelEftInLocation],
                                                      inputs[voltageInLocation],
                                                      cos(minAngle),
                                                      sin(minAngle));
                if (pmin - inputs[genModelPmechInLocation] < -0.0001) {
                    opFlags.reset(at_angle_limits);
                    logging::debug(this, "reset angle limit- from root check");
                    algebraicUpdate(inputs, emptyStateData, m_state.data(), sMode, 1.0);
                    return change_code::jacobian_change;
                }
            }
        } else {
            auto remAngle = std::min(angle - minAngle, maxAngle - angle);
            if (remAngle < 0.0000001) {
                opFlags.set(at_angle_limits);
                logging::debug(this, "angle at limit from check");
                if (inputs[genModelPmechInLocation] > 0) {
                    m_state[0] = maxAngle;
                } else {
                    m_state[0] = minAngle;
                }
                return change_code::jacobian_change;
            }
        }
    }
    return change_code::no_change;
}

}  // namespace griddyn::genmodels
