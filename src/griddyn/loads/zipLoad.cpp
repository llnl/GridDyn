/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zipLoad.h"

#include "../gridBus.h"
#include "ThreePhaseLoad.h"
#include "approximatingLoad.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "exponentialLoad.h"
#include "fDepLoad.h"
#include "fileLoad.h"
#include "rampLoad.h"
#include "sourceLoad.h"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <complex>
#include <iostream>
#include <string>

namespace griddyn {
using units::convert;
using units::puA;
using units::puMW;
using units::puOhm;
using units::puV;
using units::unit;

// setup the load object factories
static typeFactory<Load> glf("load", std::to_array<std::string_view>({"simple", "constant"}));
static childTypeFactory<zipLoad, Load> zlf("load",
                                           std::to_array<std::string_view>({"basic", "zip"}),
                                           "zip");  // set basic to the default
namespace loads {
    static typeFactoryArg<sourceLoad, sourceLoad::sourceType>
        glfp("load", "pulse", sourceLoad::sourceType::pulse);
    static typeFactoryArg<sourceLoad, sourceLoad::sourceType>
        cfgsl("load",
              std::to_array<std::string_view>({"sine", "sin", "sinusoidal"}),
              sourceLoad::sourceType::sine);
    static childTypeFactory<rampLoad, Load> glfr("load", "ramp");
    static typeFactoryArg<sourceLoad, sourceLoad::sourceType>
        glfrand("load",
                std::to_array<std::string_view>({"random", "rand"}),
                sourceLoad::sourceType::random);
    static childTypeFactory<fileLoad, Load> glfld("load", "file");
    static childTypeFactory<sourceLoad, Load>
        srcld("load", std::to_array<std::string_view>({"src", "source"}));
    static childTypeFactory<exponentialLoad, Load>
        glexp("load", std::to_array<std::string_view>({"exponential", "exp"}));
    static childTypeFactory<fDepLoad, Load> glfd("load", "fdep");
    static childTypeFactory<ThreePhaseLoad, Load>
        gl3("load", std::to_array<std::string_view>({"3phase", "3p", "threephase"}));

    static childTypeFactory<approximatingLoad, Load>
        apld("load", std::to_array<std::string_view>({"approx", "approximating"}));
}  // namespace loads

zipLoad::zipLoad(const std::string& objName): Load(objName) {}
zipLoad::zipLoad(double rP, double rQ, const std::string& objName): Load(rP, rQ, objName) {}
CoreObject* zipLoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBaseFactory<zipLoad, Load>(this, obj, &zlf);
    if (nobj == nullptr) {
        return obj;
    }
    // nobj->Psched = Psched;
    nobj->Yp = Yp;
    nobj->Yq = Yq;
    nobj->Iq = Iq;
    nobj->Ip = Ip;
    nobj->Pout = Pout;
    nobj->Vpqmax = Vpqmax;
    nobj->Vpqmin = Vpqmin;
    nobj->localBaseVoltage = localBaseVoltage;
    nobj->trigVVlow = trigVVlow;
    nobj->trigVVhigh = trigVVhigh;
    return nobj;
}

void zipLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    Load::pFlowObjectInitializeA(time0, flags);
    // Psched = getRealPower();
    // dPdf = -H / 30 * Psched;
    lastTime = time0;
#ifdef SGS_DEBUG
    std::cout << "SGS : " << prevTime << " : " << name
              << " zipLoad::pFlowInitializeA realPower = " << getRealPower()
              << " reactive power = " << getReactivePower() << '\n';
#endif
}

void zipLoad::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t flags)
{
    if ((opFlags[convert_to_constant_impedance]) ||
        CHECK_CONTROLFLAG(flags, all_loads_to_constant_impedence)) {
        double voltage = bus->getVoltage();
        double invVsquared = 1.0 / (voltage * voltage);
        Yp = Yp + P * invVsquared;
        P = 0;
        if (opFlags[use_power_factor_flag]) {
            Yq = Yq + P * pfq * invVsquared;
            Q = 0;
        } else {
            Yq = Yq + Q * invVsquared;
            Q = 0;
        }
    }

#ifdef SGS_DEBUG
    std::cout << "SGS : " << prevTime << " : " << name
              << " zipLoad::dynInitializeA realPower = " << getRealPower()
              << " reactive power = " << getReactivePower() << '\n';
#endif
}

void zipLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    if (!isConnected()) {
        Pout = 0;
        return;
    }
    if (time != prevTime) {
        updateLocalCache(inputs, stateData(time), cLocalSolverMode);
    }

    double voltage = (inputs.empty()) ? (bus->getVoltage()) : inputs[voltageInLocation];
    Pout = -getRealPower(voltage);
    prevTime = time;
    Qout = -getReactivePower(voltage);

