/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsGhostBus.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "helicsCoordinator.h"
#include "helicsLibrary.h"
#include "helicsSupport.h"
#include <string>

namespace griddyn::helicsLib {
HelicsGhostBus::HelicsGhostBus(const std::string& objName): gridBus(objName) {}

CoreObject* HelicsGhostBus::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<HelicsGhostBus, gridBus>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->loadKey = loadKey;
    nobj->voltageKey = voltageKey;

    return nobj;
}

void HelicsGhostBus::pFlowObjectInitializeA(coreTime time0, uint32_t flags)
{
    gridBus::pFlowObjectInitializeA(time0, flags);
}

void HelicsGhostBus::pFlowObjectInitializeB()
{
    gridBus::pFlowInitializeB();
    updateA(prevTime);
    updateB();
}

void HelicsGhostBus::updateA(coreTime time)
{
    if (!loadKey.empty()) {
        double Pact = convert(S.sumP(), units::puMW, outUnits, systemBasePower);
        double Qact = convert(S.sumQ(), units::puMW, outUnits, systemBasePower);
        std::complex<double> ld(Pact, Qact);

        coordinator_->publish(loadIndex, ld);
    }
    lastUpdateTime = time;
}

coreTime HelicsGhostBus::updateB()
{
    nextUpdateTime += updatePeriod;

    // now get the updates
    if (!voltageKey.empty()) {
        //    auto res = helicsGetComplex (voltageKey);
        //    if (res.real () == kNullVal)
        {
            //        voltage = std::abs (res);
            //        angle = std::arg (res);
        }
    }

    return nextUpdateTime;
}

void HelicsGhostBus::timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode)
{
    while (ttime > nextUpdateTime) {
        updateA(nextUpdateTime);
        updateB();
        gridBus::timestep(nextUpdateTime, inputs, sMode);
    }

    gridBus::timestep(ttime, inputs, sMode);
}

void HelicsGhostBus::setFlag(const std::string& flag, bool val)
{
    if (flag.front() == '#') {
    } else {
        gridBus::setFlag(flag, val);
    }
}

void HelicsGhostBus::set(const std::string& param, const std::string& val)
{
    if (param == "voltagekey") {
        voltageKey = val;
        updateSubscription();
    } else if (param == "loadkey") {
        loadKey = val;

        // helicsRegister::instance()->registerPublication(loadKey,
        // helicsRegister::dataType::helicsComplex);
    } else if ((param == "outunits") || (param == "outputunits")) {
        outUnits = units::unit_cast_from_string(val);
    } else {
        gridBus::set(param, val);
    }
}

void HelicsGhostBus::set(const std::string& param, double val, units::unit unitType)
{
    if (param.empty() || param[0] == '#') {
    } else {
        gridBus::set(param, val, unitType);
    }
}

void HelicsGhostBus::updateSubscription()
{
    std::complex<double> cv = std::polar(voltage, angle);
    std::string def = std::to_string(cv.real()) + "+" + std::to_string(cv.imag()) + "j";
    // helicsRegister::instance()->registerSubscription(voltageKey,
    // helicsRegister::dataType::helicsComplex, def);
}

}  // namespace griddyn::helicsLib
