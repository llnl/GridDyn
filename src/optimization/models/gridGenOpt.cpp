/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "gridGenOpt.h"

#include "../optObjectFactory.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridBusOpt.h"
#include "griddyn/Generator.h"
#include "utilities/MatrixData.hpp"
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

    int getPolynomialOrderLimit(const OptimizationMode& optimizationMode)
    {
        switch (optimizationMode.linMode) {
            case LinearityMode::LINEAR:
                return 1;
            case LinearityMode::QUADRATIC:
                return 2;
            default:
                return kBigINT;
        }
    }

    double evaluatePolynomialCost(const std::vector<double>& coefficients,
                                  double value,
                                  double period,
                                  int maxOrder)
    {
        double cost = 0.0;
        int coefficientIndex = 0;
        for (const auto coefficient : coefficients) {
            cost += coefficient * pow(value, coefficientIndex) * period;
            ++coefficientIndex;
            if (coefficientIndex > maxOrder) {
                break;
            }
        }
        return cost;
    }

    double evaluatePolynomialDerivative(const std::vector<double>& coefficients,
                                        double value,
                                        double period,
                                        std::size_t maxOrder)
    {
        const auto coefficientCount = std::min(coefficients.size(), maxOrder + 1U);
        double derivative = 0.0;
        for (std::size_t coefficientIndex = 1; coefficientIndex < coefficientCount;
             ++coefficientIndex) {
            derivative += static_cast<double>(coefficientIndex) * coefficients[coefficientIndex] *
                pow(value, coefficientIndex - 1U) * period;
        }
        return derivative;
    }

    double coefficientOrZero(const std::vector<double>& coefficients, std::size_t index)
    {
        return (index < coefficients.size()) ? coefficients[index] : 0.0;
    }
}  // namespace

static OptObjectFactory<GridGenOpt, Generator> gOpgen("basic", "gen", 0, true);

GridGenOpt::GridGenOpt(const std::string& objName): GridOptObject(objName), bus(nullptr) {}

GridGenOpt::GridGenOpt(CoreObject* obj, const std::string& objName):
    GridOptObject(objName), gen(dynamic_cast<Generator*>(obj)), bus(nullptr)
{
    if (gen != nullptr) {
        if (getName().empty()) {
            setName(gen->getName());
        }
        setUserID(gen->getUserID());
    }
}

CoreObject* GridGenOpt::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<GridGenOpt, GridOptObject>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->m_heatRate = m_heatRate;
    nobj->Pcoeff = Pcoeff;
    nobj->Qcoeff = Qcoeff;
    nobj->m_penaltyCost = m_penaltyCost;
    nobj->m_fuelCost = m_fuelCost;
    nobj->m_forecast = m_forecast;

    return nobj;
}

CoreObject* GridGenOpt::sourceObject() const
{
    return gen;
}

void GridGenOpt::add(CoreObject* obj)
{
    if (dynamic_cast<Generator*>(obj) != nullptr) {
        gen = static_cast<Generator*>(obj);
        setName(gen->getName());
        setUserID(gen->getUserID());
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void GridGenOpt::dynObjectInitializeA(std::uint32_t /*flags*/)
{
    bus = static_cast<GridBusOpt*>(getParent()->find("bus"));
}

void GridGenOpt::loadSizes(const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    optimizationOffsets.reset();
    switch (oMode.flowMode) {
        case FlowModel::NONE:
        case FlowModel::TRANSPORT:
        case FlowModel::DC:
            optimizationOffsets.local.genSize = 1;
            break;
        case FlowModel::AC:
            optimizationOffsets.local.genSize = 1;
            optimizationOffsets.local.qSize = 1;
            break;
    }
    optimizationOffsets.localLoad(true);
}

void GridGenOpt::setValues(const OptimizationData& optimizationData, const OptimizationMode& oMode)
{
    if ((gen == nullptr) || optimizationData.empty()) {
        return;
    }

    const auto& optimizationOffsets = offsets.getOffsets(oMode);
    if ((optimizationOffsets.gOffset != kNullLocation) &&
        ((optimizationData.valueSize == 0) ||
         (optimizationOffsets.gOffset < optimizationData.valueSize))) {
        const double realPowerTarget = optimizationData.val[optimizationOffsets.gOffset];
        // Committing an OPF solution updates both the present generator output
        // and its target set point.  The optimizer Pg is the positive
        // generation magnitude and the physical Generator stores that same
        // magnitude in P; Generator::getRealPower() exposes the network
        // injection with GridDyn's negative output sign convention.
        gen->set("p", realPowerTarget);
        gen->set("pset", realPowerTarget);
    }

    if (isAC(oMode) && (optimizationOffsets.qOffset != kNullLocation) &&
        ((optimizationData.valueSize == 0) ||
         (optimizationOffsets.qOffset < optimizationData.valueSize))) {
        gen->set("q", optimizationData.val[optimizationOffsets.qOffset]);
    }
}

// for saving the state
void GridGenOpt::guessState(double /*time*/, double val[], const OptimizationMode& oMode)
{
    if ((gen == nullptr) || (val == nullptr)) {
        return;
    }
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (optimizationOffsets.gOffset != kNullLocation) {
        // Seed OPF from the current physical dispatch.  The optimization
        // variable remains owned by the optimizer; the generator is only the
        // single source for the initial operating point.  The optimizer uses
        // positive generation magnitudes, whereas getRealPower() is the
        // signed network injection returned by GridDyn.
        val[optimizationOffsets.gOffset] = -gen->getRealPower();
    }
    if (isAC(oMode) && (optimizationOffsets.qOffset != kNullLocation)) {
        const double qGuess = gen->get("q");
        if (qGuess != kNullVal) {
            val[optimizationOffsets.qOffset] = qGuess;
        }
    }
}

void GridGenOpt::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    if (sdata == nullptr) {
        return;
    }
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if ((oMode.allowInteger) && (optimizationOffsets.intOffset != kNullLocation)) {
        for (index_t variableIndex = 0; variableIndex < optimizationOffsets.local.intSize;
             ++variableIndex) {
            sdata[optimizationOffsets.intOffset + variableIndex] = INTEGER_OBJECTIVE_VARIABLE;
        }
    }
}

void GridGenOpt::getTols(double /*tols*/[], const OptimizationMode& /* oMode */) {}

void GridGenOpt::valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (gen == nullptr) {
        return;
    }
    // Physical operating limits have one owner: the attached Generator.  This
    // makes OPF see the same time-varying limits as power-flow controls.
    upperLimit[optimizationOffsets.gOffset] = gen->getPmax(time);
    lowerLimit[optimizationOffsets.gOffset] = gen->getPmin(time);
    if (isAC(oMode)) {
        const double qUpper = gen->getQmax(time);
        const double qLower = gen->getQmin(time);
        upperLimit[optimizationOffsets.qOffset] = qUpper;
        lowerLimit[optimizationOffsets.qOffset] = qLower;
    }
}

