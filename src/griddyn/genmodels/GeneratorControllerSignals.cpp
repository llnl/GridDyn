// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2014-2026, Lawrence Livermore National Security
// See the top-level NOTICE for additional details. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "../GridPrimary.h"
#include "GenModel3.h"
#include "GenModel4.h"
#include "GenModel5type3.h"
#include <cmath>

namespace griddyn::genmodels {

IOdata GenModelClassical::getMachineControllerSignals(const IOdata& inputs,
                                                      const StateData& stateDataValue,
                                                      const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double* algebraicState = locations.algStateLoc;
    const double angleDifference = locations.diffStateLoc[0] - inputs[ANGLE_IN_LOCATION];
    const double directVoltage = -inputs[VOLTAGE_IN_LOCATION] * std::sin(angleDifference);
    const double quadratureVoltage = inputs[VOLTAGE_IN_LOCATION] * std::cos(angleDifference);
    const double electricalPower =
        (directVoltage * algebraicState[0]) + (quadratureVoltage * algebraicState[1]);
    const double electricalTorque = electricalPower +
        (Rs * ((algebraicState[0] * algebraicState[0]) +
               (algebraicState[1] * algebraicState[1])));

    IOdata signals(machineControllerSignalCount, kNullVal);
    signals[static_cast<index_t>(MachineControllerSignal::ID)] = algebraicState[0];
    signals[static_cast<index_t>(MachineControllerSignal::IQ)] = algebraicState[1];
    signals[static_cast<index_t>(MachineControllerSignal::VD)] = directVoltage;
    signals[static_cast<index_t>(MachineControllerSignal::VQ)] = quadratureVoltage;
    signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)] = electricalPower;
    signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE)] = electricalTorque;
    // The classical model has no explicit field-winding state. Its excitation
    // voltage is the closest coupled per-unit proxy for field current.
    signals[static_cast<index_t>(MachineControllerSignal::XADIFD)] = inputs[genModelEftInLocation];
    return signals;
}

MachineSignalDerivativeData
    GenModelClassical::getMachineControllerSignalDerivatives(const IOdata& inputs,
                                                             const StateData& stateDataValue,
                                                             const IOlocs& inputLocs,
                                                             const SolverMode& sMode) const
{
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    const double* algebraicState = locations.algStateLoc;
    const index_t algebraicOffset = locations.algOffset;
    const index_t differentialOffset = locations.diffOffset;
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angleDifference = locations.diffStateLoc[0] - inputs[ANGLE_IN_LOCATION];
    const double sineAngle = std::sin(angleDifference);
    const double cosineAngle = std::cos(angleDifference);
    const double directVoltage = -voltage * sineAngle;
    const double quadratureVoltage = voltage * cosineAngle;

    MachineSignalDerivativeData derivatives;
    const auto addDerivative =
        [&derivatives](MachineControllerSignal signal, index_t location, double value) {
            if ((location != kNullLocation) && (location != kInvalidLocation) && (value != 0.0)) {
                derivatives[static_cast<index_t>(signal)].push_back(
                    {.location = location, .value = value});
            }
        };

    addDerivative(MachineControllerSignal::ID, algebraicOffset, 1.0);
    addDerivative(MachineControllerSignal::IQ, algebraicOffset + 1, 1.0);

    const index_t voltageLocation = inputLocs[VOLTAGE_IN_LOCATION];
    const index_t angleLocation = inputLocs[ANGLE_IN_LOCATION];
    addDerivative(MachineControllerSignal::VD, voltageLocation, -sineAngle);
    addDerivative(MachineControllerSignal::VD, angleLocation, quadratureVoltage);
    addDerivative(MachineControllerSignal::VD, differentialOffset, -quadratureVoltage);
    addDerivative(MachineControllerSignal::VQ, voltageLocation, cosineAngle);
    addDerivative(MachineControllerSignal::VQ, angleLocation, -directVoltage);
    addDerivative(MachineControllerSignal::VQ, differentialOffset, directVoltage);

    addDerivative(MachineControllerSignal::ELECTRICAL_POWER, algebraicOffset, directVoltage);
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  algebraicOffset + 1,
                  quadratureVoltage);
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  voltageLocation,
                  (-sineAngle * algebraicState[0]) + (cosineAngle * algebraicState[1]));
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  angleLocation,
                  (quadratureVoltage * algebraicState[0]) - (directVoltage * algebraicState[1]));
    addDerivative(MachineControllerSignal::ELECTRICAL_POWER,
                  differentialOffset,
                  (-quadratureVoltage * algebraicState[0]) + (directVoltage * algebraicState[1]));

    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  algebraicOffset,
                  directVoltage + (2.0 * Rs * algebraicState[0]));
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  algebraicOffset + 1,
                  quadratureVoltage + (2.0 * Rs * algebraicState[1]));
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  voltageLocation,
                  (-sineAngle * algebraicState[0]) + (cosineAngle * algebraicState[1]));
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  angleLocation,
                  (quadratureVoltage * algebraicState[0]) - (directVoltage * algebraicState[1]));
    addDerivative(MachineControllerSignal::ELECTRICAL_TORQUE,
                  differentialOffset,
                  (-quadratureVoltage * algebraicState[0]) + (directVoltage * algebraicState[1]));

    addDerivative(MachineControllerSignal::XADIFD, inputLocs[genModelEftInLocation], 1.0);
    return derivatives;
}

