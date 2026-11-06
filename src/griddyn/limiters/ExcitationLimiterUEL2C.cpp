/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterUEL2C.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {
namespace {
    constexpr double inputTolerance = 1e-14;
}

const ModelDescription& ExcitationLimiterUEL2C::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExcitationLimiterUEL2C>(
        "ExcitationLimiterUEL2C",
        &ExcitationLimiter::getModelDescription(),
        {parameter({"k1"},
                   &ExcitationLimiterUEL2C::k1,
                   units::one,
                   "K1",
                   [](int value) { return value >= 0 && value <= 2; }),
         parameter({"k2"},
                   &ExcitationLimiterUEL2C::k2,
                   units::one,
                   "K2",
                   [](int value) { return value >= 0 && value <= 2; }),
         parameter({"kui", "ku_i"}, &ExcitationLimiterUEL2C::kuI, units::one, "Ku i"),
         parameter({"kul", "ku_l"}, &ExcitationLimiterUEL2C::kuL, units::one, "Ku l"),
         parameter({"vuimax", "vui_max"}, &ExcitationLimiterUEL2C::vuiMax, units::one, "Vui max"),
         parameter({"vuimin", "vui_min"}, &ExcitationLimiterUEL2C::vuiMin, units::one, "Vui min"),
         parameter({"vulmax1", "vuelmax", "vul_max1"},
                   &ExcitationLimiterUEL2C::vulMax1,
                   units::one,
                   "Vul max1"),
         parameter({"vulmin1", "vuelmin", "vul_min1"},
                   &ExcitationLimiterUEL2C::vulMin1,
                   units::one,
                   "Vul min1"),
         parameter(
             {"vulmax2", "vul_max2"}, &ExcitationLimiterUEL2C::vulMax2, units::one, "Vul max2"),
         parameter(
             {"vulmin2", "vul_min2"}, &ExcitationLimiterUEL2C::vulMin2, units::one, "Vul min2"),
         parameter({"p0"}, &ExcitationLimiterUEL2C::pPoints, 0, units::one, "P points"),
         parameter({"p1"}, &ExcitationLimiterUEL2C::pPoints, 1, units::one, "P points"),
         parameter({"p2"}, &ExcitationLimiterUEL2C::pPoints, 2, units::one, "P points"),
         parameter({"p3"}, &ExcitationLimiterUEL2C::pPoints, 3, units::one, "P points"),
         parameter({"p4"}, &ExcitationLimiterUEL2C::pPoints, 4, units::one, "P points"),
         parameter({"p5"}, &ExcitationLimiterUEL2C::pPoints, 5, units::one, "P points"),
         parameter({"p6"}, &ExcitationLimiterUEL2C::pPoints, 6, units::one, "P points"),
         parameter({"q0"}, &ExcitationLimiterUEL2C::qPoints, 0, units::one, "Q points"),
         parameter({"q1"}, &ExcitationLimiterUEL2C::qPoints, 1, units::one, "Q points"),
         parameter({"q2"}, &ExcitationLimiterUEL2C::qPoints, 2, units::one, "Q points"),
         parameter({"q3"}, &ExcitationLimiterUEL2C::qPoints, 3, units::one, "Q points"),
         parameter({"q4"}, &ExcitationLimiterUEL2C::qPoints, 4, units::one, "Q points"),
         parameter({"q5"}, &ExcitationLimiterUEL2C::qPoints, 5, units::one, "Q points"),
         parameter({"q6"}, &ExcitationLimiterUEL2C::qPoints, 6, units::one, "Q points"),
         localState("pointCount", &ExcitationLimiterUEL2C::pointCount, "Point count"),
         state("vuel", units::one, "Solver variable vuel when active"),
         state("integral", units::one, "Solver variable integral when active"),
         localState("prevTime", &ExcitationLimiterUEL2C::prevTime, "Prev time")});
    return description;
}

void ExcitationLimiterUEL2C::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ExcitationLimiterUEL2C::getModelDescription();
    getParamString<ExcitationLimiterUEL2C, ExcitationLimiter>(this,
                                                              pstr,
                                                              description.numericNames(),
                                                              description.stringNames(),
                                                              description.flagNames(),
                                                              pstype);
}

