/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "IEELLoad.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <optional>

namespace griddyn::loads {
static ChildTypeFactory<IEELLoad, GridLoad> gIEELLoadFactory("load", "ieel");

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
    const bool frequencyDependent = (parameters.coefficients[6] != 0.0) ||
        (parameters.coefficients[7] != 0.0);
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

static std::optional<std::size_t> parameterIndex(std::string_view param,
                                                 char prefix,
                                                 std::size_t maxIndex)
{
    if ((param.size() < 2U) || (param.front() != prefix)) {
        return std::nullopt;
    }
    std::size_t value = 0;
    const auto parsed = std::from_chars(param.data() + 1, param.data() + param.size(), value);
    if ((parsed.ec != std::errc{}) || (parsed.ptr != param.data() + param.size()) ||
        (value == 0U) || (value > maxIndex)) {
        return std::nullopt;
    }
    return value - 1U;
}

void IEELLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    static constexpr std::array<std::string_view, 14> parameterNames{
        "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8",
        "n1", "n2", "n3", "n4", "n5", "n6"};
    if (pstype == ParamStringType::LOCAL_NUM) {
        pstr.assign(parameterNames.begin(), parameterNames.end());
        return;
    }
    if ((pstype == ParamStringType::ALL) || (pstype == ParamStringType::NUMERIC)) {
        pstr.insert(pstr.end(), parameterNames.begin(), parameterNames.end());
    }
    GridLoad::getParameterStrings(pstr, pstype);
}

void IEELLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}

void IEELLoad::set(std::string_view param, double val, units::unit unitType)
{
    if (const auto coefficientIndex = parameterIndex(param, 'a', parameters.coefficients.size())) {
        parameters.coefficients[*coefficientIndex] = val;
        setIEELParameters(parameters);
    } else if (const auto exponentIndex = parameterIndex(param, 'n', parameters.exponents.size())) {
        parameters.exponents[*exponentIndex] = val;
    } else {
        GridLoad::set(param, val, unitType);
    }
}

double IEELLoad::get(std::string_view param, units::unit unitType) const
{
    if (const auto index = parameterIndex(param, 'a', parameters.coefficients.size())) {
        return parameters.coefficients[*index];
    }
    if (const auto index = parameterIndex(param, 'n', parameters.exponents.size())) {
        return parameters.exponents[*index];
    }
    return GridLoad::get(param, unitType);
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
    const double aP = frequencyCoefficient(false);
    const double aQ = frequencyCoefficient(true);
    const double pFrequencyFactor = 1.0 + (aP * (frequency - 1.0));
    const double qFrequencyFactor = 1.0 + (aQ * (frequency - 1.0));

    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getP() * voltageDerivative(false, voltage) * pFrequencyFactor);
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getQ() * voltageDerivative(true, voltage) * qFrequencyFactor);
    }
    if (inputLocs[FREQUENCY_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION, inputLocs[FREQUENCY_IN_LOCATION],
                          getP() * voltageFactor(false, voltage) * aP);
        matrixData.assign(QOUT_LOCATION, inputLocs[FREQUENCY_IN_LOCATION],
                          getQ() * voltageFactor(true, voltage) * aQ);
    }
}

double IEELLoad::getRealPower(const IOdata& inputs,
                              const StateData& stateData,
                              const SolverMode& sMode) const
{
    const double voltage = inputs.empty() ? bus->getVoltage(stateData, sMode) :
                                            inputs[VOLTAGE_IN_LOCATION];
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
    const double voltage = inputs.empty() ? bus->getVoltage(stateData, sMode) :
                                            inputs[VOLTAGE_IN_LOCATION];
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