IOdata GenModel3::getMachineControllerSignals(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              const SolverMode& sMode) const
{
    auto signals = GenModelClassical::getMachineControllerSignals(inputs, stateDataValue, sMode);
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    signals[static_cast<index_t>(MachineControllerSignal::XADIFD)] =
        locations.diffStateLoc[2] - ((Xd - Xdp) * locations.algStateLoc[0]);
    return signals;
}

MachineSignalDerivativeData
    GenModel3::getMachineControllerSignalDerivatives(const IOdata& inputs,
                                                     const StateData& stateDataValue,
                                                     const IOlocs& inputLocs,
                                                     const SolverMode& sMode) const
{
    auto derivatives = GenModelClassical::getMachineControllerSignalDerivatives(inputs,
                                                                                stateDataValue,
                                                                                inputLocs,
                                                                                sMode);
    auto& fieldCurrentDerivatives =
        derivatives[static_cast<index_t>(MachineControllerSignal::XADIFD)];
    fieldCurrentDerivatives.clear();
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    fieldCurrentDerivatives.push_back({.location = locations.algOffset, .value = -(Xd - Xdp)});
    fieldCurrentDerivatives.push_back({.location = locations.diffOffset + 2, .value = 1.0});
    return derivatives;
}

IOdata GenModel4::getMachineControllerSignals(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              const SolverMode& sMode) const
{
    auto signals = GenModelClassical::getMachineControllerSignals(inputs, stateDataValue, sMode);
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    signals[static_cast<index_t>(MachineControllerSignal::XADIFD)] =
        locations.diffStateLoc[3] - ((Xd - Xdp) * locations.algStateLoc[0]);
    return signals;
}

MachineSignalDerivativeData
    GenModel4::getMachineControllerSignalDerivatives(const IOdata& inputs,
                                                     const StateData& stateDataValue,
                                                     const IOlocs& inputLocs,
                                                     const SolverMode& sMode) const
{
    auto derivatives = GenModelClassical::getMachineControllerSignalDerivatives(inputs,
                                                                                stateDataValue,
                                                                                inputLocs,
                                                                                sMode);
    auto& fieldCurrentDerivatives =
        derivatives[static_cast<index_t>(MachineControllerSignal::XADIFD)];
    fieldCurrentDerivatives.clear();
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    fieldCurrentDerivatives.push_back({.location = locations.algOffset, .value = -(Xd - Xdp)});
    fieldCurrentDerivatives.push_back({.location = locations.diffOffset + 3, .value = 1.0});
    return derivatives;
}

IOdata GenModel5type3::getMachineControllerSignals(const IOdata& inputs,
                                                   const StateData& stateDataValue,
                                                   const SolverMode& sMode) const
{
    auto signals = GenModelClassical::getMachineControllerSignals(inputs, stateDataValue, sMode);
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    signals[static_cast<index_t>(MachineControllerSignal::XADIFD)] = locations.diffStateLoc[2];
    return signals;
}

MachineSignalDerivativeData
    GenModel5type3::getMachineControllerSignalDerivatives(const IOdata& inputs,
                                                          const StateData& stateDataValue,
                                                          const IOlocs& inputLocs,
                                                          const SolverMode& sMode) const
{
    auto derivatives = GenModelClassical::getMachineControllerSignalDerivatives(inputs,
                                                                                stateDataValue,
                                                                                inputLocs,
                                                                                sMode);
    auto& fieldCurrentDerivatives =
        derivatives[static_cast<index_t>(MachineControllerSignal::XADIFD)];
    fieldCurrentDerivatives.clear();
    const auto locations = offsets.getLocations(stateDataValue, sMode, this);
    fieldCurrentDerivatives.push_back({.location = locations.diffOffset + 2, .value = 1.0});
    return derivatives;
}

}  // namespace griddyn::genmodels