void GridGenOpt::linearObj(const OptimizationData& /* of */,
                           vectData<double>& linObj,
                           const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        linObj.assign(optimizationOffsets.gOffset, coefficientOrZero(Pcoeff, 1) * oMode.period);
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            linObj.assign(optimizationOffsets.qOffset, coefficientOrZero(Qcoeff, 1) * oMode.period);
        }
    }
}
void GridGenOpt::quadraticObj(const OptimizationData& /* of */,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        linObj.assign(optimizationOffsets.gOffset, coefficientOrZero(Pcoeff, 1) * oMode.period);
        if (Pcoeff.size() >= 3) {
            quadObj.assign(optimizationOffsets.gOffset, Pcoeff[2] * oMode.period);
        }
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            linObj.assign(optimizationOffsets.qOffset, coefficientOrZero(Qcoeff, 1) * oMode.period);
            if (Qcoeff.size() >= 3) {
                quadObj.assign(optimizationOffsets.qOffset, Qcoeff[2] * oMode.period);
            }
        }
    }
}

double GridGenOpt::objValue(const OptimizationData& optimizationData, const OptimizationMode& oMode)
{
    double cost = 0;
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    const double pValue = optimizationData.val[optimizationOffsets.gOffset];
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        const int orderLimit = getPolynomialOrderLimit(oMode);
        cost += evaluatePolynomialCost(Pcoeff, pValue, oMode.period, orderLimit);
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            const double qValue = optimizationData.val[optimizationOffsets.qOffset];
            cost += evaluatePolynomialCost(Qcoeff, qValue, oMode.period, orderLimit);
        }
    }
    return cost;
}

void GridGenOpt::gradient(const OptimizationData& optimizationData,
                          double deriv[],
                          const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    const double pValue = optimizationData.val[optimizationOffsets.gOffset];
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        const auto orderLimit = static_cast<std::size_t>(getPolynomialOrderLimit(oMode));
        deriv[optimizationOffsets.gOffset] =
            evaluatePolynomialDerivative(Pcoeff, pValue, oMode.period, orderLimit);
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            const double qValue = optimizationData.val[optimizationOffsets.qOffset];
            deriv[optimizationOffsets.qOffset] =
                evaluatePolynomialDerivative(Qcoeff, qValue, oMode.period, orderLimit);
        }
    }
}

void GridGenOpt::jacobianElements(const OptimizationData& optimizationData,
                                  MatrixData<double>& matrixDataRef,
                                  const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    const double pValue = optimizationData.val[optimizationOffsets.gOffset];
    if (optFlags[PIECEWISE_LINEAR_COST]) {
    } else {
        const auto orderLimit = static_cast<std::size_t>(getPolynomialOrderLimit(oMode));
        const double pDerivative =
            evaluatePolynomialDerivative(Pcoeff, pValue, oMode.period, orderLimit);
        if (pDerivative != 0) {
            matrixDataRef.assign(optimizationOffsets.gOffset,
                                 optimizationOffsets.gOffset,
                                 pDerivative);
        }
        if ((!(Qcoeff.empty())) && (isAC(oMode))) {
            const double qValue = optimizationData.val[optimizationOffsets.qOffset];
            const double qDerivative =
                evaluatePolynomialDerivative(Qcoeff, qValue, oMode.period, orderLimit);
            if (qDerivative != 0) {
                matrixDataRef.assign(optimizationOffsets.qOffset,
                                     optimizationOffsets.qOffset,
                                     qDerivative);
            }
        }
    }
}