#ifdef SGS_DEBUG
    std::cout << "SGS : " << prevTime << " : " << name
              << " zipLoad::timestep realPower = " << getRealPower()
              << " reactive power = " << getReactivePower() << '\n';
#endif
}

static const stringVec
    locNumStrings{"yp", "yq", "ip", "iq", "x", "r", "h", "m", "vpqmin", "vpqmax"};

static const stringVec locStrStrings{

};

static const stringVec flagStrings{"converttoimpedance", "no_pqvoltage_limit"};

void zipLoad::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<zipLoad, Load>(this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void zipLoad::setFlag(std::string_view flag, bool val)
{
    if (flag == "usepowerfactor") {
        if (val) {
            if (!(opFlags[use_power_factor_flag])) {
                opFlags.set(use_power_factor_flag);
                updatepfq();
            }
        } else {
            opFlags.reset(use_power_factor_flag);
        }
    } else if (flag == "converttoimpedance") {
        opFlags.set(convert_to_constant_impedance, val);
    } else if (flag == "no_pqvoltage_limit") {
        opFlags.set(no_pqvoltage_limit, val);
        if (opFlags[no_pqvoltage_limit]) {
            Vpqmax = 100;
            Vpqmin = -1.0;
        }
    } else {
        Load::setFlag(flag, val);
    }
}

// set properties
void zipLoad::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Load::set(param, val);
    }
}

double zipLoad::get(std::string_view param, unit unitType) const
{
    double val = kNullVal;
    if (param.length() == 1) {
        switch (param[0]) {
            case 'p':
                val = convert(getP(), puMW, unitType, systemBasePower);
                break;
            case 'q':
                val = convert(getQ(), puMW, unitType, systemBasePower);
                break;
            case 'r':
                val = getr();
                break;
            case 'x':
                val = getx();
                break;
            case 'z':
                val = std::abs(std::complex<double>(getr(), getx()));
                break;
            case 'y':
                val = std::abs(1.0 / std::complex<double>(getr(), getx()));
                break;
            case 'g':
                val = 1 / getr();
                break;
            case 'b':
                val = 1.0 / getx();
                break;
            default:
                break;
        }
        return val;
    }
    if (param == "yp") {
        val = convert(Yp, puMW, unitType, systemBasePower);
    } else if (param == "yq") {
        val = convert(Yq, puMW, unitType, systemBasePower);
    } else if (param == "ip") {
        val = convert(Ip, puMW, unitType, systemBasePower);
    } else if (param == "iq") {
        val = convert(Iq, puMW, unitType, systemBasePower);
    } else if (param == "pf") {
        val = pfq;
    } else {
        val = Load::get(param, unitType);
    }
    return val;
}

