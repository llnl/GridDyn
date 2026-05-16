/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridGenOpt.h"

#include "../optObjectFactory.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridBusOpt.h"
#include "griddyn/Generator.h"
#include "utilities/matrixData.hpp"
#include "utilities/vectData.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    bool parseIndexSuffix(std::string_view text, std::size_t& index)
    {
        if (text.empty()) {
            return false;
        }
        const auto* begin = text.data();
        const auto* end = begin + text.size();
        const auto result = std::from_chars(begin, end, index);
        return (result.ec == std::errc{}) && (result.ptr == end);
    }
}  // namespace

static optObjectFactory<gridGenOpt, Generator> opgen("basic", "gen", 0, true);

gridGenOpt::gridGenOpt(const std::string& objName): gridOptObject(objName) {}

gridGenOpt::gridGenOpt(coreObject* obj, const std::string& objName):
    gridOptObject(objName), gen(dynamic_cast<Generator*>(obj))
{
    if (gen != nullptr) {
        if (getName().empty()) {
            setName(gen->getName());
        }
        setUserID(gen->getUserID());
    }
}

coreObject* gridGenOpt::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<gridGenOpt, gridOptObject>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->m_heatRate = m_heatRate;
    nobj->Pcoeff = Pcoeff;
    nobj->Qcoeff = Qcoeff;
    nobj->m_penaltyCost = m_penaltyCost;
    nobj->m_fuelCost = m_fuelCost;
    nobj->m_forecast = m_forecast;
    nobj->m_Pmax = m_Pmax;
    nobj->m_Pmin = m_Pmin;
    nobj->systemBasePower = systemBasePower;
    nobj->mBase = mBase;

    return nobj;
}

void gridGenOpt::add(coreObject* obj)
{
    if (dynamic_cast<Generator*>(obj) != nullptr) {
        gen = static_cast<Generator*>(obj);
        setName(gen->getName());
        setUserID(gen->getUserID());
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void gridGenOpt::dynObjectInitializeA(std::uint32_t /*flags*/)
{
    bus = static_cast<gridBusOpt*>(getParent()->find("bus"));
}

void gridGenOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    oo.reset();
    switch (oMode.flowMode) {
        case FlowModel::NONE:
        case FlowModel::TRANSPORT:
        case FlowModel::DC:
            oo.local.genSize = 1;
            break;
        case FlowModel::AC:
            oo.local.genSize = 1;
            oo.local.qSize = 1;
            break;
    }
    oo.localLoad(true);
}

void gridGenOpt::setValues(const OptimizationData& /* of */, const OptimizationMode& /*oMode*/) {}

// for saving the state
void gridGenOpt::guessState(double /*time*/, double /*val*/[], const OptimizationMode& /*oMode*/)
{
    // OptimizationOffsets *oo = offsets.getOffsets (oMode);
}

void gridGenOpt::getVariableType(double /*sdata*/[], const OptimizationMode& /* oMode */) {}

void gridGenOpt::getTols(double /*tols*/[], const OptimizationMode& /* oMode */) {}

void gridGenOpt::valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    double Pup, Pdown;
    if (optFlags.test(LIMIT_OVERRIDE)) {
        if (m_Pmax < kHalfBigNum) {
            Pup = m_Pmax;
        } else {
            Pup = gen->getPmax(time);
        }
        if (m_Pmin > -kHalfBigNum) {
            Pdown = m_Pmin;
        } else {
            Pdown = gen->getPmin(time);
        }
    } else {
        Pup = gen->getPmax(time);
        Pdown = gen->getPmin(time);
    }
    upperLimit[oo.gOffset] = Pup;
    lowerLimit[oo.gOffset] = Pdown;
    if (isAC(oMode)) {
        double Qup = gen->getQmax(time);
        double Qdown = gen->getQmin(time);
        upperLimit[oo.qOffset] = Qup;
        lowerLimit[oo.qOffset] = Qdown;
    }
}

