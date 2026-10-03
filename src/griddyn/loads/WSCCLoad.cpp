/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WSCCLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace griddyn::loads {
static ChildTypeFactory<WSCCLoad, GridLoad> gWSCCLoadFactory("load", "wscc");

namespace {
    constexpr std::array<std::string_view, 11>
        parameterNames{"p1", "q1", "p2", "q2", "p3", "q3", "p4", "q4", "lpd", "lqd", "vmin"};

    bool allFinite(const WSCCParameters& parameters)
    {
        return std::isfinite(parameters.p1) && std::isfinite(parameters.q1) &&
            std::isfinite(parameters.p2) && std::isfinite(parameters.q2) &&
            std::isfinite(parameters.p3) && std::isfinite(parameters.q3) &&
            std::isfinite(parameters.p4) && std::isfinite(parameters.q4) &&
            std::isfinite(parameters.lpd) && std::isfinite(parameters.lqd) &&
            std::isfinite(parameters.vmin);
    }
}  // namespace

WSCCLoad::WSCCLoad(const std::string& objName): GridLoad(objName) {}

CoreObject* WSCCLoad::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<WSCCLoad, GridLoad>(this, obj);
    if (copy != nullptr) {
        copy->parameters = parameters;
    }
    return (copy == nullptr) ? obj : copy;
}

void WSCCLoad::setWSCCParameters(const WSCCParameters& newParameters)
{
    if (!allFinite(newParameters)) {
        throw InvalidParameterValue("WSCC load parameters must all be finite");
    }
    if (newParameters.vmin < 0.0) {
        throw InvalidParameterValue("WSCC VMIN must be nonnegative");
    }
    parameters = newParameters;
    opFlags.set(USES_BUS_FREQUENCY, hasFrequencyDependence());
}

bool WSCCLoad::hasFrequencyDependence() const
{
    const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
    if (extended) {
        return ((parameters.p4 * parameters.lpd) != 0.0) ||
            ((parameters.q4 * parameters.lqd) != 0.0);
    }
    return (((parameters.p1 != 0.0) || (parameters.p2 != 0.0) || (parameters.p3 != 0.0)) &&
            (parameters.lpd != 0.0)) ||
        (((parameters.q1 != 0.0) || (parameters.q2 != 0.0) || (parameters.q3 != 0.0)) &&
         (parameters.lqd != 0.0));
}

double WSCCLoad::voltagePolynomial(bool reactive, double voltage) const
{
    const double first = reactive ? parameters.q1 : parameters.p1;
    const double second = reactive ? parameters.q2 : parameters.p2;
    const double third = reactive ? parameters.q3 : parameters.p3;
    const double fourth = reactive ? parameters.q4 : parameters.p4;
    const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
    const double base = ((first * voltage) * voltage) + (second * voltage) + third;
    return extended ? (base + fourth) : base;
}

double WSCCLoad::voltageDerivative(bool reactive, double voltage) const
{
    const double first = reactive ? parameters.q1 : parameters.p1;
    const double second = reactive ? parameters.q2 : parameters.p2;
    return ((2.0 * first) * voltage) + second;
}

double WSCCLoad::frequencyDerivative(bool reactive, double voltage) const
{
    const double fourth = reactive ? parameters.q4 : parameters.p4;
    const double frequencyCoefficient = reactive ? parameters.lqd : parameters.lpd;
    const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
    if (extended) {
        return fourth * frequencyCoefficient;
    }
    return voltagePolynomial(reactive, voltage) * frequencyCoefficient;
}

double WSCCLoad::powerAtVoltage(bool reactive, double voltage, double frequency) const
{
    const double basePower = reactive ? getQ() : getP();
    const double fourth = reactive ? parameters.q4 : parameters.p4;
    const double frequencyCoefficient = reactive ? parameters.lqd : parameters.lpd;
    const double frequencyFactor = 1.0 + (frequencyCoefficient * (frequency - 1.0));
    const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
    const double polynomial = voltagePolynomial(reactive, voltage);
    double value = 0.0;
    if (extended) {
        value = basePower * (polynomial + (fourth * frequencyCoefficient * (frequency - 1.0)));
    } else {
        value = basePower * polynomial * frequencyFactor;
    }
    if ((parameters.vmin > 0.0) && (voltage < parameters.vmin)) {
        const double voltageRatio = voltage / parameters.vmin;
        const double minimumPolynomial = voltagePolynomial(reactive, parameters.vmin);
        const double minimumValue = extended ?
            basePower * (minimumPolynomial + (fourth * frequencyCoefficient * (frequency - 1.0))) :
            basePower * minimumPolynomial * frequencyFactor;
        value = minimumValue * voltageRatio * voltageRatio;
    }
    return value;
}

double WSCCLoad::powerVoltageDerivative(bool reactive, double voltage, double frequency) const
{
    const double basePower = reactive ? getQ() : getP();
    const double frequencyCoefficient = reactive ? parameters.lqd : parameters.lpd;
    const double frequencyFactor = 1.0 + (frequencyCoefficient * (frequency - 1.0));
    const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
    if ((parameters.vmin > 0.0) && (voltage < parameters.vmin)) {
        const double scale = powerAtVoltage(reactive, parameters.vmin, frequency) /
            (parameters.vmin * parameters.vmin);
        return ((2.0 * voltage) * scale);
    }
    const double polynomialDerivative = voltageDerivative(reactive, voltage);
    return extended ? (basePower * polynomialDerivative) :
                      (basePower * polynomialDerivative * frequencyFactor);
}

