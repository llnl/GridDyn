/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Load.h"

#include "../gridBus.h"
#include "../measurement/objectGrabbers.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <complex>
#include <iostream>
#include <string>

namespace griddyn {
using units::convert;
using units::puMW;
using units::unit;

std::atomic<count_t> Load::loadCount(0);
Load::Load(const std::string& objName): gridSecondary(objName)
{
    constructionHelper();
}
Load::Load(double rP, double rQ, const std::string& objName): gridSecondary(objName), P(rP), Q(rQ)
{
    constructionHelper();
}

void Load::constructionHelper()
{
    // default values
    setUserID(++loadCount);
    updateName();
}

coreObject* Load::clone(coreObject* obj) const
{
    auto nobj = cloneBase<Load, gridSecondary>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->setLoad(P,
                  Q);  // use the set load function in case we are cloning from a basic object to a
                       // higher level object
    nobj->pfq = pfq;
    return nobj;
}

void Load::setLoad(double level, unit unitType)
{
    setP(convert(level, unitType, puMW, systemBasePower));
}

void Load::setLoad(double Plevel, double Qlevel, unit unitType)
{
    setP(convert(Plevel, unitType, puMW, systemBasePower));
    setQ(convert(Qlevel, unitType, puMW, systemBasePower));
}

static const stringVec locNumStrings{"p", "q", "pf"};

static const stringVec locStrStrings{

};

static const stringVec flagStrings{"usepowerfactor"};

void Load::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<Load, gridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void Load::setFlag(std::string_view flag, bool val)
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
    } else {
        gridSecondary::setFlag(flag, val);
    }
}

// set properties
void Load::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        gridSecondary::set(param, val);
    }
}

double Load::get(std::string_view param, unit unitType) const
{
    double val = kNullVal;
    if (param.length() == 1) {
        switch (param[0]) {
            case 'p':
                val = convert(P, puMW, unitType, systemBasePower, localBaseVoltage);
                break;
            case 'q':
                val = convert(Q, puMW, unitType, systemBasePower, localBaseVoltage);
                break;
            default:
                break;
        }
        return val;
    }

    if (param == "pf") {
        val = pfq;
    } else if (auto fptr = getObjectFunction(this, std::string{param}).first) {
        auto unit = getObjectFunction(this, std::string{param}).second;
        coreObject* tobj = const_cast<Load*>(this);
        val = convert(fptr(tobj), unit, unitType, systemBasePower, localBaseVoltage);
    } else {
        val = gridSecondary::get(param, unitType);
    }
    return val;
}

void Load::set(std::string_view param, double val, unit unitType)
{
    if (param.empty()) {
        return;
    }
    if (param.length() == 1) {
        switch (param.front()) {
            case 'p':
                setP(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
                break;
            case 'q':
                setQ(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
                break;
            default:
                throw(unrecognizedParameter(std::string{param}));
        }
        checkFaultChange();
        return;
    }
    if (param.empty()) {
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
        } else {
            gridSecondary::set(param, val, unitType);
        }
    } else if (param.back() == '*') {
        // load increments  allows a delta on the load through the set functions
        if (param == "p*") {
            P *= val;
            checkpfq();
        } else if (param == "q*") {
            Q *= val;
            updatepfq();
        } else {
            gridSecondary::set(param, val, unitType);
        }
    } else if (param == "load p") {
        setP(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
    } else if (param == "load q") {
        setQ(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
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
    } else if (param == "qratio") {
        pfq = val;
        opFlags.set(use_power_factor_flag);
    } else {
        gridSecondary::set(param, val, unitType);
    }
}

void Load::setP(double newP)
{
    P = newP;
    checkpfq();
    checkFaultChange();
}

void Load::setQ(double newQ)
{
    Q = newQ;
    updatepfq();
    checkFaultChange();
}

void Load::updatepfq()
{
    if (opFlags[use_power_factor_flag]) {
        pfq = (P == 0.0) ? kBigNum : Q / P;
    }
}

void Load::checkpfq()
{
    if (opFlags[use_power_factor_flag]) {
        if (pfq > 1000.0)  // if the pfq is screwy, recalculate, otherwise leave it the same.
        {
            if (P != 0.0) {
                pfq = Q / P;
            }
        }
    }
}

void Load::checkFaultChange()
{
    if ((opFlags[pFlow_initialized]) && (bus->getVoltage() < 0.05)) {
        alert(this, POTENTIAL_FAULT_CHANGE);
    }
}

double Load::getRealPower() const
{
    return P;
}
double Load::getReactivePower() const
{
    return Q;
}
double Load::getRealPower(const IOdata& /*inputs*/,
                          const stateData& /*sD*/,
                          const solverMode& /*sMode*/) const
{
    return getRealPower();
}

double Load::getReactivePower(const IOdata& /*inputs*/,
                              const stateData& /*sD*/,
                              const solverMode& /*sMode*/) const
{
    return getReactivePower();
}

double Load::getRealPower(const double /*V*/) const
{
    return getRealPower();
}
double Load::getReactivePower(double /*V*/) const
{
    return getReactivePower();
}
count_t Load::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 0;
}
}  // namespace griddyn
