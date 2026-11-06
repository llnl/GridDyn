/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ElectronicLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::loads {
static ChildTypeFactory<ElectronicLoad, GridLoad> gElectronicLoadFactory(
    "load",
    std::to_array<std::string_view>({"electronic", "powerelectronic", "cmpldwelectronic"}));

const ModelDescription& ElectronicLoad::getModelDescription() const
{
    static const auto description = ModelDescription::define<ElectronicLoad>(
        "ElectronicLoad",
        &GridLoad::getModelDescription(),
        {parameter({"vd1"}, &ElectronicLoad::voltageTripStart, units::one, "Voltage trip start"),
         parameter(
             {"vd2"}, &ElectronicLoad::voltageTripComplete, units::one, "Voltage trip complete"),
         parameter({"frcel", "frel"},
                   &ElectronicLoad::recoveryFraction,
                   units::one,
                   "Recovery fraction",
                   parameterValidators::unitInterval),
         parameter({"a1"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   0,
                   units::one,
                   "Load coefficient 1"),
         parameter({"a2"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   1,
                   units::one,
                   "Load coefficient 2"),
         parameter({"a3"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   2,
                   units::one,
                   "Load coefficient 3"),
         parameter({"a4"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   3,
                   units::one,
                   "Load coefficient 4"),
         parameter({"a5"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   4,
                   units::one,
                   "Load coefficient 5"),
         parameter({"a6"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   5,
                   units::one,
                   "Load coefficient 6"),
         parameter({"a7", "pfrq"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   6,
                   units::one,
                   "Load coefficient 7"),
         parameter({"a8", "qfrq"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::coefficients,
                   7,
                   units::one,
                   "Load coefficient 8"),
         parameter({"n1"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::exponents,
                   0,
                   units::one,
                   "Voltage exponent 1"),
         parameter({"n2"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::exponents,
                   1,
                   units::one,
                   "Voltage exponent 2"),
         parameter({"n3"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::exponents,
                   2,
                   units::one,
                   "Voltage exponent 3"),
         parameter({"n4"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::exponents,
                   3,
                   units::one,
                   "Voltage exponent 4"),
         parameter({"n5"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::exponents,
                   4,
                   units::one,
                   "Voltage exponent 5"),
         parameter({"n6"},
                   &ElectronicLoad::parameters,
                   &IEELParameters::exponents,
                   5,
                   units::one,
                   "Voltage exponent 6"),
         command({"pfel"}, units::defunit, "Set pfel")});
    return description;
}

ElectronicLoad::ElectronicLoad(const std::string& objName): GridLoad(objName)
{
    parameters.coefficients = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    parameters.exponents.fill(0.0);
}

CoreObject* ElectronicLoad::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<ElectronicLoad, GridLoad>(this, obj);
    if (copy != nullptr) {
        copy->parameters = parameters;
        copy->voltageTripStart = voltageTripStart;
        copy->voltageTripComplete = voltageTripComplete;
        copy->recoveryFraction = recoveryFraction;
        copy->powerFactor = powerFactor;
        copy->minimumVoltage = minimumVoltage;
        copy->usePowerFactor = usePowerFactor;
    }
    return (copy == nullptr) ? obj : copy;
}

void ElectronicLoad::setIEELParameters(const IEELParameters& newParameters)
{
    if (!std::all_of(newParameters.coefficients.begin(),
                     newParameters.coefficients.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !std::all_of(newParameters.exponents.begin(),
                     newParameters.exponents.end(),
                     [](double value) { return std::isfinite(value); })) {
        throw InvalidParameterValue("electronic load curve parameters must be finite");
    }
    parameters = newParameters;
    const bool frequencyDependent =
        (parameters.coefficients[6] != 0.0) || (parameters.coefficients[7] != 0.0);
    opFlags.set(USES_BUS_FREQUENCY, frequencyDependent);
}

void ElectronicLoad::validateTripParameters() const
{
    if (!std::isfinite(voltageTripStart) || !std::isfinite(voltageTripComplete) ||
        (voltageTripComplete < 0.0) || (voltageTripStart <= voltageTripComplete)) {
        throw InvalidParameterValue("electronic load requires Vd1 > Vd2 >= 0");
    }
    if (!std::isfinite(recoveryFraction) || (recoveryFraction < 0.0) || (recoveryFraction > 1.0)) {
        throw InvalidParameterValue("electronic load Frcel must be in [0, 1]");
    }
}

void ElectronicLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateTripParameters();
    GridLoad::pFlowObjectInitializeA(time0, flags);
    minimumVoltage = std::max(voltageTripComplete, (bus == nullptr) ? 1.0 : bus->getVoltage());
}

void ElectronicLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateTripParameters();
    GridLoad::dynObjectInitializeA(time0, flags);
    minimumVoltage = std::max(voltageTripComplete, (bus == nullptr) ? 1.0 : bus->getVoltage());
}

void ElectronicLoad::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    GridLoad::timestep(time, inputs, sMode);
    if (!isConnected()) {
        return;
    }
    double voltage = inputs.empty() ? 1.0 : inputs[VOLTAGE_IN_LOCATION];
    if (inputs.empty() && bus != nullptr) {
        voltage = bus->getVoltage();
    }
    if (std::isfinite(voltage)) {
        minimumVoltage = std::max(voltageTripComplete, std::min(minimumVoltage, voltage));
    }
}

void ElectronicLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ElectronicLoad::getModelDescription();
    getParamString<ElectronicLoad, GridLoad>(this,
                                             pstr,
                                             description.numericNames(),
                                             description.stringNames(),
                                             description.flagNames(),
                                             pstype);
}

void ElectronicLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}

void ElectronicLoad::set(std::string_view param, double val, units::unit unitType)
{
    if (ElectronicLoad::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        if (!param.empty() && param.front() != '#') {
            setIEELParameters(parameters);
        }
        return;
    }
    if (param == "pfel") {
        if ((val < -1.0) || (val > 1.0)) {
            throw InvalidParameterValue("electronic load PFel must be in [-1, 1]");
        }
        powerFactor = val;
        usePowerFactor = (val != 0.0);
        return;
    }

    GridLoad::set(param, val, unitType);
}

double ElectronicLoad::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            ElectronicLoad::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    if (param == "pfel") {
        return usePowerFactor ? powerFactor : 0.0;
    }

    if (param == "vmin") {
        return minimumVoltage;
    }

    return GridLoad::get(param, unitType);
}

double ElectronicLoad::voltageFactor(bool reactive, double voltage) const
{
    const auto firstTerm = reactive ? 3U : 0U;
    double factor = 0.0;
    for (std::size_t index = 0; index < 3U; ++index) {
        const double coefficient = parameters.coefficients[firstTerm + index];
        if (coefficient != 0.0) {
            factor += coefficient * std::pow(voltage, parameters.exponents[firstTerm + index]);
        }
    }
    return factor;
}

double ElectronicLoad::voltageDerivative(bool reactive, double voltage) const
{
    const auto firstTerm = reactive ? 3U : 0U;
    double derivative = 0.0;
    for (std::size_t index = 0; index < 3U; ++index) {
        const double coefficient = parameters.coefficients[firstTerm + index];
        const double exponent = parameters.exponents[firstTerm + index];
        if ((coefficient != 0.0) && (exponent != 0.0)) {
            derivative += coefficient * exponent * std::pow(voltage, exponent - 1.0);
        }
    }
    return derivative;
}

double ElectronicLoad::frequencyCoefficient(bool reactive) const
{
    return parameters.coefficients[reactive ? 7U : 6U];
}

double ElectronicLoad::tripFactor(double voltage) const
{
    if (voltage < voltageTripComplete) {
        return 0.0;
    }
    const double denominator = voltageTripStart - voltageTripComplete;
    if (voltage < voltageTripStart) {
        if (voltage <= minimumVoltage) {
            return (voltage - voltageTripComplete) / denominator;
        }
        return ((minimumVoltage - voltageTripComplete) +
                (recoveryFraction * (voltage - minimumVoltage))) /
            denominator;
    }
    if (minimumVoltage >= voltageTripStart) {
        return 1.0;
    }
    return ((minimumVoltage - voltageTripComplete) +
            (recoveryFraction * (voltageTripStart - minimumVoltage))) /
        denominator;
}

double ElectronicLoad::tripDerivative(double voltage) const
{
    if ((voltage <= voltageTripComplete) || (voltage >= voltageTripStart)) {
        return 0.0;
    }
    const double slope = 1.0 / (voltageTripStart - voltageTripComplete);
    return (voltage <= minimumVoltage) ? slope : recoveryFraction * slope;
}

double ElectronicLoad::reactiveBasePower() const
{
    return usePowerFactor ? getP() * std::tan(std::acos(powerFactor)) : getQ();
}

double ElectronicLoad::frequency(const IOdata& inputs,
                                 const StateData& stateData,
                                 const SolverMode& sMode) const
{
    if (inputs.size() > FREQUENCY_IN_LOCATION) {
        return inputs[FREQUENCY_IN_LOCATION];
    }
    return (bus == nullptr) ? 1.0 : bus->getFreq(stateData, sMode);
}

void ElectronicLoad::ioPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateData,
                                          MatrixData<double>& matrixData,
                                          const IOlocs& inputLocs,
                                          const SolverMode& sMode)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double freq = frequency(inputs, stateData, sMode);
    const double trip = tripFactor(voltage);
    const double tripD = tripDerivative(voltage);
    const double pShape = voltageFactor(false, voltage);
    const double qShape = voltageFactor(true, voltage);
    const double pFreq = 1.0 + (frequencyCoefficient(false) * (freq - 1.0));
    const double qFreq = 1.0 + (frequencyCoefficient(true) * (freq - 1.0));
    const double qBase = reactiveBasePower();

    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getP() * pFreq *
                              ((voltageDerivative(false, voltage) * trip) + (pShape * tripD)));
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          qBase * qFreq *
                              ((voltageDerivative(true, voltage) * trip) + (qShape * tripD)));
    }
    if (inputLocs[FREQUENCY_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          getP() * pShape * trip * frequencyCoefficient(false));
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          qBase * qShape * trip * frequencyCoefficient(true));
    }
}

double ElectronicLoad::getRealPower(const IOdata& inputs,
                                    const StateData& stateData,
                                    const SolverMode& sMode) const
{
    double voltage = inputs.empty() ? 1.0 : inputs[VOLTAGE_IN_LOCATION];
    if (inputs.empty() && bus != nullptr) {
        voltage = bus->getVoltage(stateData, sMode);
    }
    const double freq = frequency(inputs, stateData, sMode);
    return isConnected() ? getP() * voltageFactor(false, voltage) * tripFactor(voltage) *
            (1.0 + (frequencyCoefficient(false) * (freq - 1.0))) :
                           0.0;
}

double ElectronicLoad::getReactivePower(const IOdata& inputs,
                                        const StateData& stateData,
                                        const SolverMode& sMode) const
{
    double voltage = inputs.empty() ? 1.0 : inputs[VOLTAGE_IN_LOCATION];
    if (inputs.empty() && bus != nullptr) {
        voltage = bus->getVoltage(stateData, sMode);
    }
    const double freq = frequency(inputs, stateData, sMode);
    return isConnected() ? reactiveBasePower() * voltageFactor(true, voltage) *
            tripFactor(voltage) * (1.0 + (frequencyCoefficient(true) * (freq - 1.0))) :
                           0.0;
}

double ElectronicLoad::getRealPower(double voltage) const
{
    const double freq = (bus == nullptr) ? 1.0 : bus->getFreq();
    return isConnected() ? getP() * voltageFactor(false, voltage) * tripFactor(voltage) *
            (1.0 + (frequencyCoefficient(false) * (freq - 1.0))) :
                           0.0;
}

double ElectronicLoad::getReactivePower(double voltage) const
{
    const double freq = (bus == nullptr) ? 1.0 : bus->getFreq();
    return isConnected() ? reactiveBasePower() * voltageFactor(true, voltage) *
            tripFactor(voltage) * (1.0 + (frequencyCoefficient(true) * (freq - 1.0))) :
                           0.0;
}

double ElectronicLoad::getRealPower() const
{
    return (bus == nullptr) ? GridLoad::getRealPower() : getRealPower(bus->getVoltage());
}

double ElectronicLoad::getReactivePower() const
{
    return (bus == nullptr) ? GridLoad::getReactivePower() : getReactivePower(bus->getVoltage());
}
}  // namespace griddyn::loads
