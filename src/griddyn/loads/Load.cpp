/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Load.h"

#include "../GridBus.h"
#include "../measurement/ObjectGrabbers.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <complex>
#include <iostream>
#include <string>

namespace griddyn {
using units::convert;
using units::puMW;
using units::unit;

std::atomic<count_t> GridLoad::loadCount(0);
GridLoad::GridLoad(const std::string& objName): GridSecondary(objName)
{
    constructionHelper();
}
GridLoad::GridLoad(double rP, double rQ, const std::string& objName):
    GridSecondary(objName), P(rP), Q(rQ)
{
    constructionHelper();
}

void GridLoad::constructionHelper()
{
    // default values
    setUserID(++loadCount);
    updateName();
}

CoreObject* GridLoad::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<GridLoad, GridSecondary>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->setLoad(P,
                  Q);  // use the set load function in case we are cloning from a basic object to a
                       // higher level object
    nobj->pfq = pfq;
    return nobj;
}

void GridLoad::setLoad(double level, unit unitType)
{
    setP(convert(level, unitType, puMW, systemBasePower));
}

void GridLoad::setLoad(double Plevel, double Qlevel, unit unitType)
{
    setP(convert(Plevel, unitType, puMW, systemBasePower));
    setQ(convert(Qlevel, unitType, puMW, systemBasePower));
}

static const stringVec locNumStrings{"p", "q", "pf"};

static const stringVec locStrStrings{

};

static const stringVec flagStrings{"usepowerfactor"};

void GridLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<GridLoad, GridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void GridLoad::setFlag(std::string_view flag, bool val)
{
    if (flag == "usepowerfactor") {
        if (val) {
            if (!(opFlags[USE_POWER_FACTOR_FLAG])) {
                opFlags.set(USE_POWER_FACTOR_FLAG);
                updatepfq();
            }
        } else {
            opFlags.reset(USE_POWER_FACTOR_FLAG);
        }
    } else {
        GridSecondary::setFlag(flag, val);
    }
}

// set properties
void GridLoad::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        GridSecondary::set(param, val);
    }
}

double GridLoad::get(std::string_view param, unit unitType) const
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
    } else if (auto fptr = getObjectFunction(this, param).first) {
        auto unit = getObjectFunction(this, param).second;
        CoreObject* tobj = const_cast<GridLoad*>(this);
        val = convert(fptr(tobj), unit, unitType, systemBasePower, localBaseVoltage);
    } else {
        val = GridSecondary::get(param, unitType);
    }
    return val;
}

void GridLoad::set(std::string_view param, double val, unit unitType)
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
                throw(UnrecognizedParameter(param));
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
            GridSecondary::set(param, val, unitType);
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
            GridSecondary::set(param, val, unitType);
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
        opFlags.set(USE_POWER_FACTOR_FLAG);
    } else if (param == "qratio") {
        pfq = val;
        opFlags.set(USE_POWER_FACTOR_FLAG);
    } else {
        GridSecondary::set(param, val, unitType);
    }
}

void GridLoad::setP(double newP)
{
    P = newP;
    checkpfq();
    checkFaultChange();
}

void GridLoad::setQ(double newQ)
{
    Q = newQ;
    updatepfq();
    checkFaultChange();
}

void GridLoad::updatepfq()
{
    if (opFlags[USE_POWER_FACTOR_FLAG]) {
        pfq = (P == 0.0) ? kBigNum : Q / P;
    }
}

void GridLoad::checkpfq()
{
    if (opFlags[USE_POWER_FACTOR_FLAG]) {
        if (pfq > 1000.0)  // if the pfq is screwy, recalculate, otherwise leave it the same.
        {
            if (P != 0.0) {
                pfq = Q / P;
            }
        }
    }
}

void GridLoad::checkFaultChange()
{
    if ((opFlags[POWERFLOW_INITIALIZED]) && (bus->getVoltage() < 0.05)) {
        alert(this, POTENTIAL_FAULT_CHANGE);
    }
}

double GridLoad::getRealPower() const
{
    return P;
}
double GridLoad::getReactivePower() const
{
    return Q;
}
double GridLoad::getRealPower(const IOdata& /*inputs*/,
                              const StateData& /*sD*/,
                              const SolverMode& /*sMode*/) const
{
    return getRealPower();
}

double GridLoad::getReactivePower(const IOdata& /*inputs*/,
                                  const StateData& /*sD*/,
                                  const SolverMode& /*sMode*/) const
{
    return getReactivePower();
}

double GridLoad::getRealPower(const double /*V*/) const
{
    return getRealPower();
}
double GridLoad::getReactivePower(double /*V*/) const
{
    return getReactivePower();
}
count_t GridLoad::outputDependencyCount(index_t /*num*/, const SolverMode& /*sMode*/) const
{
    return 0;
}
}  // namespace griddyn