void zipLoad::set(std::string_view param, double val, unit unitType)
{
    if (param.empty()) {
        return;
    }
    if (param.length() == 1) {
        switch (param[0]) {
            case 'p':
                setP(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
                break;
            case 'q':
                setQ(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
                break;
            case 'r':
                setr(convert(val, unitType, puOhm, systemBasePower, localBaseVoltage));
                break;
            case 'x':
                setx(convert(val, unitType, puOhm, systemBasePower, localBaseVoltage));
                break;
            case 'g':
                setup(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
                break;
            case 'b':
                setYq(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
                break;
            default:
                throw(unrecognizedParameter(param));
        }
        checkFaultChange();
        return;
    }
    if (param.back() == '+')  // load increments
    {
        // load increments  allows a delta on the load through the set functions
        if (param == "p+") {
            P += convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            checkpfq();
        } else if (param == "q+") {
            Q += convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            updatepfq();
        } else if ((param == "yp+") || (param == "zr+")) {
            Yp += convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            checkFaultChange();
        } else if ((param == "yq+") || (param == "zq+")) {
            Yq += convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            checkFaultChange();
        } else if ((param == "ir+") || (param == "ip+")) {
            Ip += convert(val, unitType, puA, systemBasePower, localBaseVoltage);
            checkFaultChange();
        } else if (param == "iq+") {
            Iq += convert(val, unitType, puA, systemBasePower, localBaseVoltage);
            checkFaultChange();
        } else {
            gridSecondary::set(param, val, unitType);  // NOLINT
        }
    } else if (param.back() == '*') {
        // load increments  allows a delta on the load through the set functions
        if (param == "p*") {
            P *= val;
            checkpfq();
        } else if (param == "q*") {
            Q *= val;
            updatepfq();
        } else if ((param == "yp*") || (param == "zr*")) {
            Yp *= val;
            checkFaultChange();
        } else if ((param == "yq*") || (param == "zq*")) {
            Yq *= val;
            checkFaultChange();
        } else if ((param == "ir*") || (param == "ip*")) {
            Ip *= val;
            checkFaultChange();
        } else if (param == "iq*") {
            Iq *= val;
            checkFaultChange();
        } else {
            gridSecondary::set(param, val, unitType);  // NOLINT
        }
    } else if (param == "load p") {
        setP(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
    } else if (param == "load q") {
        setQ(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
    } else if ((param == "yp") || (param == "shunt g") || (param == "zr")) {
        setup(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
    } else if ((param == "yq") || (param == "shunt b") || (param == "zq")) {
        setYq(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
    } else if ((param == "ir") || (param == "ip")) {
        setIp(convert(val, unitType, puA, systemBasePower, localBaseVoltage));
    } else if (param == "iq") {
        setIq(convert(val, unitType, puA, systemBasePower, localBaseVoltage));
    } else if ((param == "pf") || (param == "powerfactor")) {
        if (val != 0.0) {
            if (std::abs(val) <= 1.0) {
                pfq = std::sqrt(1.0 - val * val) / val;
            } else {
                pfq = 0.0;
            }
        } else {
            pfq = kBigNum;
        }
        opFlags.set(use_power_factor_flag);
    } else if (param == "scale") {
        P *= val;
        Q *= val;
        Ip *= val;
        Iq *= val;
        Yp *= val;
        Yq *= val;
        updatepfq();
        checkFaultChange();
    } else if (param == "qratio") {
        pfq = val;
        opFlags.set(use_power_factor_flag);
    } else if (param == "vpqmin") {
        if (!opFlags[no_pqvoltage_limit]) {
            Vpqmin = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
            trigVVlow = 1.0 / (Vpqmin * Vpqmin);
        }
    } else if (param == "vpqmax") {
        if (!opFlags[no_pqvoltage_limit]) {
            Vpqmax = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
            trigVVhigh = 1.0 / (Vpqmax * Vpqmax);
        }
    } else if (param == "pqlowvlimit") {
        // this is mostly a convenience flag for adaptive solving
        if (val > 0.1)  // not a flag
        {
            if (Vpqmin < 0.5) {
                if (!opFlags[no_pqvoltage_limit]) {
                    Vpqmin = 0.9;
                    trigVVlow = 1.0 / (Vpqmin * Vpqmin);
                }
            }
        }
    } else if ((param == "basevoltage") || (param == "base vol")) {
        // SGS added to set the base voltage 2015-01-30
        localBaseVoltage = val;
    } else {
        Load::set(param, val, unitType);
    }
}

void zipLoad::setup(double newYp)
{
    Yp = newYp;
    checkFaultChange();
}

void zipLoad::setYq(double newYq)
{
    Yq = newYq;
    checkFaultChange();
}

void zipLoad::setIp(double newIp)
{
    Ip = newIp;
    checkFaultChange();
}

void zipLoad::setIq(double newIq)
{
    Iq = newIq;
    checkFaultChange();
}

void zipLoad::setr(double newr)
{
    if (newr == 0.0) {
        Yp = 0.0;
        return;
    }
    std::complex<double> z(newr, getx());
    auto y = std::conj(1.0 / z);
    Yp = y.real();
    Yq = y.imag();
    checkFaultChange();
}
void zipLoad::setx(double newx)
{
    if (newx == 0.0) {
        Yq = 0.0;
        return;
    }
    std::complex<double> z(getr(), -newx);
    auto y = 1.0 / z;
    Yp = y.real();
    Yq = y.imag();
    checkFaultChange();
}

double zipLoad::getr() const
{
    if (Yp == 0.0) {
        return 0.0;
    }
    std::complex<double> y(Yp, Yq);
    auto z = 1.0 / y;  // I would take a conjugate but it doesn't matter since we are only returning
                       // the real part
    return z.real();
}

double zipLoad::getx() const
{
    if (Yq == 0.0) {
        return 0.0;
    }
    std::complex<double> y(Yp, Yq);
    auto z = std::conj(1.0 / y);
    return z.imag();
}

void zipLoad::updateLocalCache(const IOdata& /*inputs*/,
                               const stateData& sD,
                               const solverMode& /*sMode*/)
{
    lastTime = sD.time;
}

void zipLoad::setState(coreTime time,
                       const double state[],
                       const double dstate_dt[],
                       const solverMode& sMode)
{
    stateData sD(time, state, dstate_dt);
    updateLocalCache(noInputs, sD, sMode);
    prevTime = time;
}

double zipLoad::voltageAdjustment(double val, double voltage) const
{
    if (voltage < Vpqmin) {
        val = voltage * voltage * val * trigVVlow;
    } else if (voltage > Vpqmax) {
        val = voltage * voltage * val * trigVVhigh;
    }
    return val;
}

double zipLoad::getQval() const
{
    double val = Q;

    if (opFlags[use_power_factor_flag]) {
        if (pfq < 1000.0) {
            val = P * pfq;
        }
    }
    return val;
}

double zipLoad::getRealPower() const
{
    return getRealPower(bus->getVoltage());
}

double zipLoad::getReactivePower() const
{
    return getReactivePower(bus->getVoltage());
}

double
    zipLoad::getRealPower(const IOdata& inputs, const stateData& sD, const solverMode& sMode) const
{
    double voltage = (inputs.empty()) ? (bus->getVoltage(sD, sMode)) : inputs[voltageInLocation];
    return getRealPower(voltage);
}

double zipLoad::getReactivePower(const IOdata& inputs,
                                 const stateData& sD,
                                 const solverMode& sMode) const
{
    double voltage = (inputs.empty()) ? (bus->getVoltage(sD, sMode)) : inputs[voltageInLocation];
    return getReactivePower(voltage);
}

double zipLoad::getRealPower(const double voltage) const
{
    if (!isConnected()) {
        return 0.0;
    }
    double val = voltageAdjustment(P, voltage);
    val += voltage * (voltage * Yp + Ip);

    return val;
}

double zipLoad::getReactivePower(double voltage) const
{
    if (!isConnected()) {
        return 0.0;
    }
    double val = voltageAdjustment(getQval(), voltage);

    val += voltage * (voltage * Yq + Iq);
    return val;
}

void zipLoad::outputPartialDerivatives(const IOdata& inputs,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       const solverMode& sMode)
{
    if (inputs.empty())  // we only have output derivatives if the input arguments are not counted
    {
        auto argsBus = bus->getOutputs(noInputs, sD, sMode);
        auto inputLocs = bus->getOutputLocs(sMode);
        ioPartialDerivatives(argsBus, sD, md, inputLocs, sMode);
    }
}

count_t zipLoad::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 0;
}
void zipLoad::ioPartialDerivatives(const IOdata& inputs,
                                   const stateData& sD,
                                   matrixData<double>& md,
                                   const IOlocs& inputLocs,
                                   const solverMode& sMode)
{
    if (sD.time != lastTime) {
        updateLocalCache(inputs, sD, sMode);
    }
    double voltage = inputs[voltageInLocation];
    double tv = 0.0;
    if (voltage < Vpqmin) {
        tv = trigVVlow;
    } else if (voltage > Vpqmax) {
        tv = trigVVhigh;
    }

    md.assignCheckCol(PoutLocation,
                      inputLocs[voltageInLocation],
                      2.0 * voltage * Yp + Ip + 2.0 * voltage * P * tv);

    if (opFlags[use_power_factor_flag]) {
        if (pfq < 1000.0) {
            md.assignCheckCol(QoutLocation,
                              inputLocs[voltageInLocation],
                              2.0 * voltage * Yq + Iq + 2.0 * voltage * P * pfq * tv);
        } else {
            md.assignCheckCol(QoutLocation,
                              inputLocs[voltageInLocation],
                              2.0 * voltage * Yq + Iq + 2.0 * voltage * Q * tv);
        }
    } else {
        md.assignCheckCol(QoutLocation,
                          inputLocs[voltageInLocation],
                          2.0 * voltage * Yq + Iq + 2.0 * voltage * Q * tv);
    }
}

bool compareLoad(zipLoad* ld1, zipLoad* ld2, bool /*printDiff*/)
{
    bool cmp = true;

    if ((ld1->opFlags.to_ullong() & flagMask) != (ld2->opFlags.to_ullong() & flagMask)) {
        cmp = false;
    }
    if (std::abs(ld1->P - ld2->P) > 0.00001) {
        cmp = false;
    }

    if (std::abs(ld1->Q - ld2->Q) > 0.00001) {
        cmp = false;
    }
    if (std::abs(ld1->pfq - ld2->pfq) > 0.00001) {
        cmp = false;
    }
    if (std::abs(ld1->Ip - ld2->Ip) > 0.00001) {
        cmp = false;
    }

    if (std::abs(ld1->Iq - ld2->Iq) > 0.00001) {
        cmp = false;
    }
    if (std::abs(ld1->Yp - ld2->Yp) > 0.00001) {
        cmp = false;
    }

    if (std::abs(ld1->Yq - ld2->Yq) > 0.00001) {
        cmp = false;
    }
    return cmp;
}

}  // namespace griddyn