void GridGenOpt::getConstraints(const OptimizationData& /* of */,
                                MatrixData<double>& /*cons*/,
                                double /*upperLimit*/[],
                                double /*lowerLimit*/[],
                                const OptimizationMode& /* oMode */)
{
}

void GridGenOpt::constraintValue(const OptimizationData& /* of */,
                                 double /*cVals*/[],
                                 const OptimizationMode& /* oMode */)
{
}

void GridGenOpt::constraintJacobianElements(const OptimizationData& /* of */,
                                            MatrixData<double>& /*md*/,
                                            const OptimizationMode& /* oMode */)
{
}

void GridGenOpt::getObjectiveNames(stringVec& objectiveNames,
                                   const OptimizationMode& oMode,
                                   const std::string& prefix)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (objectiveNames.size() <= static_cast<size_t>(optimizationOffsets.gOffset)) {
        objectiveNames.resize(static_cast<size_t>(optimizationOffsets.gOffset) + 1);
    }
    objectiveNames[optimizationOffsets.gOffset] = prefix + getName() + ":PGen";
    if (isAC(oMode)) {
        if (objectiveNames.size() <= static_cast<size_t>(optimizationOffsets.qOffset)) {
            objectiveNames.resize(static_cast<size_t>(optimizationOffsets.qOffset) + 1);
        }
        objectiveNames[optimizationOffsets.qOffset] = prefix + getName() + ":QGen";
    }
}

// set properties
void GridGenOpt::set(std::string_view param, std::string_view val)
{
    if (param == "#") {
    } else {
        GridOptObject::set(param, val);
    }
}

void GridGenOpt::set(std::string_view param, double val, units::unit unitType)
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
        Pcoeff[1] = convert(val, unitType, currency / puMW / hr, gen->getRoot()->get("basepower"));
    } else if ((param == "quadraticp") || (param == "quadp") || (param == "quadratic") ||
               (param == "quad")) {
        if (Pcoeff.size() < 3) {
            Pcoeff.resize(3);
        }
        Pcoeff[2] =
            convert(val, unitType, currency / (puMW.pow(2)) / hr, gen->getRoot()->get("basepower"));
    } else if (param == "constantq") {
        if (Qcoeff.empty()) {
            Qcoeff.resize(1);
        }
        Qcoeff[0] = val;
    } else if (param == "linearq") {
        if (Qcoeff.size() < 2) {
            Qcoeff.resize(2);
        }
        Qcoeff[1] = convert(val, unitType, currency / puMW / hr, gen->getRoot()->get("basepower"));
    } else if ((param == "quadraticq") || (param == "quadq")) {
        if (Qcoeff.size() < 3) {
            Qcoeff.resize(3);
        }
        Qcoeff[2] =
            convert(val, unitType, currency / (puMW.pow(2)) / hr, gen->getRoot()->get("basepower"));
    } else if ((param == "penalty_cost") || (param == "penalty")) {
        m_penaltyCost =
            convert(val, unitType, currency / puMW / hr, gen->getRoot()->get("basepower"));
    } else if ((param == "pmax") || (param == "pmin")) {
        if (gen != nullptr) {
            gen->set(param, val, unitType);
        }
    } else if (param == "forecast") {
        m_forecast = convert(val, unitType, puMW, gen->getRoot()->get("basepower"));
    } else {
        GridOptObject::set(param, val, unitType);
    }
}

double GridGenOpt::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "#") {
    } else {
        val = GridOptObject::get(param, unitType);
    }
    return val;
}

void GridGenOpt::loadMatPowerCostCoeff(std::vector<double> coeff, int powerMode, int costModel)
{
    const auto basePower = (gen != nullptr) ? gen->getRoot()->get("basepower") : 1.0;
    if (costModel == 2) {
        // MATPOWER lists polynomial terms highest-to-lowest in MW/MVAr.  The
        // optimizer uses per-unit injections and constant-first coefficients.
        std::reverse(coeff.begin(), coeff.end());
        double scale = 1.0;
        for (auto& coefficient : coeff) {
            coefficient *= scale;
            scale *= basePower;
        }
    } else if (costModel == 1) {
        // Preserve the cost ordinate, but express breakpoint power in per unit.
        for (std::size_t index = 0; index < coeff.size(); index += 2) {
            coeff[index] /= basePower;
        }
        optFlags.set(PIECEWISE_LINEAR_COST);
    }
    if (powerMode == 0) {
        Pcoeff = coeff;
    } else {
        Qcoeff = coeff;
    }
}

GridOptObject* GridGenOpt::getBus(index_t /*index*/) const
{
    return bus;
}

GridOptObject* GridGenOpt::getArea(index_t index) const
{
    return bus->getArea(index);
}

}  // namespace griddyn
