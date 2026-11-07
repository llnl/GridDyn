/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WSCCLoad.h"

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
#include <string_view>

namespace griddyn::loads {
static ChildTypeFactory<WSCCLoad, GridLoad> gWSCCLoadFactory("load", "wscc");

namespace {

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

const ModelDescription& WSCCLoad::getModelDescription() const
{
    static const auto description = ModelDescription::define<WSCCLoad>(
        "WSCCLoad",
        &GridLoad::getModelDescription(),
        {parameter({"p1"}, &WSCCLoad::parameters, &WSCCParameters::p1, units::one, "WSCC p1"),
         parameter({"q1"}, &WSCCLoad::parameters, &WSCCParameters::q1, units::one, "WSCC q1"),
         parameter({"p2"}, &WSCCLoad::parameters, &WSCCParameters::p2, units::one, "WSCC p2"),
         parameter({"q2"}, &WSCCLoad::parameters, &WSCCParameters::q2, units::one, "WSCC q2"),
         parameter({"p3"}, &WSCCLoad::parameters, &WSCCParameters::p3, units::one, "WSCC p3"),
         parameter({"q3"}, &WSCCLoad::parameters, &WSCCParameters::q3, units::one, "WSCC q3"),
         parameter({"p4"}, &WSCCLoad::parameters, &WSCCParameters::p4, units::one, "WSCC p4"),
         parameter({"q4"}, &WSCCLoad::parameters, &WSCCParameters::q4, units::one, "WSCC q4"),
         parameter({"lpd"}, &WSCCLoad::parameters, &WSCCParameters::lpd, units::one, "WSCC lpd"),
         parameter({"lqd"}, &WSCCLoad::parameters, &WSCCParameters::lqd, units::one, "WSCC lqd"),
         parameter(
             {"vmin"}, &WSCCLoad::parameters, &WSCCParameters::vmin, units::one, "WSCC vmin")});
    return description;
}

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

double WSCCLoad::powerFrequencyDerivative(bool reactive, double voltage) const
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
    const auto& description = WSCCLoad::getModelDescription();
    getParamString<WSCCLoad, GridLoad>(this,
                                       pstr,
                                       description.numericNames(),
                                       description.stringNames(),
                                       description.flagNames(),
                                       pstype);
}

void WSCCLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}

void WSCCLoad::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(WSCCLoad::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        if (!param.empty() && param.front() != '#') {
            setWSCCParameters(parameters);
        }
        return;
    }
    GridLoad::set(param, val, unitType);
}

double WSCCLoad::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = WSCCLoad::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
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
                          powerFrequencyDerivative(false, voltage));
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          powerFrequencyDerivative(true, voltage));
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
