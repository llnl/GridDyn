/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RampLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>

// #include <ctime>
namespace griddyn::loads {
using units::convert;
using units::puA;
using units::puMW;
using units::puOhm;
using units::unit;
RampLoad::RampLoad(const std::string& objName): ZipLoad(objName) {}
RampLoad::RampLoad(double rP, double qP, const std::string& objName): ZipLoad(rP, qP, objName) {}
CoreObject* RampLoad::clone(CoreObject* obj) const
{
    auto* ld = cloneBase<RampLoad, ZipLoad>(this, obj);
    if (ld == nullptr) {
        return obj;
    }

    ld->dPdt = dPdt;
    ld->dQdt = dQdt;
    ld->drdt = drdt;
    ld->dxdt = dxdt;
    ld->dIpdt = dIpdt;
    ld->dIqdt = dIqdt;
    ld->dYqdt = dYqdt;
    ld->dYpdt = dYpdt;
    return ld;
}

// set properties
void RampLoad::set(std::string_view param, std::string_view val)
{
    ZipLoad::set(param, val);
}
void RampLoad::set(std::string_view param, double val, unit unitType)
{
    if (param.length() == 4) {
        if ((param[0] == 'd') && (param[2] == 'd') && (param[3] == 't')) {
            switch (param[1]) {
                case 'p':  // dpdt
                    dPdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                    break;
                case 'r':  // drdt
                    drdt = convert(val, unitType, puA, systemBasePower, localBaseVoltage);
                    break;
                case 'x':  // dxdt
                    dxdt = convert(val, unitType, puOhm, systemBasePower, localBaseVoltage);
                    break;
                case 'q':  // dqdt
                    dQdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                    break;
                case 'i':  // didt
                    dIpdt = convert(val, unitType, puOhm, systemBasePower, localBaseVoltage);
                    break;
                case 'z':
                case 'y':  // dzdt dydt
                    dYpdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                    break;
                default:
                    throw(unrecognizedParameter(param));
            }
        } else {
            ZipLoad::set(param, val, unitType);
        }
    } else if (param.length() == 5) {
        if ((param[0] == 'd') && (param[3] == 'd') && (param[4] == 't')) {
            switch (param[2]) {
                case 'r':
                case 'p':
                    switch (param[1]) {
                        case 'i':  // dirdt dipdt
                            dIpdt =
                                convert(val, unitType, puOhm, systemBasePower, localBaseVoltage);
                            break;
                        case 'z':
                        case 'y':  // dzrdt dyrdt dzpdt dzrdt
                            dYpdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                            break;
                        default:
                            throw(unrecognizedParameter(param));
                    }
                    break;
                case 'q':
                    switch (param[1]) {
                        case 'i':  // diqdt
                            dIqdt =
                                convert(val, unitType, puOhm, systemBasePower, localBaseVoltage);
                            break;
                        case 'z':
                        case 'y':  // dzqdt dyqdt
                            dYqdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                            break;
                        default:
                            throw(unrecognizedParameter(param));
                    }
                    break;
                default:
                    throw(unrecognizedParameter(param));
            }
        } else {
            ZipLoad::set(param, val, unitType);
        }
    } else {
        ZipLoad::set(param, val, unitType);
    }
}

void RampLoad::updateLocalCache(const IOdata& /*inputs*/,
                                const stateData& sD,
                                const solverMode& /*sMode*/)
{
    auto tdiff = sD.time - lastTime;
    if (tdiff == timeZero) {
        return;
    }
    if (dPdt != 0.0) {
        setP(getP() + dPdt * tdiff);
    }
    if (dQdt != 0.0) {
        setQ(getQ() + dQdt * tdiff);
    }

    if ((drdt != 0) || (dxdt != 0)) {
        setr(getr() + drdt * tdiff);
        setx(getx() + dxdt * tdiff);
    } else if ((dYpdt != 0.0) || (dYqdt != 0.0)) {
        setup(getYp() + dYpdt * tdiff);
        setYq(getYq() + dYqdt * tdiff);
    }
    if ((dIpdt != 0) || (dIqdt != 0)) {
        setIp(getIp() + dIpdt * tdiff);
        setIq(getIq() + dIqdt * tdiff);
    }

    lastTime = sD.time;
}

void RampLoad::clearRamp()
{
    dPdt = 0.0;
    dQdt = 0.0;
    drdt = 0.0;
    dxdt = 0.0;
    dIpdt = 0.0;
    dIqdt = 0.0;
    dYqdt = 0.0;
    dYpdt = 0.0;
}
}  // namespace griddyn::loads