ExcitationLimiterUEL2C::ExcitationLimiterUEL2C(const std::string& objName):
    ExcitationLimiter(objName)
{
    setRole(Role::UNDER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 1;
    offsets.local().local.jacSize = 12;
}

CoreObject* ExcitationLimiterUEL2C::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterUEL2C, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->pPoints = pPoints;
        result->qPoints = qPoints;
        result->pointCount = pointCount;
        result->k1 = k1;
        result->k2 = k2;
        result->kuI = kuI;
        result->kuL = kuL;
        result->vuiMax = vuiMax;
        result->vuiMin = vuiMin;
        result->vulMax1 = vulMax1;
        result->vulMin1 = vulMin1;
        result->vulMax2 = vulMax2;
        result->vulMin2 = vulMin2;
        result->prevTime = 0.0;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterUEL2C::set(std::string_view param, double val, units::unit unitType)
{
    if (ExcitationLimiterUEL2C::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        if (param.size() == 2 && (param.front() == 'p' || param.front() == 'q') &&
            param[1] >= '0' && param[1] <= '6') {
            pointCount = std::max(pointCount, static_cast<std::size_t>(param[1] - '0' + 1));
        }
        return;
    }
    ExcitationLimiter::set(param, val, unitType);
}

double ExcitationLimiterUEL2C::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            ExcitationLimiterUEL2C::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterUEL2C::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if ((k1 != 0 && k1 != 1 && k1 != 2) || (k2 != 0 && k2 != 1 && k2 != 2) || !std::isfinite(kuI) ||
        !std::isfinite(kuL) || !std::isfinite(vuiMax) || !std::isfinite(vuiMin) ||
        !std::isfinite(vulMax1) || !std::isfinite(vulMin1) || !std::isfinite(vulMax2) ||
        !std::isfinite(vulMin2) || kuI < 0.0 || kuL < 0.0 || vuiMin > vuiMax || vulMin1 < 0.0 ||
        vulMin1 > vulMax1 || vulMin2 < 0.0 || vulMin2 > vulMax2 || pointCount < 2 ||
        pointCount > pPoints.size()) {
        throw InvalidParameterValue("UEL2C fixed-profile parameters or curve");
    }
    for (std::size_t point = 0; point < pointCount; ++point) {
        if (!std::isfinite(pPoints[point]) || !std::isfinite(qPoints[point]) ||
            (point > 0 && pPoints[point] <= pPoints[point - 1])) {
            throw InvalidParameterValue("UEL2C curve points must be finite and ordered by P");
        }
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 1;
    offsets.local().local.jacSize = 12;
    prevTime = time0;
}

ExcitationLimiterUEL2C::CurveEvaluation ExcitationLimiterUEL2C::curve(double normalizedP) const
{
    if (normalizedP <= pPoints[0]) {
        return {.qLimit = qPoints[0], .slope = 0.0};
    }
    if (normalizedP >= pPoints[pointCount - 1]) {
        return {.qLimit = qPoints[pointCount - 1], .slope = 0.0};
    }
    for (std::size_t point = 1; point < pointCount; ++point) {
        if (normalizedP <= pPoints[point]) {
            const double slope =
                (qPoints[point] - qPoints[point - 1]) / (pPoints[point] - pPoints[point - 1]);
            return {.qLimit = qPoints[point - 1] + (slope * (normalizedP - pPoints[point - 1])),
                    .slope = slope};
        }
    }
    return {.qLimit = qPoints[pointCount - 1], .slope = 0.0};
}

ExcitationLimiterUEL2C::Evaluation ExcitationLimiterUEL2C::evaluate(const IOdata& inputs,
                                                                    double integral) const
{
    if (inputs.size() < excitationLimiterInputCount) {
        throw InvalidParameterValue("UEL2C input vector");
    }
    for (auto index :
         {limiterIdInLocation, limiterIqInLocation, limiterVdInLocation, limiterVqInLocation}) {
        if (!std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
            throw InvalidParameterValue("UEL2C requires compatible machine P, Q, and voltage");
        }
    }
    const double idCurrent = inputs[limiterIdInLocation];
    const double quadratureCurrent = inputs[limiterIqInLocation];
    const double directVoltage = inputs[limiterVdInLocation];
    const double quadratureVoltage = inputs[limiterVqInLocation];
    const double activePower =
        (idCurrent * directVoltage) + (quadratureCurrent * quadratureVoltage);
    const double reactivePower =
        (idCurrent * quadratureVoltage) - (quadratureCurrent * directVoltage);
    const double voltage = std::hypot(directVoltage, quadratureVoltage);
    if (voltage <= inputTolerance) {
        throw InvalidParameterValue("UEL2C requires nonzero terminal voltage");
    }
    const double voltageFactor1 = std::pow(voltage, k1);
    const double voltageFactor2 = std::pow(voltage, k2);
    const double normalizedP = activePower / voltageFactor1;
    const auto limit = curve(normalizedP);
    const double qReference = limit.qLimit * voltageFactor2;
    Evaluation result;
    result.error = qReference - reactivePower;

    const std::array<double, excitationLimiterInputCount> activePowerDerivatives{
        0.0, directVoltage, quadratureVoltage, idCurrent, quadratureCurrent};
    const std::array<double, excitationLimiterInputCount> reactivePowerDerivatives{
        0.0, quadratureVoltage, -directVoltage, -quadratureCurrent, idCurrent};
    const std::array<double, excitationLimiterInputCount> voltageDerivatives{
        0.0, 0.0, 0.0, directVoltage / voltage, quadratureVoltage / voltage};
    const double voltageFactor1Derivative =
        (k1 == 0) ? 0.0 : static_cast<double>(k1) * std::pow(voltage, k1 - 1);
    const double voltageFactor2Derivative =
        (k2 == 0) ? 0.0 : static_cast<double>(k2) * std::pow(voltage, k2 - 1);
    for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
        const double dNormalizedP = (activePowerDerivatives[input] / voltageFactor1) -
            ((activePower * voltageFactor1Derivative * voltageDerivatives[input]) /
             (voltageFactor1 * voltageFactor1));
        result.errorDerivatives[input] = (limit.slope * voltageFactor2 * dNormalizedP) +
            (limit.qLimit * voltageFactor2Derivative * voltageDerivatives[input]) -
            reactivePowerDerivatives[input];
    }

    const double raw = (kuL * result.error) + integral;
    const double piLimited = std::clamp(raw, vuiMin, vuiMax);
    const double output = std::clamp(piLimited, vulMin1, vulMax1);
    result.output = std::clamp(output, vulMin2, vulMax2);
    if (raw > vuiMin && raw < vuiMax && piLimited > vulMin1 && piLimited < vulMax1 &&
        output > vulMin2 && output < vulMax2) {
        result.outputIntegralSlope = 1.0;
        result.outputErrorSlope = kuL;
    }
    return result;
}