double WSCCLoad::powerFrequencyDerivative(bool reactive, double voltage, double frequency) const
{
    const double basePower = reactive ? getQ() : getP();
    if ((parameters.vmin > 0.0) && (voltage < parameters.vmin)) {
        const double fourth = reactive ? parameters.q4 : parameters.p4;
        const double frequencyCoefficient = reactive ? parameters.lqd : parameters.lpd;
        const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
        const double minimumFrequencyDerivative = extended ?
            fourth * frequencyCoefficient :
            voltagePolynomial(reactive, parameters.vmin) * frequencyCoefficient;
        const double voltageRatio = voltage / parameters.vmin;
        return basePower * minimumFrequencyDerivative * voltageRatio * voltageRatio;
    }
    return basePower * frequencyDerivative(reactive, voltage);
}

void WSCCLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    if (pstype == ParamStringType::LOCAL_NUM) {
        pstr.assign(parameterNames.begin(), parameterNames.end());
        return;
    }
    if ((pstype == ParamStringType::ALL) || (pstype == ParamStringType::NUMERIC)) {
        pstr.insert(pstr.end(), parameterNames.begin(), parameterNames.end());
    }
    GridLoad::getParameterStrings(pstr, pstype);
}

void WSCCLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}

void WSCCLoad::set(std::string_view param, double val, units::unit unitType)
{
    auto updatedParameters = parameters;
    bool handled = true;
    if (param == "p1") {
        updatedParameters.p1 = val;
    } else if (param == "q1") {
        updatedParameters.q1 = val;
    } else if (param == "p2") {
        updatedParameters.p2 = val;
    } else if (param == "q2") {
        updatedParameters.q2 = val;
    } else if (param == "p3") {
        updatedParameters.p3 = val;
    } else if (param == "q3") {
        updatedParameters.q3 = val;
    } else if (param == "p4") {
        updatedParameters.p4 = val;
    } else if (param == "q4") {
        updatedParameters.q4 = val;
    } else if (param == "lpd") {
        updatedParameters.lpd = val;
    } else if (param == "lqd") {
        updatedParameters.lqd = val;
    } else if (param == "vmin") {
        updatedParameters.vmin = val;
    } else {
        handled = false;
    }
    if (handled) {
        setWSCCParameters(updatedParameters);
    } else {
        GridLoad::set(param, val, unitType);
    }
}

double WSCCLoad::get(std::string_view param, units::unit unitType) const
{
    if (param == "p1") {
        return parameters.p1;
    }
    if (param == "q1") {
        return parameters.q1;
    }
    if (param == "p2") {
        return parameters.p2;
    }
    if (param == "q2") {
        return parameters.q2;
    }
    if (param == "p3") {
        return parameters.p3;
    }
    if (param == "q3") {
        return parameters.q3;
    }
    if (param == "p4") {
        return parameters.p4;
    }
    if (param == "q4") {
        return parameters.q4;
    }
    if (param == "lpd") {
        return parameters.lpd;
    }
    if (param == "lqd") {
        return parameters.lqd;
    }
    if (param == "vmin") {
        return parameters.vmin;
    }
    return GridLoad::get(param, unitType);
}

void WSCCLoad::ioPartialDerivatives(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          powerVoltageDerivative(false, voltage, frequency));
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          powerVoltageDerivative(true, voltage, frequency));
    }
    if (inputLocs[FREQUENCY_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          powerFrequencyDerivative(false, voltage, frequency));
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          powerFrequencyDerivative(true, voltage, frequency));
    }
}

double WSCCLoad::getRealPower(const IOdata& inputs,
                              const StateData& stateData,
                              const SolverMode& sMode) const
{
    const double voltage =
        inputs.empty() ? bus->getVoltage(stateData, sMode) : inputs[VOLTAGE_IN_LOCATION];
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    return isConnected() ? powerAtVoltage(false, voltage, frequency) : 0.0;
}

double WSCCLoad::getReactivePower(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) const
{
    const double voltage =
        inputs.empty() ? bus->getVoltage(stateData, sMode) : inputs[VOLTAGE_IN_LOCATION];
    const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
        inputs[FREQUENCY_IN_LOCATION] :
        bus->getFreq(stateData, sMode);
    return isConnected() ? powerAtVoltage(true, voltage, frequency) : 0.0;
}

double WSCCLoad::getRealPower(double voltage) const
{
    const double frequency = (bus == nullptr) ? 1.0 : bus->getFreq();
    return isConnected() ? powerAtVoltage(false, voltage, frequency) : 0.0;
}

double WSCCLoad::getReactivePower(double voltage) const
{
    const double frequency = (bus == nullptr) ? 1.0 : bus->getFreq();
    return isConnected() ? powerAtVoltage(true, voltage, frequency) : 0.0;
}

double WSCCLoad::getRealPower() const
{
    return (bus == nullptr) ? GridLoad::getRealPower() : getRealPower(bus->getVoltage());
}

double WSCCLoad::getReactivePower() const
{
    return (bus == nullptr) ? GridLoad::getReactivePower() : getReactivePower(bus->getVoltage());
}
}  // namespace griddyn::loads
