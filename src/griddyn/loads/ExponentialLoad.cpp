/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExponentialLoad.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::loads {
ExponentialLoad::ExponentialLoad(const std::string& objName): GridLoad(objName) {}
ExponentialLoad::ExponentialLoad(double rP, double qP, const std::string& objName):
    GridLoad(rP, qP, objName)
{
}
CoreObject* ExponentialLoad::clone(CoreObject* obj) const
{
    auto ld = cloneBase<ExponentialLoad, GridLoad>(this, obj);
    if (ld == nullptr) {
        return obj;
    }

    ld->alphaP = alphaP;
    ld->alphaQ = alphaQ;
    return ld;
}

// set properties
void ExponentialLoad::set(std::string_view param, std::string_view val)
{
    GridLoad::set(param, val);
}
void ExponentialLoad::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "alphap") || (param == "ap")) {
        alphaP = val;
    } else if ((param == "alphaq") || (param == "aq")) {
        alphaQ = val;
    } else if ((param == "alpha") || (param == "a")) {
        alphaP = alphaQ = val;
    } else {
        GridLoad::set(param, val, unitType);
    }
}

void ExponentialLoad::ioPartialDerivatives(const IOdata& inputs,
                                           const StateData& /*sD*/,
                                           MatrixData<double>& md,
                                           const IOlocs& inputLocs,
                                           const SolverMode& /*sMode*/)
{
    const double V = inputs[voltageInLocation];
    // power vs voltage
    if (inputLocs[voltageInLocation] != kNullLocation) {
        md.assign(PoutLocation,
                  inputLocs[voltageInLocation],
                  getP() * alphaP * pow(V, alphaP - 1.0));

        // reactive power vs voltage
        md.assign(QoutLocation,
                  inputLocs[voltageInLocation],
                  getQ() * alphaQ * pow(V, alphaQ - 1.0));
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
    return getRealPower(inputs[voltageInLocation]);
}

double ExponentialLoad::getReactivePower(const IOdata& inputs,
                                         const StateData& /*sD*/,
                                         const SolverMode& /*sMode*/) const
{
    return getReactivePower(inputs[voltageInLocation]);
}

double ExponentialLoad::getRealPower(const double V) const
{
    if (isConnected()) {
        double val = getP();
        val *= pow(V, alphaP);
        return val;
    }
    return 0.0;
}

double ExponentialLoad::getReactivePower(double V) const
{
    if (isConnected()) {
        double val = getQ();
        val *= pow(V, alphaQ);
        return val;
    }
    return 0.0;
}
}  // namespace griddyn::loads