double ExcitationLimiterUEL2C::integralRate(double integral, double error) const
{
    const double rate = kuI * error;
    if ((integral <= vuiMin && rate < 0.0) || (integral >= vuiMax && rate > 0.0)) {
        return 0.0;
    }
    return rate;
}

void ExcitationLimiterUEL2C::dynObjectInitializeB(const IOdata& inputs,
                                                  const IOdata& /*desiredOutput*/,
                                                  IOdata& fieldSet)
{
    m_state[1] = 0.0;
    m_state[0] = evaluate(inputs, m_state[1]).output;
    fieldSet = {m_state[0]};
}

void ExcitationLimiterUEL2C::residual(const IOdata& inputs,
                                      const StateData& stateData,
                                      double resid[],
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc[0]);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = result.output - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        loc.destDiffLoc[0] = integralRate(loc.diffStateLoc[0], result.error) - loc.dstateLoc[0];
    }
}

void ExcitationLimiterUEL2C::derivative(const IOdata& inputs,
                                        const StateData& stateData,
                                        double deriv[],
                                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc[0]);
    loc.destDiffLoc[0] = integralRate(loc.diffStateLoc[0], result.error);
}

void ExcitationLimiterUEL2C::algebraicUpdate(const IOdata& inputs,
                                             const StateData& stateData,
                                             double update[],
                                             const SolverMode& sMode,
                                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc[0]).output;
}

void ExcitationLimiterUEL2C::jacobianElements(const IOdata& inputs,
                                              const StateData& stateData,
                                              MatrixData<double>& matrixData,
                                              const IOlocs& inputLocs,
                                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc[0]);
    const double rate = kuI * result.error;
    const bool blockedAtMinimum = loc.diffStateLoc[0] <= vuiMin && rate < 0.0;
    const bool blockedAtMaximum = loc.diffStateLoc[0] >= vuiMax && rate > 0.0;
    const bool integrate = !blockedAtMinimum && !blockedAtMaximum;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            if (result.outputIntegralSlope != 0.0) {
                matrixData.assign(loc.algOffset, loc.diffOffset, result.outputIntegralSlope);
            }
            for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
                if (result.outputErrorSlope * result.errorDerivatives[input] != 0.0) {
                    matrixData.assignCheckCol(loc.algOffset,
                                              inputLocs[input],
                                              result.outputErrorSlope *
                                                  result.errorDerivatives[input]);
                }
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(loc.diffOffset, loc.diffOffset, -stateData.cj);
    if (integrate) {
        for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
            matrixData.assignCheckCol(loc.diffOffset,
                                      inputLocs[input],
                                      kuI * result.errorDerivatives[input]);
        }
    }
}

void ExcitationLimiterUEL2C::timestep(CoreTime time,
                                      const IOdata& inputs,
                                      const SolverMode& /*sMode*/)
{
    const double step = time - prevTime;
    const auto result = evaluate(inputs, m_state[1]);
    m_state[1] =
        std::clamp(m_state[1] + (step * integralRate(m_state[1], result.error)), vuiMin, vuiMax);
    m_state[0] = evaluate(inputs, m_state[1]).output;
    prevTime = time;
}

stringVec ExcitationLimiterUEL2C::localStateNames() const
{
    return {"vuel", "integral"};
}
}  // namespace griddyn::limiters
