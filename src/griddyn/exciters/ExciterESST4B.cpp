/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterESST4B.h"

#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace griddyn::exciters {
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t outerIntegralState = 1;
    constexpr index_t regulatorLagState = 2;
    constexpr index_t innerIntegralState = 3;
    constexpr double initializationTolerance = 1e-7;

    bool integrationBlocked(double state, double minimum, double maximum, double drive)
    {
        return ((state >= maximum) && (drive > 0.0)) || ((state <= minimum) && (drive < 0.0));
    }
}  // namespace

ExciterESST4B::ExciterESST4B(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Vrmax = 8.0;
    Vrmin = 0.0;
}

CoreObject* ExciterESST4B::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterESST4B, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Kpr = Kpr;
    result->Kir = Kir;
    result->Ta = Ta;
    result->Kpm = Kpm;
    result->Kim = Kim;
    result->Vmmax = Vmmax;
    result->Vmmin = Vmmin;
    result->Kg = Kg;
    result->Kp = Kp;
    result->Ki = Ki;
    result->Vbmax = Vbmax;
    result->Kc = Kc;
    result->Xl = Xl;
    result->ThetaP = ThetaP;
    return result;
}

void ExciterESST4B::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Tr) || !std::isfinite(Kpr) || !std::isfinite(Kir) || !std::isfinite(Vrmax) ||
        !std::isfinite(Vrmin) || !std::isfinite(Ta) || !std::isfinite(Kpm) || !std::isfinite(Kim) ||
        !std::isfinite(Vmmax) || !std::isfinite(Vmmin) || !std::isfinite(Kg) ||
        !std::isfinite(Kp) || !std::isfinite(Ki) || !std::isfinite(Vbmax) || !std::isfinite(Kc) ||
        !std::isfinite(Xl) || !std::isfinite(ThetaP) || (Tr <= 0.0) || (Ta <= 0.0) ||
        (Kpr <= 0.0) || (Kpm <= 0.0) || (Kir < 0.0) || (Kim < 0.0) || (Vrmax < Vrmin) ||
        (Vmmax < Vmmin) || (Vbmax <= 0.0)) {
        throw InvalidParameterValue("ESST4B gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 4;
    offsets.local().local.jacSize = 32;
}

void ExciterESST4B::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    for (const index_t index : {exciterIdInLocation,
                                exciterIqInLocation,
                                exciterVdInLocation,
                                exciterVqInLocation,
                                exciterXadIfdInLocation}) {
        if (!std::isfinite(inputs[index]) || (std::abs(inputs[index]) > 1e20)) {
            throw InvalidParameterValue("ESST4B requires compatible synchronous-machine signals");
        }
    }
    const double fieldVoltage = desiredOutput.empty() ? 0.0 : desiredOutput[0];
    const double vb = rectifierVoltage(inputs);
    if (vb <= 1e-12) {
        throw InvalidParameterValue("ESST4B initial rectifier voltage");
    }
    const double vm = fieldVoltage / vb;
    const double vr = Kg * fieldVoltage;
    if ((vm < Vmmin - initializationTolerance) || (vm > Vmmax + initializationTolerance) ||
        (vr < Vrmin - initializationTolerance) || (vr > Vrmax + initializationTolerance)) {
        throw InvalidParameterValue("ESST4B initial regulator output outside limits");
    }
    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[outerIntegralState] = vr;
    state[regulatorLagState] = vr;
    state[innerIntegralState] = vm;
    vBias = state[voltageMeasurementState] - Vref;
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

double ExciterESST4B::rectifierVoltage(const IOdata& inputs) const
{
    return detail::computeRectifierData(inputs, Kp, Ki, Kc, Xl, ThetaP, Vbmax).voltage;
}

void ExciterESST4B::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const double innerError = loc.diffStateLoc[regulatorLagState] - Kg * loc.algStateLoc[0];
        const double vm = std::clamp(Kpm * innerError + loc.diffStateLoc[innerIntegralState],
                                     static_cast<double>(Vmmin),
                                     static_cast<double>(Vmmax));
        loc.destLoc[0] = rectifierVoltage(inputs) * vm - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t ii = 0; ii < 4; ++ii) {
            loc.destDiffLoc[ii] -= loc.dstateLoc[ii];
        }
    }
}

