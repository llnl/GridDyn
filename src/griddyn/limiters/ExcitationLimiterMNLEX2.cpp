/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterMNLEX2.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {

ExcitationLimiterMNLEX2::ExcitationLimiterMNLEX2(const std::string& objName):
    ExcitationLimiter(objName)
{
    setRole(Role::UNDER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 2;
    offsets.local().local.jacSize = 16;
}

CoreObject* ExcitationLimiterMNLEX2::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterMNLEX2, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->kF2 = kF2;
        result->tF2 = tF2;
        result->kM = kM;
        result->tM = tM;
        result->melMax = melMax;
        result->q0 = q0;
        result->radius = radius;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterMNLEX2::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "kf2" || param == "k_f2") {
        kF2 = val;
    } else if (param == "tf2" || param == "t_f2") {
        tF2 = val;
    } else if (param == "km" || param == "k_m") {
        kM = val;
    } else if (param == "tm" || param == "t_m") {
        tM = val;
    } else if (param == "melmax" || param == "mel_max") {
        melMax = val;
    } else if (param == "q0" || param == "q_0") {
        q0 = val;
    } else if (param == "radius") {
        radius = val;
    } else {
        ExcitationLimiter::set(param, val, unitType);
    }
}

double ExcitationLimiterMNLEX2::get(std::string_view param, units::unit unitType) const
{
    if (param == "kf2" || param == "k_f2") {
        return kF2;
    }
    if (param == "tf2" || param == "t_f2") {
        return tF2;
    }
    if (param == "km" || param == "k_m") {
        return kM;
    }
    if (param == "tm" || param == "t_m") {
        return tM;
    }
    if (param == "melmax" || param == "mel_max") {
        return melMax;
    }
    if (param == "q0" || param == "q_0") {
        return q0;
    }
    if (param == "radius") {
        return radius;
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterMNLEX2::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(kF2) || !std::isfinite(tF2) || !std::isfinite(kM) || !std::isfinite(tM) ||
        !std::isfinite(melMax) || !std::isfinite(q0) || !std::isfinite(radius) || kF2 < 0.0 ||
        tF2 <= 0.0 || kM <= 0.0 || tM <= 0.0 || melMax <= 0.0 || radius <= 0.0) {
        throw InvalidParameterValue("MNLEX2 gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 2;
    offsets.local().local.jacSize = 16;
    prevTime = time0;
}

ExcitationLimiterMNLEX2::Circle ExcitationLimiterMNLEX2::circleError(const IOdata& inputs) const
{
    if (inputs.size() < excitationLimiterInputCount) {
        throw InvalidParameterValue("MNLEX2 input vector");
    }
    for (auto index :
         {limiterIdInLocation, limiterIqInLocation, limiterVdInLocation, limiterVqInLocation}) {
        if (!std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
            throw InvalidParameterValue("MNLEX2 requires compatible machine P, Q, and voltage");
        }
    }
    const double iDirect = inputs[limiterIdInLocation];
    const double quadratureCurrent = inputs[limiterIqInLocation];
    const double directVoltage = inputs[limiterVdInLocation];
    const double quadratureVoltage = inputs[limiterVqInLocation];
    const double activePower = (iDirect * directVoltage) +
        (quadratureCurrent * quadratureVoltage);
    const double reactivePower = (iDirect * quadratureVoltage) -
        (quadratureCurrent * directVoltage);
    const double voltageSquared = (directVoltage * directVoltage) +
        (quadratureVoltage * quadratureVoltage);
    const double centeredQ = (q0 * voltageSquared) - reactivePower;
    const double scaledRadius = radius * voltageSquared;
    Circle result;
    result.error = (centeredQ * centeredQ) + (activePower * activePower) -
        (scaledRadius * scaledRadius);
    const std::array<double, excitationLimiterInputCount> activePowerDerivatives{
        0, directVoltage, quadratureVoltage, iDirect, quadratureCurrent};
    const std::array<double, excitationLimiterInputCount> reactivePowerDerivatives{
        0, quadratureVoltage, -directVoltage, -quadratureCurrent, iDirect};
    const std::array<double, excitationLimiterInputCount> voltageSquaredDerivatives{
        0, 0, 0, 2 * directVoltage, 2 * quadratureVoltage};
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        const double centeredQDerivative =
            2 * centeredQ * ((q0 * voltageSquaredDerivatives[index]) -
                             reactivePowerDerivatives[index]);
        const double activePowerDerivative = 2 * activePower * activePowerDerivatives[index];
        const double radiusDerivative =
            2 * scaledRadius * radius * voltageSquaredDerivatives[index];
        result.derivatives[index] = centeredQDerivative + activePowerDerivative - radiusDerivative;
    }
    return result;
}

double ExcitationLimiterMNLEX2::rate(double output, double feedback, double error) const
{
    const double unconstrainedRate =
        ((kM * (error - ((kF2 / tF2) * (output - feedback)))) - output) / tM;
    if ((output <= 0.0 && unconstrainedRate < 0.0) ||
        (output >= melMax && unconstrainedRate > 0.0)) {
        return 0.0;
    }
    return unconstrainedRate;
}

void ExcitationLimiterMNLEX2::dynObjectInitializeB(const IOdata& inputs,
                                                   const IOdata& /*desiredOutput*/,
                                                   IOdata& fieldSet)
{
    const double output = std::clamp(kM * circleError(inputs).error, 0.0, melMax);
    m_state[0] = output;
    m_state[1] = output;
    m_state[2] = output;
    fieldSet = {output};
}

void ExcitationLimiterMNLEX2::residual(const IOdata& inputs,
                                       const StateData& stateData,
                                       double resid[],
                                       const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const double mel = loc.diffStateLoc[0];
    const double output = std::clamp(mel, 0.0, melMax);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = output - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        const auto error = circleError(inputs).error;
        loc.destDiffLoc[0] = rate(output, loc.diffStateLoc[1], error) - loc.dstateLoc[0];
        loc.destDiffLoc[1] = ((output - loc.diffStateLoc[1]) / tF2) - loc.dstateLoc[1];
    }
}

