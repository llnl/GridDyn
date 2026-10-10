/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterUEL1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {
namespace {
    constexpr double signalTolerance = 1e-14;
}

ExcitationLimiterUEL1::ExcitationLimiterUEL1(const std::string& objName): ExcitationLimiter(objName)
{
    setRole(Role::UNDER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 24;
}

CoreObject* ExcitationLimiterUEL1::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterUEL1, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->kur = kur;
        result->kuc = kuc;
        result->vurMax = vurMax;
        result->vucMax = vucMax;
        result->kuI = kuI;
        result->kuL = kuL;
        result->vuiMax = vuiMax;
        result->vuiMin = vuiMin;
        result->tu1 = tu1;
        result->tu2 = tu2;
        result->tu3 = tu3;
        result->tu4 = tu4;
        result->vulMax = vulMax;
        result->vulMin = vulMin;
        result->kuf = kuf;
        result->prevTime = 0.0;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterUEL1::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "kur" || param == "ku_r") {
        kur = val;
    } else if (param == "kuc" || param == "ku_c") {
        kuc = val;
    } else if (param == "vurmax" || param == "vur_max") {
        vurMax = val;
    } else if (param == "vucmax" || param == "vuc_max") {
        vucMax = val;
    } else if (param == "kui" || param == "ku_i") {
        kuI = val;
    } else if (param == "kul" || param == "ku_l") {
        kuL = val;
    } else if (param == "vuimax" || param == "vui_max") {
        vuiMax = val;
    } else if (param == "vuimin" || param == "vui_min") {
        vuiMin = val;
    } else if (param == "tu1") {
        tu1 = val;
    } else if (param == "tu2") {
        tu2 = val;
    } else if (param == "tu3") {
        tu3 = val;
    } else if (param == "tu4") {
        tu4 = val;
    } else if (param == "vulmax" || param == "vuelmax" || param == "vul_max") {
        vulMax = val;
    } else if (param == "vulmin" || param == "vuelmin" || param == "vul_min") {
        vulMin = val;
    } else if (param == "kuf" || param == "ku_f") {
        kuf = val;
    } else {
        ExcitationLimiter::set(param, val, unitType);
    }
}

