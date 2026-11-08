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
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridBusOpt.h"
#include "griddyn/Generator.h"
#include "utilities/MatrixData.hpp"
#include "utilities/vectData.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
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

    std::size_t piecewiseSegmentCount(const std::vector<double>& coefficients)
    {
        return (coefficients.size() >= 4U) ? (coefficients.size() / 2U) - 1U : 0U;
    }

    double piecewiseSlope(const std::vector<double>& coefficients, std::size_t segment)
    {
        const auto left = 2U * segment;
        return (coefficients[left + 3U] - coefficients[left + 1U]) /
            (coefficients[left + 2U] - coefficients[left]);
    }

    bool isConvexPiecewise(const std::vector<double>& coefficients)
    {
        const auto segmentCount = piecewiseSegmentCount(coefficients);
        if (segmentCount < 2U) {
            return true;
        }
        double previousSlope = piecewiseSlope(coefficients, 0U);
        for (std::size_t segment = 1U; segment < segmentCount; ++segment) {
            const double slope = piecewiseSlope(coefficients, segment);
            const double tolerance =
                1.0e-12 * (std::max)({1.0, std::abs(previousSlope), std::abs(slope)});
            if (slope < previousSlope - tolerance) {
                return false;
            }
            previousSlope = slope;
        }
        return true;
    }

    std::size_t piecewiseSegmentForPower(const std::vector<double>& coefficients, double power)
    {
        const auto segmentCount = piecewiseSegmentCount(coefficients);
        for (std::size_t segment = 0U; segment + 1U < segmentCount; ++segment) {
            if (power <= coefficients[(2U * segment) + 2U]) {
                return segment;
            }
        }
        return segmentCount - 1U;
    }

    double piecewiseCostAt(const std::vector<double>& coefficients, double power)
    {
        const auto segment = piecewiseSegmentForPower(coefficients, power);
        return coefficients[(2U * segment) + 1U] +
            (piecewiseSlope(coefficients, segment) * (power - coefficients[2U * segment]));
    }
}  // namespace

static OptObjectFactory<GridGenOpt, Generator> gOpgen("basic", "gen", 0, true);

const ModelDescription& GridGenOpt::getModelDescription() const
{
    static const auto description = ModelDescription::define<GridGenOpt>(
        "GridGenOpt",
        &GridOptObject::getModelDescription(),
        {parameter({"heatrate"}, &GridGenOpt::m_heatRate, units::defunit, "Generator heat rate"),
         parameter({"fuel", "fuelcost"}, &GridGenOpt::m_fuelCost, units::defunit, "Fuel cost"),
         command({"penalty_cost", "penalty"},
                 units::currency / units::puMW / units::hr,
                 "Power penalty cost per hour"),
         command({"forecast"}, units::puMW, "Forecast real power on the attached system base"),
         indexedCommand({"p", "q"},
                        units::defunit,
                        "Cost coefficient in native per-unit variables"),
         command({"constantp", "constant", "constantq"},
                 units::defunit,
                 "Constant cost coefficient"),
         command({"linearp", "linear", "linearq"},
                 units::currency / units::puMW / units::hr,
                 "Linear cost coefficient"),
         command({"quadraticp", "quadp", "quadratic", "quad", "quadraticq", "quadq"},
                 units::currency / units::puMW.pow(2) / units::hr,
                 "Quadratic cost coefficient"),
         command({"pmax", "pmin"}, units::puMW, "Set the attached generator's real power limit")});
    return description;
}

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
    nobj->activeCostCurve = activeCostCurve;
    nobj->reactiveCostCurve = reactiveCostCurve;
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
    // Cost data is retained on the optimization adapter even for generators
    // that are out of service, but those units must not contribute dispatch
    // variables to the OPF model.
    if ((gen == nullptr) || !gen->isEnabled()) {
        optimizationOffsets.localLoad(true);
        return;
    }
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
    if (usesActivePiecewiseCost(oMode)) {
        const auto segments = static_cast<count_t>(piecewiseSegmentCount(Pcoeff));
        if (hasNonconvexActiveCost()) {
            // One cost ordinate, one interpolation fraction and one binary
            // selector per segment. Three equalities and t_j <= z_j rows
            // force exactly one segment to represent the generator output.
            optimizationOffsets.local.contSize = 1 + segments;
            optimizationOffsets.local.intSize = segments;
            optimizationOffsets.local.constraintsSize = 3 + segments;
        } else {
            // A convex PWL cost is the maximum of its segment lines.
            optimizationOffsets.local.contSize = 1;
            optimizationOffsets.local.constraintsSize = segments;
        }
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
    if (usesActivePiecewiseCost(oMode) && (optimizationOffsets.contOffset != kNullLocation)) {
        const double power = val[optimizationOffsets.gOffset];
        val[optimizationOffsets.contOffset] = piecewiseCostAt(Pcoeff, power);
        if (hasNonconvexActiveCost()) {
            const auto segment = piecewiseSegmentForPower(Pcoeff, power);
            const auto segmentCount = piecewiseSegmentCount(Pcoeff);
            for (std::size_t index = 0U; index < segmentCount; ++index) {
                val[optimizationOffsets.contOffset + 1 + static_cast<index_t>(index)] = 0.0;
                val[optimizationOffsets.intOffset + static_cast<index_t>(index)] = 0.0;
            }
            const double width = Pcoeff[(2U * segment) + 2U] - Pcoeff[2U * segment];
            val[optimizationOffsets.contOffset + 1 + static_cast<index_t>(segment)] =
                (std::max)(0.0, (std::min)(1.0, (power - Pcoeff[2U * segment]) / width));
            val[optimizationOffsets.intOffset + static_cast<index_t>(segment)] = 1.0;
        }
    }
}

