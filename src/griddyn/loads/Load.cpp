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
#include "core/ModelParameter.hpp"
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
GridLoad::GridLoad(double realPower, double reactivePower, const std::string& objName):
    GridSecondary(objName), P(realPower), Q(reactivePower)
{
    constructionHelper();
}

void GridLoad::constructionHelper()
{
    // default values
    setUserID(++loadCount);
    updateName();
}

const ModelDescription& GridLoad::getModelDescription() const
{
    static const auto description = ModelDescription::define<GridLoad>(
        "GridLoad",
        &GridSecondary::getModelDescription(),
        {parameter({"p", "load p"}, &GridLoad::P, puMW, "Real power at nominal voltage"),
         parameter({"q", "load q"}, &GridLoad::Q, puMW, "Reactive power at nominal voltage"),
         parameter({"qratio"}, &GridLoad::pfq, units::one, "Power-factor control target ratio"),
         flag({"fixedshunt"}, &GridLoad::fixedShunt, "Fixed-shunt classification"),
         flag({"usepowerfactor"},
              &GridLoad::opFlags,
              USE_POWER_FACTOR_FLAG,
              "Power-factor control enabled"),
         command({"pf", "powerfactor"}, units::one, "Configure power-factor control"),
         command({"p+"}, puMW, "Increment real power"),
         command({"q+"}, puMW, "Increment reactive power"),
         command({"p*"}, units::one, "Scale real power"),
         command({"q*"}, units::one, "Scale reactive power")});
    return description;
}

CoreObject* GridLoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<GridLoad, GridSecondary>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->setLoad(P,
                  Q);  // use the set load function in case we are cloning from a basic object to a
                       // higher level object
    nobj->pfq = pfq;
    nobj->fixedShunt = fixedShunt;
    return nobj;
}

void GridLoad::setLoad(double level, unit unitType)
{
    setP(convert(level, unitType, puMW, systemBasePower));
}

void GridLoad::setLoad(double plevel, double qlevel, unit unitType)
{
    setP(convert(plevel, unitType, puMW, systemBasePower));
    setQ(convert(qlevel, unitType, puMW, systemBasePower));
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
    } else if (!GridLoad::getModelDescription().trySetFlag(*this, flag, val)) {
        GridSecondary::setFlag(flag, val);
    }
}

// set properties
void GridLoad::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else if (!isSetHandled(GridLoad::getModelDescription().trySetLocal(*this, param, val))) {
        GridSecondary::set(param, val);
    }
}

double GridLoad::get(std::string_view param, unit unitType) const
{
    const auto* field = GridLoad::getModelDescription().findLocal(param);
    const auto canonicalName = field != nullptr ? field->name() : param;
    if (canonicalName == "p" || canonicalName == "q") {
        return convertPower(canonicalName == "p" ? P : Q, puMW, unitType);
    }
    if (const auto value = GridLoad::getModelDescription().tryGet(*this, param, unitType)) {
        return *value;
    }
    double val = kNullVal;
    if (param.length() == 1) {
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
    const auto* field = GridLoad::getModelDescription().findLocal(param);
    const auto canonicalName = field != nullptr ? field->name() : param;
    if (canonicalName == "p" || canonicalName == "q") {
        const auto power = convertPower(val, unitType, puMW);
        if (canonicalName == "p") {
            setP(power);
        } else {
            setQ(power);
        }
        return;
    }
    if (isSetHandled(GridLoad::getModelDescription().trySet(*this, param, val, unitType))) {
        if (canonicalName == "qratio") {
            opFlags.set(USE_POWER_FACTOR_FLAG);
        }
        return;
    }
    if (param.length() == 1) {
        throw(UnrecognizedParameter(param));
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
    } else if ((param == "pf") || (param == "powerfactor")) {
        if (val != 0.0) {
            if (std::abs(val) <= 1.0) {
                pfq = std::sqrt(1.0 - (val * val)) / val;
            } else {
                pfq = 0.0;
            }
        } else {
            pfq = kBigNum;
        }
        opFlags.set(USE_POWER_FACTOR_FLAG);
    } else {
        GridSecondary::set(param, val, unitType);
    }
}

double GridLoad::convertPower(double value, unit sourceUnit, unit targetUnit) const
{
    if (units::is_error(sourceUnit) || units::is_error(targetUnit) ||
        std::isnan(convert(1.0, sourceUnit, targetUnit, systemBasePower, localBaseVoltage))) {
        throw InvalidParameterValue("load power units");
    }
    return convert(value, sourceUnit, targetUnit, systemBasePower, localBaseVoltage);
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