void gridGenOpt::linearObj(const OptimizationData& /* of */,
                           vectData<double>& linObj,
                           const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        linObj.assign(0, Pcoeff[0] * oMode.period);
        linObj.assign(oo.gOffset, Pcoeff[1] * oMode.period);
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            linObj.assign(0, Qcoeff[0] * oMode.period);
            linObj.assign(oo.qOffset, Qcoeff[1] * oMode.period);
        }
    }
}
void gridGenOpt::quadraticObj(const OptimizationData& /* of */,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        linObj.assign(0, Pcoeff[0] * oMode.period);
        linObj.assign(oo.gOffset, Pcoeff[1] * oMode.period);
        if (Pcoeff.size() >= 2) {
            quadObj.assign(oo.gOffset, Pcoeff[2] * oMode.period);
        }
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            linObj.assign(0, Qcoeff[0] * oMode.period);
            linObj.assign(oo.qOffset, Qcoeff[1] * oMode.period);
            if (Qcoeff.size() >= 2) {
                quadObj.assign(oo.qOffset, Qcoeff[2] * oMode.period);
            }
        }
    }
}

double gridGenOpt::objValue(const OptimizationData& of, const OptimizationMode& oMode)
{
    double cost = 0;
    auto& oo = offsets.getOffsets(oMode);
    double P = of.val[oo.gOffset];
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        int kk = 0;
        int kmax;
        switch (oMode.linMode) {
            case LinearityMode::LINEAR:
                kmax = 1;
                break;
            case LinearityMode::QUADRATIC:
                kmax = 2;
                break;
            default:
                kmax = kBigINT;
        }
        for (auto pc : Pcoeff) {
            cost += pc * pow(P, kk) * oMode.period;
            ++kk;
            if (kk > kmax) {
                break;
            }
        }
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            double Q = of.val[oo.qOffset];
            kk = 0;
            for (auto pc : Qcoeff) {
                cost += pc * pow(Q, kk) * oMode.period;
                ++kk;
                if (kk > kmax) {
                    break;
                }
            }
        }
    }
    return cost;
}

void gridGenOpt::gradient(const OptimizationData& of, double deriv[], const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    double P = of.val[oo.gOffset];
    double temp;
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        size_t kk = 0;
        size_t kmax;
        size_t klim;
        switch (oMode.linMode) {
            case LinearityMode::LINEAR:
                kmax = 1;
                break;
            case LinearityMode::QUADRATIC:
                kmax = 2;
                break;
            default:
                kmax = kBigINT;
        }
        klim = std::max(kmax, Pcoeff.size());
        temp = 0;
        for (kk = 1; kk < klim; ++kk) {
            temp += static_cast<double>(kk) * Pcoeff[kk] * pow(P, kk - 1) * oMode.period;
        }
        deriv[oo.gOffset] = temp;
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            double Q = of.val[oo.qOffset];
            klim = std::max(kmax, Qcoeff.size());
            temp = 0;
            for (kk = 1; kk < klim; ++kk) {
                temp += static_cast<double>(kk) * Qcoeff[kk] * pow(Q, kk - 1) * oMode.period;
            }
            deriv[oo.qOffset] = temp;
        }
    }
}
void gridGenOpt::jacobianElements(const OptimizationData& of,
                                  matrixData<double>& md,
                                  const OptimizationMode& oMode)
{
    auto& oo = offsets.getOffsets(oMode);
    double P = of.val[oo.gOffset];
    double temp;
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        size_t kk = 0;
        size_t kmax;
        size_t klim;
        switch (oMode.linMode) {
            case LinearityMode::LINEAR:
                kmax = 1;
                break;
            case LinearityMode::QUADRATIC:
                kmax = 2;
                break;
            default:
                kmax = kBigINT;
        }
        klim = std::max(kmax, Pcoeff.size());
        temp = 0;
        for (kk = 1; kk < klim; ++kk) {
            temp += static_cast<double>(kk) * Pcoeff[kk] * pow(P, kk - 1) * oMode.period;
        }
        if (temp != 0) {
            md.assign(oo.gOffset, oo.gOffset, temp);
        }
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            double Q = of.val[oo.qOffset];
            klim = std::max(kmax, Qcoeff.size());
            temp = 0;
            for (kk = 1; kk < klim; ++kk) {
                temp += static_cast<double>(kk) * Qcoeff[kk] * pow(Q, kk - 1) * oMode.period;
            }
            if (temp != 0) {
                md.assign(oo.qOffset, oo.qOffset, temp);
            }
        }
    }
}

void gridGenOpt::getConstraints(const OptimizationData& /* of */,
                                matrixData<double>& /*cons*/,
                                double /*upperLimit*/[],
                                double /*lowerLimit*/[],
                                const OptimizationMode& /* oMode */)
{
}

