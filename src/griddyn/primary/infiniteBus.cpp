/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "infiniteBus.h"

#include "../Link.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include <string>

namespace griddyn {
using units::convert;
using units::puHz;
using units::puV;
using units::unit;

infiniteBus::infiniteBus(const std::string& objName): gridBus(objName)
{
    // default values
    type = busType::SLK;
    dynType = dynBusType::dynSLK;
}

infiniteBus::infiniteBus(double startVoltage, double startAngle, const std::string& objName):
    gridBus(startVoltage, startAngle, objName)
{
    type = busType::SLK;
    dynType = dynBusType::dynSLK;
}

CoreObject* infiniteBus::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<infiniteBus, gridBus>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->dvdt = dvdt;
    nobj->dfdt = dfdt;
    return nobj;
}

void infiniteBus::updateVoltageAngle(coreTime time)
{
    auto dt = static_cast<double>(time - prevTime);

    angle += 2.0 * kPI * dt * (dfdt / 2.0 + (freq - 1.0));
    freq += dfdt * dt;
    voltage += dvdt * dt;
}

void infiniteBus::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    updateVoltageAngle(time);
    gridBus::timestep(time, inputs, sMode);
}

void infiniteBus::setState(coreTime time,
                           const double state[],
                           const double dstate_dt[],
                           const solverMode& sMode)
{
    updateVoltageAngle(time);
    gridBus::setState(time, state, dstate_dt, sMode);
}

// set properties
void infiniteBus::set(std::string_view param, std::string_view val)
{
    if (param == "type") {
        if (val != "infinite") {
            throw(invalidParameterValue(std::string{param} + ':' + std::string{val}));
        }
    } else {
        gridBus::set(param, val);
    }
}

void infiniteBus::set(std::string_view param, double val, unit unitType)
{
    if (param == "dvdt") {
        dvdt = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if (param == "dfdt") {
        dfdt = convert(val, unitType, puHz, systemBaseFrequency);
    } else {
        gridBus::set(param, val, unitType);
    }
}

double infiniteBus::getVoltage(const double /*state*/[], const solverMode& /*sMode*/) const
{
    return voltage;
}

double infiniteBus::getAngle(const double /*state*/[], const solverMode& /*sMode*/) const
{
    return angle;
}

double infiniteBus::getVoltage(const stateData& sD, const solverMode& /*sMode*/) const
{
    const double dt = (!sD.empty()) ? static_cast<double>(sD.time - prevTime) : 0.0;
    return voltage + dt * dvdt;
}

double infiniteBus::getAngle(const stateData& sD, const solverMode& /*sMode*/) const
{
    const double dt = (!sD.empty()) ? static_cast<double>(sD.time - prevTime) : 0.0;
    return angle + 2 * kPI * (dt * ((freq - 1.0) + dfdt / 2));
}

double infiniteBus::getFreq(const stateData& sD, const solverMode& /*sMode*/) const
{
    double dt = (!sD.empty()) ? static_cast<double>(sD.time - prevTime) : 0.0;
    return freq + dt * dfdt;
}

bool infiniteBus::checkCapable()
{
    return true;
}

}  // namespace griddyn