void ExciterESST4B::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    double* dst = loc.destDiffLoc;
    const double outerError =
        Vref + vBias + inputs[exciterVssInLocation] - state[voltageMeasurementState];
    const double outerUnlimited = Kpr * outerError + state[outerIntegralState];
    const double vr =
        std::clamp(outerUnlimited, static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    const double innerError = state[regulatorLagState] - Kg * loc.algStateLoc[0];
    dst[voltageMeasurementState] =
        (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    dst[outerIntegralState] =
        integrationBlocked(state[outerIntegralState], Vrmin / Kpr, Vrmax / Kpr, Kir * outerError) ?
        0.0 :
        Kir * outerError;
    dst[regulatorLagState] = (vr - state[regulatorLagState]) / Ta;
    dst[innerIntegralState] =
        integrationBlocked(state[innerIntegralState], Vmmin / Kpm, Vmmax / Kpm, Kim * innerError) ?
        0.0 :
        Kim * innerError;
}

void ExciterESST4B::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto ra = loc.algOffset;
    const auto rd = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    const double innerError = state[regulatorLagState] - Kg * loc.algStateLoc[0];
    const double innerUnlimited = Kpm * innerError + state[innerIntegralState];
    const bool innerLimited = (innerUnlimited <= Vmmin) || (innerUnlimited >= Vmmax);
    if (hasAlgebraic(sMode)) {
        const auto rectifier = detail::computeRectifierData(inputs, Kp, Ki, Kc, Xl, ThetaP, Vbmax);
        const double vm =
            std::clamp(innerUnlimited, static_cast<double>(Vmmin), static_cast<double>(Vmmax));
        matrixData.assign(ra, ra, -1.0 - (innerLimited ? 0.0 : rectifier.voltage * Kpm * Kg));
        if (!innerLimited) {
            matrixData.assign(ra, rd + regulatorLagState, rectifier.voltage * Kpm);
            matrixData.assign(ra, rd + innerIntegralState, rectifier.voltage);
        }
        const std::array<index_t, 5> signalIndices{exciterIdInLocation,
                                                   exciterIqInLocation,
                                                   exciterVdInLocation,
                                                   exciterVqInLocation,
                                                   exciterXadIfdInLocation};
        for (index_t ii = 0; ii < 5; ++ii) {
            matrixData.assignCheckCol(ra,
                                      inputLocs[signalIndices[ii]],
                                      vm * rectifier.derivatives[ii]);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(rd, rd, -1.0 / Tr - stateData.cj);
    matrixData.assignCheckCol(rd, inputLocs[exciterVoltageInLocation], 1.0 / Tr);
    const double outerError = Vref + vBias + inputs[exciterVssInLocation] - state[0];
    const double outerUnlimited = Kpr * outerError + state[1];
    const bool outerLimited = (outerUnlimited <= Vrmin) || (outerUnlimited >= Vrmax);
    const bool outerBlocked =
        integrationBlocked(state[outerIntegralState], Vrmin / Kpr, Vrmax / Kpr, Kir * outerError);
    matrixData.assign(rd + 1, rd + 1, -stateData.cj);
    if (!outerBlocked) {
        matrixData.assign(rd + 1, rd, -Kir);
        matrixData.assignCheckCol(rd + 1, inputLocs[exciterVssInLocation], Kir);
    }
    matrixData.assign(rd + 2, rd + 2, -1.0 / Ta - stateData.cj);
    if (!outerLimited) {
        matrixData.assign(rd + 2, rd, -Kpr / Ta);
        matrixData.assign(rd + 2, rd + 1, 1.0 / Ta);
        matrixData.assignCheckCol(rd + 2, inputLocs[exciterVssInLocation], Kpr / Ta);
    }
    const bool innerBlocked =
        integrationBlocked(state[innerIntegralState], Vmmin / Kpm, Vmmax / Kpm, Kim * innerError);
    matrixData.assign(rd + 3, rd + 3, -stateData.cj);
    if (!innerBlocked) {
        matrixData.assign(rd + 3, rd + 2, Kim);
        matrixData.assign(rd + 3, ra, -Kim * Kg);
    }
}

void ExciterESST4B::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double dt = time - prevTime;
    for (index_t ii = 0; ii < 4; ++ii) {
        m_state[ii + 1] += dt * m_dstate_dt[ii + 1];
    }
    m_state[outerIntegralState + 1] = std::clamp(m_state[outerIntegralState + 1],
                                                 static_cast<double>(Vrmin / Kpr),
                                                 static_cast<double>(Vrmax / Kpr));
    m_state[innerIntegralState + 1] = std::clamp(m_state[innerIntegralState + 1],
                                                 static_cast<double>(Vmmin / Kpm),
                                                 static_cast<double>(Vmmax / Kpm));
    const double innerError = m_state[regulatorLagState + 1] - Kg * m_state[0];
    const double vm = std::clamp(Kpm * innerError + m_state[innerIntegralState + 1],
                                 static_cast<double>(Vmmin),
                                 static_cast<double>(Vmmax));
    m_state[0] = rectifierVoltage(inputs) * vm;
    prevTime = time;
}

void ExciterESST4B::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterESST4B::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tr") {
        Tr = val;
    } else if (param == "kpr") {
        Kpr = val;
    } else if (param == "kir") {
        Kir = val;
    } else if (param == "ta") {
        Ta = val;
    } else if (param == "kpm") {
        Kpm = val;
    } else if (param == "kim") {
        Kim = val;
    } else if (param == "vmmax") {
        Vmmax = val;
    } else if (param == "vmmin") {
        Vmmin = val;
    } else if (param == "kg") {
        Kg = val;
    } else if (param == "kp") {
        Kp = val;
    } else if (param == "ki") {
        Ki = val;
    } else if (param == "vbmax") {
        Vbmax = val;
    } else if (param == "kc") {
        Kc = val;
    } else if (param == "xl") {
        Xl = val;
    } else if ((param == "thetap") || (param == "theta_p")) {
        ThetaP = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterESST4B::get(std::string_view param, units::unit unitType) const
{
    if (param == "tr") {
        return Tr;
    }
    if (param == "kpr") {
        return Kpr;
    }
    if (param == "kir") {
        return Kir;
    }
    if (param == "vrmax") {
        return Vrmax;
    }
    if (param == "vrmin") {
        return Vrmin;
    }
    if (param == "ta") {
        return Ta;
    }
    if (param == "kpm") {
        return Kpm;
    }
    if (param == "kim") {
        return Kim;
    }
    if (param == "vmmax") {
        return Vmmax;
    }
    if (param == "vmmin") {
        return Vmmin;
    }
    if (param == "kg") {
        return Kg;
    }
    if (param == "kp") {
        return Kp;
    }
    if (param == "ki") {
        return Ki;
    }
    if (param == "vbmax") {
        return Vbmax;
    }
    if (param == "kc") {
        return Kc;
    }
    if (param == "xl") {
        return Xl;
    }
    if ((param == "thetap") || (param == "theta_p")) {
        return ThetaP;
    }
    return Exciter::get(param, unitType);
}

stringVec ExciterESST4B::localStateNames() const
{
    return {"efd", "vmeas", "vrint", "va", "vmint"};
}

index_t ExciterESST4B::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") {
        return offset;
    }
    if (field == "vrint") {
        return offset + 1;
    }
    if (field == "va") {
        return offset + 2;
    }
    if (field == "vmint") {
        return offset + 3;
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
