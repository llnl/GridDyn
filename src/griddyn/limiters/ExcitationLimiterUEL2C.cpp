/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterUEL2C.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {
namespace {
constexpr double inputTolerance = 1e-14;
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
    if (param == "k1") {
        if (!std::isfinite(val) || std::floor(val) != val) {
            throw InvalidParameterValue("UEL2C K1 must be an integer in [0,2]");
        }
        k1 = static_cast<int>(val);
    } else if (param == "k2") {
        if (!std::isfinite(val) || std::floor(val) != val) {
            throw InvalidParameterValue("UEL2C K2 must be an integer in [0,2]");
        }
        k2 = static_cast<int>(val);
    } else if (param == "kui" || param == "ku_i") {
        kuI = val;
    } else if (param == "kul" || param == "ku_l") {
        kuL = val;
    } else if (param == "vuimax" || param == "vui_max") {
        vuiMax = val;
    } else if (param == "vuimin" || param == "vui_min") {
        vuiMin = val;
    } else if (param == "vulmax1" || param == "vuelmax" || param == "vul_max1") {
        vulMax1 = val;
    } else if (param == "vulmin1" || param == "vuelmin" || param == "vul_min1") {
        vulMin1 = val;
    } else if (param == "vulmax2" || param == "vul_max2") {
        vulMax2 = val;
    } else if (param == "vulmin2" || param == "vul_min2") {
        vulMin2 = val;
    } else {
        bool found = false;
        for (std::size_t point = 0; point < pPoints.size(); ++point) {
            const auto index = std::to_string(point);
            if (param == ("p" + index)) {
                pPoints[point] = val;
                pointCount = std::max(pointCount, point + 1);
                found = true;
                break;
            }
            if (param == ("q" + index)) {
                qPoints[point] = val;
                pointCount = std::max(pointCount, point + 1);
                found = true;
                break;
            }
        }
        if (!found) {
            ExcitationLimiter::set(param, val, unitType);
        }
    }
}

double ExcitationLimiterUEL2C::get(std::string_view param, units::unit unitType) const
{
    if (param == "k1") { return static_cast<double>(k1); }
    if (param == "k2") { return static_cast<double>(k2); }
    if (param == "kui" || param == "ku_i") { return kuI; }
    if (param == "kul" || param == "ku_l") { return kuL; }
    if (param == "vuimax" || param == "vui_max") { return vuiMax; }
    if (param == "vuimin" || param == "vui_min") { return vuiMin; }
    if (param == "vulmax1" || param == "vuelmax" || param == "vul_max1") { return vulMax1; }
    if (param == "vulmin1" || param == "vuelmin" || param == "vul_min1") { return vulMin1; }
    if (param == "vulmax2" || param == "vul_max2") { return vulMax2; }
    if (param == "vulmin2" || param == "vul_min2") { return vulMin2; }
    for (std::size_t point = 0; point < pPoints.size(); ++point) {
        const auto index = std::to_string(point);
        if (param == ("p" + index)) { return pPoints[point]; }
        if (param == ("q" + index)) { return qPoints[point]; }
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterUEL2C::dynObjectInitializeA(CoreTime time0,
                                                  std::uint32_t /*flags*/)
{
    if ((k1 != 0 && k1 != 1 && k1 != 2) || (k2 != 0 && k2 != 1 && k2 != 2) ||
        !std::isfinite(kuI) || !std::isfinite(kuL) || !std::isfinite(vuiMax) ||
        !std::isfinite(vuiMin) || !std::isfinite(vulMax1) || !std::isfinite(vulMin1) ||
        !std::isfinite(vulMax2) || !std::isfinite(vulMin2) || kuI < 0.0 || kuL < 0.0 ||
        vuiMin > vuiMax || vulMin1 < 0.0 || vulMin1 > vulMax1 ||
        vulMin2 < 0.0 || vulMin2 > vulMax2 || pointCount < 2 || pointCount > pPoints.size()) {
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

ExcitationLimiterUEL2C::CurveEvaluation
    ExcitationLimiterUEL2C::curve(double normalizedP) const
{
    if (normalizedP <= pPoints[0]) { return {qPoints[0], 0.0}; }
    if (normalizedP >= pPoints[pointCount - 1]) { return {qPoints[pointCount - 1], 0.0}; }
    for (std::size_t point = 1; point < pointCount; ++point) {
        if (normalizedP <= pPoints[point]) {
            const double slope = (qPoints[point] - qPoints[point - 1]) /
                (pPoints[point] - pPoints[point - 1]);
            return {qPoints[point - 1] + slope * (normalizedP - pPoints[point - 1]), slope};
        }
    }
    return {qPoints[pointCount - 1], 0.0};
}

ExcitationLimiterUEL2C::Evaluation
    ExcitationLimiterUEL2C::evaluate(const IOdata& inputs, double integral) const
{
    if (inputs.size() < excitationLimiterInputCount) {
        throw InvalidParameterValue("UEL2C input vector");
    }
    for (auto index : {limiterIdInLocation, limiterIqInLocation,
                       limiterVdInLocation, limiterVqInLocation}) {
        if (!std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
            throw InvalidParameterValue("UEL2C requires compatible machine P, Q, and voltage");
        }
    }
    const double idCurrent = inputs[limiterIdInLocation];
    const double iq = inputs[limiterIqInLocation];
    const double vd = inputs[limiterVdInLocation];
    const double vq = inputs[limiterVqInLocation];
    const double p = idCurrent * vd + iq * vq;
    const double q = idCurrent * vq - iq * vd;
    const double voltage = std::hypot(vd, vq);
    if (voltage <= inputTolerance) {
        throw InvalidParameterValue("UEL2C requires nonzero terminal voltage");
    }
    const double f1 = std::pow(voltage, k1);
    const double f2 = std::pow(voltage, k2);
    const double normalizedP = p / f1;
    const auto limit = curve(normalizedP);
    const double qReference = limit.qLimit * f2;
    Evaluation result;
    result.error = qReference - q;

    const std::array<double, excitationLimiterInputCount> dp{
        0.0, vd, vq, idCurrent, iq};
    const std::array<double, excitationLimiterInputCount> dq{
        0.0, vq, -vd, -iq, idCurrent};
    const std::array<double, excitationLimiterInputCount> dv{
        0.0, 0.0, 0.0, vd / voltage, vq / voltage};
    const double f1Derivative = (k1 == 0) ? 0.0 :
        static_cast<double>(k1) * std::pow(voltage, k1 - 1);
    const double f2Derivative = (k2 == 0) ? 0.0 :
        static_cast<double>(k2) * std::pow(voltage, k2 - 1);
    for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
        const double dNormalizedP = dp[input] / f1 - p * f1Derivative * dv[input] / (f1 * f1);
        result.errorDerivatives[input] =
            limit.slope * f2 * dNormalizedP + limit.qLimit * f2Derivative * dv[input] - dq[input];
    }

    const double raw = kuL * result.error + integral;
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

void ExcitationLimiterUEL2C::residual(const IOdata& inputs, const StateData& stateData,
                                      double resid[], const SolverMode& sMode)
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

void ExcitationLimiterUEL2C::derivative(const IOdata& inputs, const StateData& stateData,
                                        double deriv[], const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) { return; }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc[0]);
    loc.destDiffLoc[0] = integralRate(loc.diffStateLoc[0], result.error);
}

void ExcitationLimiterUEL2C::algebraicUpdate(const IOdata& inputs,
                                             const StateData& stateData,
                                             double update[], const SolverMode& sMode,
                                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) { return; }
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
    const bool integrate = !((loc.diffStateLoc[0] <= vuiMin && rate < 0.0) ||
                             (loc.diffStateLoc[0] >= vuiMax && rate > 0.0));
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            if (result.outputIntegralSlope != 0.0) {
                matrixData.assign(loc.algOffset, loc.diffOffset, result.outputIntegralSlope);
            }
            for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
                if (result.outputErrorSlope * result.errorDerivatives[input] != 0.0) {
                    matrixData.assignCheckCol(loc.algOffset, inputLocs[input],
                                              result.outputErrorSlope * result.errorDerivatives[input]);
                }
            }
        }
    }
    if (!hasDifferential(sMode)) { return; }
    matrixData.assign(loc.diffOffset, loc.diffOffset, -stateData.cj);
    if (integrate) {
        for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
            matrixData.assignCheckCol(loc.diffOffset, inputLocs[input],
                                      kuI * result.errorDerivatives[input]);
        }
    }
}

void ExcitationLimiterUEL2C::timestep(CoreTime time, const IOdata& inputs,
                                      const SolverMode& /*sMode*/)
{
    const double step = time - prevTime;
    const auto result = evaluate(inputs, m_state[1]);
    m_state[1] = std::clamp(m_state[1] + step * integralRate(m_state[1], result.error),
                            vuiMin, vuiMax);
    m_state[0] = evaluate(inputs, m_state[1]).output;
    prevTime = time;
}

stringVec ExcitationLimiterUEL2C::localStateNames() const
{
    return {"vuel", "integral"};
}
}  // namespace griddyn::limiters