void GridGenOpt::getVariableType(double sdata[], const OptimizationMode& oMode)
{
    if (sdata == nullptr) {
        return;
    }
    GridOptObject::getVariableType(sdata, oMode);
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (usesActivePiecewiseCost(oMode) && hasNonconvexActiveCost() &&
        (optimizationOffsets.intOffset != kNullLocation)) {
        for (index_t variableIndex = 0; variableIndex < optimizationOffsets.local.intSize;
             ++variableIndex) {
            sdata[optimizationOffsets.intOffset + variableIndex] = BINARY_OBJECTIVE_VARIABLE;
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
    if ((gen == nullptr) || (optimizationOffsets.gOffset == kNullLocation)) {
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
    if (usesActivePiecewiseCost(oMode) && hasNonconvexActiveCost()) {
        const auto segmentCount = piecewiseSegmentCount(Pcoeff);
        for (std::size_t segment = 0U; segment < segmentCount; ++segment) {
            const auto fraction =
                optimizationOffsets.contOffset + 1 + static_cast<index_t>(segment);
            const auto selector = optimizationOffsets.intOffset + static_cast<index_t>(segment);
            lowerLimit[fraction] = lowerLimit[selector] = 0.0;
            upperLimit[fraction] = upperLimit[selector] = 1.0;
        }
    }
}

void GridGenOpt::linearObj(const OptimizationData& /* of */,
                           vectData<double>& linObj,
                           const OptimizationMode& oMode)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (optimizationOffsets.gOffset == kNullLocation) {
        return;
    }
    if (usesActivePiecewiseCost(oMode)) {
        linObj.assign(optimizationOffsets.contOffset, oMode.period);
    } else if (activeCostCurve.model != 1) {
        linObj.assign(optimizationOffsets.gOffset, coefficientOrZero(Pcoeff, 1) * oMode.period);
        if (!Qcoeff.empty() && isAC(oMode) && (reactiveCostCurve.model != 1)) {
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
    if (optimizationOffsets.gOffset == kNullLocation) {
        return;
    }
    if (usesActivePiecewiseCost(oMode)) {
        linObj.assign(optimizationOffsets.contOffset, oMode.period);
    } else if (activeCostCurve.model != 1) {
        linObj.assign(optimizationOffsets.gOffset, coefficientOrZero(Pcoeff, 1) * oMode.period);
        if (Pcoeff.size() >= 3) {
            quadObj.assign(optimizationOffsets.gOffset, Pcoeff[2] * oMode.period);
        }
        if (!Qcoeff.empty() && isAC(oMode) && (reactiveCostCurve.model != 1)) {
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
    if (optimizationOffsets.gOffset == kNullLocation) {
        return cost;
    }
    const double pValue = optimizationData.val[optimizationOffsets.gOffset];
    if (usesActivePiecewiseCost(oMode)) {
        cost += optimizationData.val[optimizationOffsets.contOffset] * oMode.period;
    } else if (activeCostCurve.model != 1) {
        const int orderLimit = getPolynomialOrderLimit(oMode);
        cost += evaluatePolynomialCost(Pcoeff, pValue, oMode.period, orderLimit);
        if (!Qcoeff.empty() && isAC(oMode) && (reactiveCostCurve.model != 1)) {
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
    if (optimizationOffsets.gOffset == kNullLocation) {
        return;
    }
    const double pValue = optimizationData.val[optimizationOffsets.gOffset];
    if (usesActivePiecewiseCost(oMode)) {
        deriv[optimizationOffsets.contOffset] = oMode.period;
    } else if (activeCostCurve.model != 1) {
        const auto orderLimit = static_cast<std::size_t>(getPolynomialOrderLimit(oMode));
        deriv[optimizationOffsets.gOffset] =
            evaluatePolynomialDerivative(Pcoeff, pValue, oMode.period, orderLimit);
        if (!Qcoeff.empty() && isAC(oMode) && (reactiveCostCurve.model != 1)) {
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
    if (optimizationOffsets.gOffset == kNullLocation) {
        return;
    }
    const double pValue = optimizationData.val[optimizationOffsets.gOffset];
    if (usesActivePiecewiseCost(oMode)) {
        matrixDataRef.assign(optimizationOffsets.contOffset,
                             optimizationOffsets.contOffset,
                             oMode.period);
    } else if (activeCostCurve.model != 1) {
        const auto orderLimit = static_cast<std::size_t>(getPolynomialOrderLimit(oMode));
        const double pDerivative =
            evaluatePolynomialDerivative(Pcoeff, pValue, oMode.period, orderLimit);
        if (pDerivative != 0) {
            matrixDataRef.assign(optimizationOffsets.gOffset,
                                 optimizationOffsets.gOffset,
                                 pDerivative);
        }
        if (!Qcoeff.empty() && isAC(oMode) && (reactiveCostCurve.model != 1)) {
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
                                MatrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const OptimizationMode& oMode)
{
    if (!usesActivePiecewiseCost(oMode)) {
        return;
    }
    const auto& off = offsets.getOffsets(oMode);
    const auto count = piecewiseSegmentCount(Pcoeff);
    const auto row = off.constraintOffset;
    const auto power = off.gOffset;
    const auto cost = off.contOffset;
    if (!hasNonconvexActiveCost()) {
        for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
            const auto coefficientIndex = 2U * segmentIndex;
            const auto line = row + static_cast<index_t>(segmentIndex);
            const double slope = piecewiseSlope(Pcoeff, segmentIndex);
            lowerLimit[line] = Pcoeff[coefficientIndex + 1U] - (slope * Pcoeff[coefficientIndex]);
            upperLimit[line] = kBigNum;
            cons.assign(line, cost, 1.0);
            cons.assign(line, power, -slope);
        }
        return;
    }
    lowerLimit[row] = upperLimit[row] = 1.0;
    lowerLimit[row + 1] = upperLimit[row + 1] = 0.0;
    lowerLimit[row + 2] = upperLimit[row + 2] = 0.0;
    cons.assign(row + 1, power, 1.0);
    cons.assign(row + 2, cost, 1.0);
    for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
        const auto coefficientIndex = 2U * segmentIndex;
        const auto fraction = off.contOffset + 1 + static_cast<index_t>(segmentIndex);
        const auto selector = off.intOffset + static_cast<index_t>(segmentIndex);
        const auto selectionRow = row + 3 + static_cast<index_t>(segmentIndex);
        const double powerAtSegmentStart = Pcoeff[coefficientIndex];
        const double powerWidth = Pcoeff[coefficientIndex + 2U] - powerAtSegmentStart;
        const double costAtSegmentStart = Pcoeff[coefficientIndex + 1U];
        const double costWidth = Pcoeff[coefficientIndex + 3U] - costAtSegmentStart;
        cons.assign(row, selector, 1.0);
        cons.assign(row + 1, selector, -powerAtSegmentStart);
        cons.assign(row + 1, fraction, -powerWidth);
        cons.assign(row + 2, selector, -costAtSegmentStart);
        cons.assign(row + 2, fraction, -costWidth);
        cons.assign(selectionRow, fraction, 1.0);
        cons.assign(selectionRow, selector, -1.0);
        lowerLimit[selectionRow] = -kBigNum;
        upperLimit[selectionRow] = 0.0;
    }
}

void GridGenOpt::constraintValue(const OptimizationData& optimizationData,
                                 double cVals[],
                                 const OptimizationMode& oMode)
{
    if (!usesActivePiecewiseCost(oMode)) {
        return;
    }
    const auto& off = offsets.getOffsets(oMode);
    const auto count = piecewiseSegmentCount(Pcoeff);
    const auto row = off.constraintOffset;
    const double power = optimizationData.val[off.gOffset];
    const double cost = optimizationData.val[off.contOffset];
    if (!hasNonconvexActiveCost()) {
        for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
            cVals[row + static_cast<index_t>(segmentIndex)] =
                cost - (piecewiseSlope(Pcoeff, segmentIndex) * power);
        }
        return;
    }
    cVals[row] = 0.0;
    cVals[row + 1] = power;
    cVals[row + 2] = cost;
    for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
        const auto coefficientIndex = 2U * segmentIndex;
        const double fraction =
            optimizationData.val[off.contOffset + 1 + static_cast<index_t>(segmentIndex)];
        const double selector =
            optimizationData.val[off.intOffset + static_cast<index_t>(segmentIndex)];
        const double powerAtSegmentStart = Pcoeff[coefficientIndex];
        const double costAtSegmentStart = Pcoeff[coefficientIndex + 1U];
        const double powerWidth = Pcoeff[coefficientIndex + 2U] - powerAtSegmentStart;
        const double costWidth = Pcoeff[coefficientIndex + 3U] - costAtSegmentStart;
        cVals[row] += selector;
        cVals[row + 1] -= (powerAtSegmentStart * selector) + (powerWidth * fraction);
        cVals[row + 2] -= (costAtSegmentStart * selector) + (costWidth * fraction);
        cVals[row + 3 + static_cast<index_t>(segmentIndex)] = fraction - selector;
    }
}

void GridGenOpt::constraintJacobianElements(const OptimizationData& /* of */,
                                            MatrixData<double>& matrixDataRef,
                                            const OptimizationMode& oMode)
{
    if (!usesActivePiecewiseCost(oMode)) {
        return;
    }
    const auto& off = offsets.getOffsets(oMode);
    const auto count = piecewiseSegmentCount(Pcoeff);
    const auto row = off.constraintOffset;
    if (!hasNonconvexActiveCost()) {
        for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
            const auto line = row + static_cast<index_t>(segmentIndex);
            matrixDataRef.assign(line, off.contOffset, 1.0);
            matrixDataRef.assign(line, off.gOffset, -piecewiseSlope(Pcoeff, segmentIndex));
        }
        return;
    }
    matrixDataRef.assign(row + 1, off.gOffset, 1.0);
    matrixDataRef.assign(row + 2, off.contOffset, 1.0);
    for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
        const auto coefficientIndex = 2U * segmentIndex;
        const auto fraction = off.contOffset + 1 + static_cast<index_t>(segmentIndex);
        const auto selector = off.intOffset + static_cast<index_t>(segmentIndex);
        const auto selectionRow = row + 3 + static_cast<index_t>(segmentIndex);
        matrixDataRef.assign(row, selector, 1.0);
        matrixDataRef.assign(row + 1, selector, -Pcoeff[coefficientIndex]);
        matrixDataRef.assign(row + 1,
                             fraction,
                             Pcoeff[coefficientIndex] - Pcoeff[coefficientIndex + 2U]);
        matrixDataRef.assign(row + 2, selector, -Pcoeff[coefficientIndex + 1U]);
        matrixDataRef.assign(row + 2,
                             fraction,
                             Pcoeff[coefficientIndex + 1U] - Pcoeff[coefficientIndex + 3U]);
        matrixDataRef.assign(selectionRow, fraction, 1.0);
        matrixDataRef.assign(selectionRow, selector, -1.0);
    }
}

void GridGenOpt::getObjectiveNames(stringVec& objectiveNames,
                                   const OptimizationMode& oMode,
                                   const std::string& prefix)
{
    auto& optimizationOffsets = offsets.getOffsets(oMode);
    if (optimizationOffsets.gOffset == kNullLocation) {
        return;
    }
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
    if (usesActivePiecewiseCost(oMode)) {
        const auto count = piecewiseSegmentCount(Pcoeff);
        const auto last = static_cast<std::size_t>(optimizationOffsets.contOffset) +
            (hasNonconvexActiveCost() ? count : 0U);
        if (objectiveNames.size() <= last) {
            objectiveNames.resize(last + 1U);
        }
        objectiveNames[optimizationOffsets.contOffset] = prefix + getName() + ":Cost";
        if (hasNonconvexActiveCost()) {
            const auto lastSelector =
                static_cast<std::size_t>(optimizationOffsets.intOffset) + count;
            if (objectiveNames.size() < lastSelector) {
                objectiveNames.resize(lastSelector);
            }
            for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
                objectiveNames[optimizationOffsets.contOffset + 1 +
                               static_cast<index_t>(segmentIndex)] =
                    prefix + getName() + ":Fraction" + std::to_string(segmentIndex);
                objectiveNames[optimizationOffsets.intOffset + static_cast<index_t>(segmentIndex)] =
                    prefix + getName() + ":Segment" + std::to_string(segmentIndex);
            }
        }
    }
}

// set properties
void GridGenOpt::set(std::string_view param, double val, units::unit unitType)
{
    using units::convert;
    using units::puMW;
    const auto result = GridGenOpt::getModelDescription().trySet(*this, param, val, unitType);
    if (isSetHandled(result)) {
        return;
    }
    if (result == SetResult::INDEXED_COMMAND) {
        std::size_t num = 0;
        // trySet has already checked the complete decimal suffix and its range.
        std::from_chars(param.data() + 1, param.data() + param.size(), num);
        auto& coefficients = (param.front() == 'p') ? Pcoeff : Qcoeff;
        if (num >= coefficients.size()) {
            coefficients.resize(num + 1);
        }
        coefficients[num] = val;
        return;
    }
    if ((param == "constantp") || (param == "constant")) {
        if (Pcoeff.empty()) {
            Pcoeff.resize(1);
        }
        Pcoeff[0] = val;
    } else if ((param == "linearp") || (param == "linear")) {
        const auto coefficient = convertCostCoefficient(val, unitType, 1);
        if (Pcoeff.size() < 2) {
            Pcoeff.resize(2);
        }
        Pcoeff[1] = coefficient;
    } else if ((param == "quadraticp") || (param == "quadp") || (param == "quadratic") ||
               (param == "quad")) {
        const auto coefficient = convertCostCoefficient(val, unitType, 2);
        if (Pcoeff.size() < 3) {
            Pcoeff.resize(3);
        }
        Pcoeff[2] = coefficient;
    } else if (param == "constantq") {
        if (Qcoeff.empty()) {
            Qcoeff.resize(1);
        }
        Qcoeff[0] = val;
    } else if (param == "linearq") {
        const auto coefficient = convertCostCoefficient(val, unitType, 1);
        if (Qcoeff.size() < 2) {
            Qcoeff.resize(2);
        }
        Qcoeff[1] = coefficient;
    } else if ((param == "quadraticq") || (param == "quadq")) {
        const auto coefficient = convertCostCoefficient(val, unitType, 2);
        if (Qcoeff.size() < 3) {
            Qcoeff.resize(3);
        }
        Qcoeff[2] = coefficient;
    } else if ((param == "penalty_cost") || (param == "penalty")) {
        m_penaltyCost = convertCostCoefficient(val, unitType, 1);
    } else if ((param == "pmax") || (param == "pmin")) {
        if (gen != nullptr) {
            gen->set(param, val, unitType);
        }
    } else if (param == "forecast") {
        m_forecast = convert(val, unitType, puMW, powerBase());
    } else {
        GridOptObject::set(param, val, unitType);
    }
}

double GridGenOpt::powerBase() const
{
    return (gen != nullptr) ? gen->getRoot()->get("basepower") : 1.0;
}

double GridGenOpt::convertCostCoefficient(double value, units::unit unitType, int order) const
{
    const auto nativeUnits = units::currency / units::puMW.pow(order) / units::hr;
    const auto converted = (units::is_default(unitType) || unitType.is_per_unit()) ?
        units::convert(value, unitType, nativeUnits) :
        units::convert(value, unitType, units::currency / units::MW.pow(order) / units::hr) *
            std::pow(powerBase(), order);
    if (!std::isfinite(converted)) {
        throw InvalidParameterValue("invalid generator cost coefficient units or value");
    }
    return converted;
}

void GridGenOpt::loadMatPowerCostCoeff(std::vector<double> coeff, int powerMode, int costModel)
{
    const auto basePower = powerBase();
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
        if ((powerMode == 0) && (gen != nullptr) && (coeff.size() >= 4U)) {
            // MATPOWER extends the first and last segment line beyond the
            // supplied knots. Include the physical dispatch limits so the
            // binary segment formulation does not narrow the feasible range.
            const double minimum = gen->getPmin(0.0);
            if (minimum < coeff.front()) {
                const double slope = piecewiseSlope(coeff, 0U);
                const double cost = coeff[1] + (slope * (minimum - coeff[0]));
                coeff.insert(coeff.begin(), {minimum, cost});
            }
            const double maximum = gen->getPmax(0.0);
            if (maximum > coeff[coeff.size() - 2U]) {
                const auto last = piecewiseSegmentCount(coeff) - 1U;
                const double slope = piecewiseSlope(coeff, last);
                const double cost = coeff.back() + (slope * (maximum - coeff[coeff.size() - 2U]));
                coeff.insert(coeff.end(), {maximum, cost});
            }
        }
        optFlags.set(PIECEWISE_LINEAR_COST);
    }
    if (powerMode == 0) {
        Pcoeff = coeff;
    } else {
        Qcoeff = coeff;
    }
}

void GridGenOpt::loadMatPowerCostCurve(const MatPowerCostCurve& curve, bool reactive)
{
    if (!curve.valid()) {
        throw InvalidParameterValue("invalid MATPOWER/PYPOWER generator cost curve");
    }
    (reactive ? reactiveCostCurve : activeCostCurve) = curve;
    optFlags.reset(PIECEWISE_LINEAR_COST);
    if (activeCostCurve.model == 1 || reactiveCostCurve.model == 1) {
        optFlags.set(PIECEWISE_LINEAR_COST);
    }
    loadMatPowerCostCoeff(curve.coefficients, reactive ? 1 : 0, curve.model);
}

bool GridGenOpt::usesActivePiecewiseCost(const OptimizationMode& oMode) const
{
    return (oMode.flowMode == FlowModel::DC) && (activeCostCurve.model == 1) &&
        (piecewiseSegmentCount(Pcoeff) != 0U) && (gen != nullptr) && gen->isEnabled();
}

bool GridGenOpt::hasNonconvexActiveCost() const
{
    return !isConvexPiecewise(Pcoeff);
}

bool GridGenOpt::piecewiseCoversGenerationBounds(double time) const
{
    if ((gen == nullptr) || !gen->isEnabled() || (activeCostCurve.model != 1)) {
        return true;
    }
    if (piecewiseSegmentCount(Pcoeff) == 0U) {
        return false;
    }
    const double first = Pcoeff.front();
    const double last = Pcoeff[Pcoeff.size() - 2U];
    const double tolerance = 1.0e-9 * (std::max)({1.0, std::abs(first), std::abs(last)});
    return gen->getPmin(time) >= first - tolerance && gen->getPmax(time) <= last + tolerance;
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
