/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "IEELLoad.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <string>

namespace griddyn::loads {
static ChildTypeFactory<IEELLoad, GridLoad> gIEELLoadFactory("load", "ieel");

const ModelDescription& IEELLoad::getModelDescription() const
{
    static const auto description =
        ModelDescription::define<IEELLoad>("IEELLoad",
                                           &GridLoad::getModelDescription(),
                                           {parameter({"a1"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      0,
                                                      units::one,
                                                      "Load coefficient 1"),
                                            parameter({"a2"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      1,
                                                      units::one,
                                                      "Load coefficient 2"),
                                            parameter({"a3"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      2,
                                                      units::one,
                                                      "Load coefficient 3"),
                                            parameter({"a4"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      3,
                                                      units::one,
                                                      "Load coefficient 4"),
                                            parameter({"a5"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      4,
                                                      units::one,
                                                      "Load coefficient 5"),
                                            parameter({"a6"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      5,
                                                      units::one,
                                                      "Load coefficient 6"),
                                            parameter({"a7"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      6,
                                                      units::one,
                                                      "Load coefficient 7"),
                                            parameter({"a8"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::coefficients,
                                                      7,
                                                      units::one,
                                                      "Load coefficient 8"),
                                            parameter({"n1"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::exponents,
                                                      0,
                                                      units::one,
                                                      "Voltage exponent 1"),
                                            parameter({"n2"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::exponents,
                                                      1,
                                                      units::one,
                                                      "Voltage exponent 2"),
                                            parameter({"n3"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::exponents,
                                                      2,
                                                      units::one,
                                                      "Voltage exponent 3"),
                                            parameter({"n4"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::exponents,
                                                      3,
                                                      units::one,
                                                      "Voltage exponent 4"),
                                            parameter({"n5"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::exponents,
                                                      4,
                                                      units::one,
                                                      "Voltage exponent 5"),
                                            parameter({"n6"},
                                                      &IEELLoad::parameters,
                                                      &IEELParameters::exponents,
                                                      5,
                                                      units::one,
                                                      "Voltage exponent 6")});
    return description;
}

IEELLoad::IEELLoad(const std::string& objName): GridLoad(objName) {}

CoreObject* IEELLoad::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<IEELLoad, GridLoad>(this, obj);
    if (copy != nullptr) {
        copy->parameters = parameters;
    }
    return (copy == nullptr) ? obj : copy;
}

void IEELLoad::setIEELParameters(const IEELParameters& newParameters)
{
    parameters = newParameters;
    const bool frequencyDependent =
        (parameters.coefficients[6] != 0.0) || (parameters.coefficients[7] != 0.0);
    opFlags.set(USES_BUS_FREQUENCY, frequencyDependent);
}

double IEELLoad::voltageFactor(bool reactive, double voltage) const
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

double IEELLoad::voltageDerivative(bool reactive, double voltage) const
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

double IEELLoad::frequencyCoefficient(bool reactive) const
{
    return parameters.coefficients[reactive ? 7U : 6U];
}

void IEELLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}

void IEELLoad::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(IEELLoad::getModelDescription().trySet(*this, param, val, unitType))) {
        if (!param.empty() && param.front() != '#') {
            setIEELParameters(parameters);
        }
        return;
    }
    GridLoad::set(param, val, unitType);
}

void IEELLoad::ioPartialDerivatives(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    const double pFrequencyCoefficient = frequencyCoefficient(false);
    const double qFrequencyCoefficient = frequencyCoefficient(true);
    const double pFrequencyFactor = 1.0 + (pFrequencyCoefficient * (frequency - 1.0));
    const double qFrequencyFactor = 1.0 + (qFrequencyCoefficient * (frequency - 1.0));

    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getP() * voltageDerivative(false, voltage) * pFrequencyFactor);
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getQ() * voltageDerivative(true, voltage) * qFrequencyFactor);
    }
    if (inputLocs[FREQUENCY_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          getP() * voltageFactor(false, voltage) * pFrequencyCoefficient);
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          getQ() * voltageFactor(true, voltage) * qFrequencyCoefficient);
    }
}

double IEELLoad::getRealPower(const IOdata& inputs,
                              const StateData& stateData,
                              const SolverMode& sMode) const
{
    const double voltage =
        inputs.empty() ? bus->getVoltage(stateData, sMode) : inputs[VOLTAGE_IN_LOCATION];
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    return isConnected() ? getP() * voltageFactor(false, voltage) *
            (1.0 + (frequencyCoefficient(false) * (frequency - 1.0))) :
                           0.0;
}

double IEELLoad::getReactivePower(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) const
{
    const double voltage =
        inputs.empty() ? bus->getVoltage(stateData, sMode) : inputs[VOLTAGE_IN_LOCATION];
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    return isConnected() ? getQ() * voltageFactor(true, voltage) *
            (1.0 + (frequencyCoefficient(true) * (frequency - 1.0))) :
                           0.0;
}

double IEELLoad::getRealPower(double voltage) const
{
    const double frequency = (bus == nullptr) ? 1.0 : bus->getFreq();
    return isConnected() ? getP() * voltageFactor(false, voltage) *
            (1.0 + (frequencyCoefficient(false) * (frequency - 1.0))) :
                           0.0;
}

double IEELLoad::getReactivePower(double voltage) const
{
    const double frequency = (bus == nullptr) ? 1.0 : bus->getFreq();
    return isConnected() ? getQ() * voltageFactor(true, voltage) *
            (1.0 + (frequencyCoefficient(true) * (frequency - 1.0))) :
                           0.0;
}

double IEELLoad::getRealPower() const
{
    return (bus == nullptr) ? GridLoad::getRealPower() : getRealPower(bus->getVoltage());
}

double IEELLoad::getReactivePower() const
{
    return (bus == nullptr) ? GridLoad::getReactivePower() : getReactivePower(bus->getVoltage());
}
}  // namespace griddyn::loads