void ExcitationLimiterMNLEX2::derivative(const IOdata& inputs,
                                         const StateData& stateData,
                                         double deriv[],
                                         const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double output = std::clamp(loc.diffStateLoc[0], 0.0, melMax);
    loc.destDiffLoc[0] = rate(output, loc.diffStateLoc[1], circleError(inputs).error);
    loc.destDiffLoc[1] = (output - loc.diffStateLoc[1]) / tF2;
}

void ExcitationLimiterMNLEX2::algebraicUpdate(const IOdata& /*inputs*/,
                                              const StateData& stateData,
                                              double update[],
                                              const SolverMode& sMode,
                                              double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    loc.destLoc[0] = std::clamp(loc.diffStateLoc[0], 0.0, melMax);
}

void ExcitationLimiterMNLEX2::jacobianElements(const IOdata& inputs,
                                               const StateData& stateData,
                                               MatrixData<double>& matrixData,
                                               const IOlocs& inputLocs,
                                               const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            const double mel = loc.diffStateLoc[0];
            const double outputSensitivity = (mel >= 0.0 && mel < melMax) ? 1.0 : 0.0;
            matrixData.assign(loc.algOffset, loc.diffOffset, outputSensitivity);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto circle = circleError(inputs);
    const double mel = loc.diffStateLoc[0];
    const double output = std::clamp(mel, 0.0, melMax);
    const double outputSensitivity = (mel >= 0.0 && mel < melMax) ? 1.0 : 0.0;
    const double unconstrainedRate =
        ((kM * (circle.error - ((kF2 / tF2) * (output - loc.diffStateLoc[1])))) - output) / tM;
    const bool limited =
        (output <= 0.0 && unconstrainedRate < 0.0) || (output >= melMax && unconstrainedRate > 0.0);
    const auto outputRow = loc.diffOffset;
    const double outputRateSlope =
        limited ? 0.0 : outputSensitivity * (-1.0 - ((kM * kF2) / tF2)) / tM;
    matrixData.assign(outputRow, outputRow, outputRateSlope - stateData.cj);
    if (!limited) {
        matrixData.assign(outputRow, outputRow + 1, kM * kF2 / (tM * tF2));
        for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
            matrixData.assignCheckCol(outputRow,
                                      inputLocs[index],
                                      kM * circle.derivatives[index] / tM);
        }
    }
    matrixData.assign(outputRow + 1, outputRow, outputSensitivity / tF2);
    matrixData.assign(outputRow + 1, outputRow + 1, -1.0 / tF2 - stateData.cj);
}

void ExcitationLimiterMNLEX2::timestep(CoreTime time,
                                       const IOdata& inputs,
                                       const SolverMode& /*sMode*/)
{
    const double step = time - prevTime;
    const double output = m_state[1];
    const double feedback = m_state[2];
    const double error = circleError(inputs).error;
    m_state[1] = std::clamp(output + (step * rate(output, feedback, error)), 0.0, melMax);
    m_state[2] = feedback + ((step * (output - feedback)) / tF2);
    m_state[0] = m_state[1];
    prevTime = time;
}

stringVec ExcitationLimiterMNLEX2::localStateNames() const
{
    return {"vuel", "mel", "feedback"};
}
}  // namespace griddyn::limiters