double ExcitationLimiterUEL1::get(std::string_view param, units::unit unitType) const
{
    if (param == "kur" || param == "ku_r") {
        return kur;
    }
    if (param == "kuc" || param == "ku_c") {
        return kuc;
    }
    if (param == "vurmax" || param == "vur_max") {
        return vurMax;
    }
    if (param == "vucmax" || param == "vuc_max") {
        return vucMax;
    }
    if (param == "kui" || param == "ku_i") {
        return kuI;
    }
    if (param == "kul" || param == "ku_l") {
        return kuL;
    }
    if (param == "vuimax" || param == "vui_max") {
        return vuiMax;
    }
    if (param == "vuimin" || param == "vui_min") {
        return vuiMin;
    }
    if (param == "tu1") {
        return tu1;
    }
    if (param == "tu2") {
        return tu2;
    }
    if (param == "tu3") {
        return tu3;
    }
    if (param == "tu4") {
        return tu4;
    }
    if (param == "vulmax" || param == "vuelmax" || param == "vul_max") {
        return vulMax;
    }
    if (param == "vulmin" || param == "vuelmin" || param == "vul_min") {
        return vulMin;
    }
    if (param == "kuf" || param == "ku_f") {
        return kuf;
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterUEL1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(kur) || !std::isfinite(kuc) || !std::isfinite(vurMax) ||
        !std::isfinite(vucMax) || !std::isfinite(kuI) || !std::isfinite(kuL) ||
        !std::isfinite(vuiMax) || !std::isfinite(vuiMin) || !std::isfinite(tu1) ||
        !std::isfinite(tu2) || !std::isfinite(tu3) || !std::isfinite(tu4) ||
        !std::isfinite(vulMax) || !std::isfinite(vulMin) || !std::isfinite(kuf) || kur <= 0.0 ||
        kuc < 0.0 || vurMax <= 0.0 || vucMax <= 0.0 || kuI < 0.0 || kuL < 0.0 || vuiMin > vuiMax ||
        vulMin < 0.0 || vulMin > vulMax || tu1 < 0.0 || tu2 < 0.0 || tu3 < 0.0 || tu4 < 0.0 ||
        (tu2 == 0.0 && tu1 != 0.0) || (tu4 == 0.0 && tu3 != 0.0) ||
        std::abs(kuf) > signalTolerance) {
        throw InvalidParameterValue(
            "UEL1 parameters (KUF must be zero until AVR feedback is routed)");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 24;
    prevTime = time0;
}

ExcitationLimiterUEL1::Characteristic
    ExcitationLimiterUEL1::characteristic(const IOdata& inputs) const
{
    if (inputs.size() < excitationLimiterInputCount) {
        throw InvalidParameterValue("UEL1 input vector");
    }
    for (auto index :
         {limiterIdInLocation, limiterIqInLocation, limiterVdInLocation, limiterVqInLocation}) {
        if (!std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
            throw InvalidParameterValue("UEL1 requires compatible machine current and voltage");
        }
    }
    const double idCurrent = inputs[limiterIdInLocation];
    const double quadratureCurrent = inputs[limiterIqInLocation];
    const double directVoltage = inputs[limiterVdInLocation];
    const double quadratureVoltage = inputs[limiterVqInLocation];
    const double voltage = std::hypot(directVoltage, quadratureVoltage);
    if (voltage <= signalTolerance) {
        throw InvalidParameterValue("UEL1 requires nonzero terminal voltage");
    }

    // VUC = |KUC*VT - j*IT| and VUR = |KUR*VT|, using GridDyn's d-q
    // sign convention for machine terminal current and voltage.
    const double ucReal = (kuc * directVoltage) + quadratureCurrent;
    const double ucImag = (kuc * quadratureVoltage) - idCurrent;
    const double ucRaw = std::hypot(ucReal, ucImag);
    const double urRaw = kur * voltage;
    const double limitedUcVoltage = std::min(ucRaw, vucMax);
    const double limitedUrVoltage = std::min(urRaw, vurMax);
    Characteristic result;
    result.error = limitedUcVoltage - limitedUrVoltage;

    const bool ucActive = ucRaw < vucMax && ucRaw > signalTolerance;
    const bool urActive = urRaw < vurMax;
    if (ucActive) {
        result.derivatives[limiterIdInLocation] = -ucImag / ucRaw;
        result.derivatives[limiterIqInLocation] = ucReal / ucRaw;
        result.derivatives[limiterVdInLocation] = kuc * ucReal / ucRaw;
        result.derivatives[limiterVqInLocation] = kuc * ucImag / ucRaw;
    }
    if (urActive) {
        result.derivatives[limiterVdInLocation] -= kur * directVoltage / voltage;
        result.derivatives[limiterVqInLocation] -= kur * quadratureVoltage / voltage;
    }
    return result;
}

ExcitationLimiterUEL1::Control ExcitationLimiterUEL1::control(const IOdata& inputs,
                                                              double integral,
                                                              double lag1,
                                                              double lag2) const
{
    const auto measurement = characteristic(inputs);
    const double piSignal = (kuL * measurement.error) + integral;
    const double alpha1 = (tu2 > 0.0) ? tu1 / tu2 : 1.0;
    const double first = (alpha1 * piSignal) + ((1.0 - alpha1) * lag1);
    const double alpha2 = (tu4 > 0.0) ? tu3 / tu4 : 1.0;
    const double raw = (alpha2 * first) + ((1.0 - alpha2) * lag2);
    const double action = std::clamp(raw, vulMin, vulMax);
    const double slope = (raw > vulMin && raw < vulMax) ? 1.0 : 0.0;
    return {.action = action,
            .errorGain = slope * alpha2 * alpha1 * kuL,
            .integralGain = slope * alpha2 * alpha1,
            .firstLagGain = slope * alpha2 * (1.0 - alpha1),
            .secondLagGain = slope * (1.0 - alpha2)};
}

double ExcitationLimiterUEL1::integralRate(double integral, double error) const
{
    const double rate = kuI * error;
    if ((integral <= vuiMin && rate < 0.0) || (integral >= vuiMax && rate > 0.0)) {
        return 0.0;
    }
    return rate;
}

void ExcitationLimiterUEL1::dynObjectInitializeB(const IOdata& inputs,
                                                 const IOdata& /*desiredOutput*/,
                                                 IOdata& fieldSet)
{
    const double error = characteristic(inputs).error;
    m_state[1] = 0.0;
    m_state[2] = kuL * error;
    m_state[3] = m_state[2];
    m_state[0] = control(inputs, m_state[1], m_state[2], m_state[3]).action;
    fieldSet = {m_state[0]};
}

void ExcitationLimiterUEL1::residual(const IOdata& inputs,
                                     const StateData& stateData,
                                     double resid[],
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto measured = characteristic(inputs);
    const auto output =
        control(inputs, loc.diffStateLoc[0], loc.diffStateLoc[1], loc.diffStateLoc[2]);
    const double piSignal = (kuL * measured.error) + loc.diffStateLoc[0];
    const double alpha1 = (tu2 > 0.0) ? tu1 / tu2 : 1.0;
    const double first = (alpha1 * piSignal) + ((1.0 - alpha1) * loc.diffStateLoc[1]);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = output.action - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        loc.destDiffLoc[0] = integralRate(loc.diffStateLoc[0], measured.error) - loc.dstateLoc[0];
        loc.destDiffLoc[1] = (tu2 > 0.0) ?
            ((piSignal - loc.diffStateLoc[1]) / tu2) - loc.dstateLoc[1] :
            -loc.dstateLoc[1];
        loc.destDiffLoc[2] = (tu4 > 0.0) ?
            ((first - loc.diffStateLoc[2]) / tu4) - loc.dstateLoc[2] :
            -loc.dstateLoc[2];
    }
}