void gridGenOpt::constraintValue(const OptimizationData& /* of */,
                                 double /*cVals*/[],
                                 const OptimizationMode& /* oMode */)
{
}

void gridGenOpt::constraintJacobianElements(const OptimizationData& /* of */,
                                            matrixData<double>& /*md*/,
                                            const OptimizationMode& /* oMode */)
{
}

void gridGenOpt::getObjName(stringVec& objNames,
                            const OptimizationMode& oMode,
                            const std::string& prefix)
{
    auto& oo = offsets.getOffsets(oMode);
    objNames[oo.gOffset] = prefix + getName() + ":PGen";
    if (isAC(oMode)) {
        objNames[oo.gOffset] = prefix + getName() + ":QGen";
    }
}

// set properties
void gridGenOpt::set(std::string_view param, std::string_view val)
{
    if (param == "#") {
    } else {
        gridOptObject::set(param, val);
    }
}

void gridGenOpt::set(std::string_view param, double val, units::unit unitType)
{
    using units::convert;
    using units::currency;
    using units::hr;
    using units::puMW;
    if (param.empty()) {
        return;
    }
    if (param[0] == 'p') {
        std::size_t num;
        if (parseIndexSuffix(param.substr(1), num)) {
            if (num > Pcoeff.size()) {
                Pcoeff.resize(num + 1);
            }
            Pcoeff[num] = val;
            return;
        }
    } else if (param[0] == 'q') {
        std::size_t num;
        if (parseIndexSuffix(param.substr(1), num)) {
            if (num > Qcoeff.size()) {
                Qcoeff.resize(num + 1);
            }
            Qcoeff[num] = val;
            return;
        }
    }

    if (param == "heatrate") {
        m_heatRate = val;
    } else if ((param == "fuel") || (param == "fuelcost")) {
        m_fuelCost = val;
    } else if ((param == "constantp") || (param == "constant")) {
        if (Pcoeff.empty()) {
            Pcoeff.resize(1);
        }
        Pcoeff[0] = val;
    } else if ((param == "linearp") || (param == "linear")) {
        if (Pcoeff.size() < 2) {
            Pcoeff.resize(2);
        }
        Pcoeff[1] = convert(val, unitType, currency / puMW / hr, systemBasePower);
    } else if ((param == "quadraticp") || (param == "quadp") || (param == "quadratic") ||
               (param == "quad")) {
        if (Pcoeff.size() < 3) {
            Pcoeff.resize(3);
        }
        Pcoeff[2] = convert(val, unitType, currency / (puMW.pow(2)) / hr, systemBasePower);
    } else if (param == "constantq") {
        if (Qcoeff.empty()) {
            Qcoeff.resize(1);
        }
        Qcoeff[0] = val;
    } else if (param == "linearq") {
        if (Qcoeff.size() < 2) {
            Qcoeff.resize(2);
        }
        Qcoeff[1] = convert(val, unitType, currency / puMW / hr, systemBasePower);
    } else if ((param == "quadraticq") || (param == "quadq")) {
        if (Qcoeff.size() < 3) {
            Qcoeff.resize(3);
        }
        Qcoeff[1] = convert(val, unitType, currency / (puMW.pow(2)) / hr, systemBasePower);
    } else if ((param == "penalty_cost") || (param == "penalty")) {
        m_penaltyCost = convert(val, unitType, currency / puMW / hr, systemBasePower);
    } else if (param == "pmax") {
        m_Pmax = convert(val, unitType, puMW, systemBasePower);
        optFlags.set(LIMIT_OVERRIDE);
    } else if (param == "pmin") {
        m_Pmin = convert(val, unitType, puMW, systemBasePower);
        optFlags.set(LIMIT_OVERRIDE);
    } else if (param == "forecast") {
        m_forecast = convert(val, unitType, puMW, systemBasePower);
    } else {
        gridOptObject::set(param, val, unitType);
    }
}

double gridGenOpt::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "#") {
    } else {
        val = gridOptObject::get(param, unitType);
    }
    return val;
}

void gridGenOpt::loadCostCoeff(std::vector<double> const& coeff, int mode)
{
    if (mode == 0) {
        Pcoeff = coeff;
    } else {
        Qcoeff = coeff;
    }
}

gridOptObject* gridGenOpt::getBus(index_t /*index*/) const
{
    return bus;
}

gridOptObject* gridGenOpt::getArea(index_t index) const
{
    return bus->getArea(index);
}

}  // namespace griddyn
