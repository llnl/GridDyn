/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExponentialLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::loads {

ExponentialLoad::ExponentialLoad(const std::string& objName): GridLoad(objName) {}
ExponentialLoad::ExponentialLoad(double realPower,
                                 double reactivePower,
                                 const std::string& objName):
    GridLoad(realPower, reactivePower, objName)
{
}

const ModelDescription& ExponentialLoad::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExponentialLoad>(
        "ExponentialLoad",
        &GridLoad::getModelDescription(),
        {parameter({"alphap", "ap"},
                   &ExponentialLoad::alphaP,
                   units::one,
                   "Voltage exponent for real power"),
         parameter({"alphaq", "aq"},
                   &ExponentialLoad::alphaQ,
                   units::one,
                   "Voltage exponent for reactive power"),
         command({"alpha", "a"}, units::one, "Set both voltage exponents")});
    return description;
}

CoreObject* ExponentialLoad::clone(CoreObject* obj) const
{
    auto* clonedLoad = cloneBase<ExponentialLoad, GridLoad>(this, obj);
    if (clonedLoad == nullptr) {
        return obj;
    }

    clonedLoad->alphaP = alphaP;
    clonedLoad->alphaQ = alphaQ;
    return clonedLoad;
}

// set properties
void ExponentialLoad::set(std::string_view param, std::string_view val)
{
    if (!isSetHandled(ExponentialLoad::getModelDescription().trySetLocal(*this, param, val))) {
        GridLoad::set(param, val);
    }
}
void ExponentialLoad::set(std::string_view param, double val, units::unit unitType)
{
    const auto result = ExponentialLoad::getModelDescription().trySet(*this, param, val, unitType);
    if (result == SetResult::COMMAND) {
        const auto converted = units::convert(val, unitType, units::one);
        if (!std::isfinite(converted)) {
            throw InvalidParameterValue(param);
        }
        alphaP = alphaQ = converted;
    } else if (result == SetResult::NO_MATCH) {
        GridLoad::set(param, val, unitType);
    }
}

void ExponentialLoad::ioPartialDerivatives(const IOdata& inputs,
                                           const StateData& /*sD*/,
                                           MatrixData<double>& matrixData,
                                           const IOlocs& inputLocs,
                                           const SolverMode& /*sMode*/)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    // power vs voltage
    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getP() * alphaP * pow(voltage, alphaP - 1.0));

        // reactive power vs voltage
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getQ() * alphaQ * pow(voltage, alphaQ - 1.0));
    }
}

double ExponentialLoad::getRealPower() const
{
    return getRealPower(bus->getVoltage());
}
double ExponentialLoad::getReactivePower() const
{
    return getReactivePower(bus->getVoltage());
}
double ExponentialLoad::getRealPower(const IOdata& inputs,
                                     const StateData& /*sD*/,
                                     const SolverMode& /*sMode*/) const
{
    return getRealPower(inputs[VOLTAGE_IN_LOCATION]);
}

double ExponentialLoad::getReactivePower(const IOdata& inputs,
                                         const StateData& /*sD*/,
                                         const SolverMode& /*sMode*/) const
{
    return getReactivePower(inputs[VOLTAGE_IN_LOCATION]);
}

double ExponentialLoad::getRealPower(const double voltage) const
{
    if (isConnected()) {
        double val = getP();
        val *= pow(voltage, alphaP);
        return val;
    }
    return 0.0;
}

double ExponentialLoad::getReactivePower(double voltage) const
{
    if (isConnected()) {
        double val = getQ();
        val *= pow(voltage, alphaQ);
        return val;
    }
    return 0.0;
}
}  // namespace griddyn::loads