void ExcitationLimiterUEL1::derivative(const IOdata& inputs,
                                       const StateData& stateData,
                                       double deriv[],
                                       const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto measured = characteristic(inputs);
    const double piSignal = (kuL * measured.error) + loc.diffStateLoc[0];
    const double alpha1 = (tu2 > 0.0) ? tu1 / tu2 : 1.0;
    const double first = (alpha1 * piSignal) + ((1.0 - alpha1) * loc.diffStateLoc[1]);
    loc.destDiffLoc[0] = integralRate(loc.diffStateLoc[0], measured.error);
    loc.destDiffLoc[1] = (tu2 > 0.0) ? (piSignal - loc.diffStateLoc[1]) / tu2 : 0.0;
    loc.destDiffLoc[2] = (tu4 > 0.0) ? (first - loc.diffStateLoc[2]) / tu4 : 0.0;
}

void ExcitationLimiterUEL1::algebraicUpdate(const IOdata& inputs,
                                            const StateData& stateData,
                                            double update[],
                                            const SolverMode& sMode,
                                            double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] =
        control(inputs, loc.diffStateLoc[0], loc.diffStateLoc[1], loc.diffStateLoc[2]).action;
}

void ExcitationLimiterUEL1::jacobianElements(const IOdata& inputs,
                                             const StateData& stateData,
                                             MatrixData<double>& matrixData,
                                             const IOlocs& inputLocs,
                                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto measured = characteristic(inputs);
    const auto output =
        control(inputs, loc.diffStateLoc[0], loc.diffStateLoc[1], loc.diffStateLoc[2]);
    const double integral = loc.diffStateLoc[0];
    const double error = measured.error;
    const double rate = kuI * error;
    const bool blockedAtMinimum = integral <= vuiMin && rate < 0.0;
    const bool blockedAtMaximum = integral >= vuiMax && rate > 0.0;
    const bool integrate = !blockedAtMinimum && !blockedAtMaximum;
    const double alpha1 = (tu2 > 0.0) ? tu1 / tu2 : 1.0;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(loc.algOffset, loc.diffOffset, output.integralGain);
            matrixData.assign(loc.algOffset, loc.diffOffset + 1, output.firstLagGain);
            matrixData.assign(loc.algOffset, loc.diffOffset + 2, output.secondLagGain);
            for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
                if (output.errorGain * measured.derivatives[input] != 0.0) {
                    matrixData.assignCheckCol(loc.algOffset,
                                              inputLocs[input],
                                              output.errorGain * measured.derivatives[input]);
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
                                      kuI * measured.derivatives[input]);
        }
    }
    if (tu2 > 0.0) {
        matrixData.assign(loc.diffOffset + 1, loc.diffOffset, 1.0 / tu2);
        matrixData.assign(loc.diffOffset + 1, loc.diffOffset + 1, (-1.0 / tu2) - stateData.cj);
        for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
            matrixData.assignCheckCol(loc.diffOffset + 1,
                                      inputLocs[input],
                                      kuL * measured.derivatives[input] / tu2);
        }
    } else {
        matrixData.assign(loc.diffOffset + 1, loc.diffOffset + 1, -stateData.cj);
    }
    if (tu4 > 0.0) {
        matrixData.assign(loc.diffOffset + 2, loc.diffOffset, alpha1 / tu4);
        matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 1, (1.0 - alpha1) / tu4);
        matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 2, (-1.0 / tu4) - stateData.cj);
        for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
            matrixData.assignCheckCol(loc.diffOffset + 2,
                                      inputLocs[input],
                                      alpha1 * kuL * measured.derivatives[input] / tu4);
        }
    } else {
        matrixData.assign(loc.diffOffset + 2, loc.diffOffset + 2, -stateData.cj);
    }
}

void ExcitationLimiterUEL1::timestep(CoreTime time,
                                     const IOdata& inputs,
                                     const SolverMode& /*sMode*/)
{
    const double step = time - prevTime;
    const double error = characteristic(inputs).error;
    const double oldIntegral = m_state[1];
    const double oldLag1 = m_state[2];
    const double oldLag2 = m_state[3];
    const double piSignal = (kuL * error) + oldIntegral;
    const double alpha1 = (tu2 > 0.0) ? tu1 / tu2 : 1.0;
    const double first = (alpha1 * piSignal) + ((1.0 - alpha1) * oldLag1);
    m_state[1] =
        std::clamp(oldIntegral + (step * integralRate(oldIntegral, error)), vuiMin, vuiMax);
    if (tu2 > 0.0) {
        m_state[2] += step * (piSignal - oldLag1) / tu2;
    }
    if (tu4 > 0.0) {
        m_state[3] += step * (first - oldLag2) / tu4;
    }
    m_state[0] = control(inputs, m_state[1], m_state[2], m_state[3]).action;
    prevTime = time;
}

stringVec ExcitationLimiterUEL1::localStateNames() const
{
    return {"vuel", "integral", "lead_lag_1", "lead_lag_2"};
}
}  // namespace griddyn::limiters
