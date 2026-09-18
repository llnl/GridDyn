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
#include <string>

namespace griddyn::exciters {
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t outerIntegralState = 1;
    constexpr index_t regulatorLagState = 2;
    constexpr index_t innerIntegralState = 3;
    constexpr double initializationTolerance = 1e-7;

    index_t stateIndex(index_t fullIndex, bool hasVoltageMeasurement, bool hasRegulatorLag)
    {
        index_t index = fullIndex;
        if (!hasVoltageMeasurement && (fullIndex > voltageMeasurementState)) {
            --index;
        }
        if (!hasRegulatorLag && (fullIndex > regulatorLagState)) {
            --index;
        }
        return index;
    }

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

void ExciterESST4B::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(Tr) || !std::isfinite(Kpr) || !std::isfinite(Kir) || !std::isfinite(Vrmax) ||
        !std::isfinite(Vrmin) || !std::isfinite(Ta) || !std::isfinite(Kpm) || !std::isfinite(Kim) ||
        !std::isfinite(Vmmax) || !std::isfinite(Vmmin) || !std::isfinite(Kg) ||
        !std::isfinite(Kp) || !std::isfinite(Ki) || !std::isfinite(Vbmax) || !std::isfinite(Kc) ||
        !std::isfinite(Xl) || !std::isfinite(ThetaP) || (Tr < 0.0) || (Ta < 0.0) ||
        (Kpr <= 0.0) || (Kpm <= 0.0) || (Kir < 0.0) || (Kim < 0.0) || (Vrmax < Vrmin) ||
        (Vmmax < Vmmin) || (Vbmax <= 0.0)) {
        throw InvalidParameterValue("ESST4B gains, time constants, or limits");
    }
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 2 + static_cast<index_t>(hasVoltageMeasurement) +
        static_cast<index_t>(hasRegulatorLag);
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
    const double rectifierVoltageValue = rectifierVoltage(inputs);
    if (rectifierVoltageValue <= 1e-12) {
        throw InvalidParameterValue("ESST4B initial rectifier voltage");
    }
    const double innerOutput = fieldVoltage / rectifierVoltageValue;
    const double regulatorOutput = Kg * fieldVoltage;
    if ((innerOutput < Vmmin - initializationTolerance) ||
        (regulatorOutput < Vrmin - initializationTolerance)) {
        throw InvalidParameterValue("ESST4B initial regulator output below lower limit");
    }
    if (!adjustInitialUpperLimit(innerOutput, Vmmax, "ESST4B initial inner output") ||
        !adjustInitialUpperLimit(regulatorOutput, Vrmax, "ESST4B initial regulator output")) {
        throw InvalidParameterValue("ESST4B initial regulator output outside upper limit");
    }
    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    const auto outerIndex = stateIndex(outerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const auto innerIndex = stateIndex(innerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    if (hasVoltageMeasurement) {
        state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    }
    state[outerIndex] = regulatorOutput;
    if (hasRegulatorLag) {
        state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag)] =
            regulatorOutput;
    }
    state[innerIndex] = innerOutput;
    vBias = inputs[exciterVoltageInLocation] - Vref;
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
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    const auto outerIndex = stateIndex(outerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const auto innerIndex = stateIndex(innerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const double* state = loc.diffStateLoc;
    const double measuredVoltage = hasVoltageMeasurement ?
        state[voltageMeasurementState] :
        inputs[exciterVoltageInLocation];
    const double regulatorOutput = hasRegulatorLag ?
        state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag)] :
        std::clamp(Kpr * (Vref + vBias + inputs[exciterVssInLocation] - measuredVoltage) +
                       state[outerIndex],
                   static_cast<double>(Vrmin),
                   static_cast<double>(Vrmax));
    if (hasAlgebraic(sMode)) {
        const double innerError = regulatorOutput - Kg * loc.algStateLoc[0];
        const double innerOutput =
            std::clamp(Kpm * innerError + state[innerIndex],
                       static_cast<double>(Vmmin),
                       static_cast<double>(Vmmax));
        loc.destLoc[0] = rectifierVoltage(inputs) * innerOutput - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t ii = 0; ii < loc.diffSize; ++ii) {
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
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    const auto outerIndex = stateIndex(outerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const auto innerIndex = stateIndex(innerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const double measuredVoltage = hasVoltageMeasurement ?
        state[voltageMeasurementState] :
        inputs[exciterVoltageInLocation];
    const double outerError = Vref + vBias + inputs[exciterVssInLocation] - measuredVoltage;
    const double outerUnlimited = Kpr * outerError + state[outerIndex];
    const double regulatorOutput =
        std::clamp(outerUnlimited, static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    const double innerError =
        (hasRegulatorLag ? state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag)] :
                           regulatorOutput) -
        Kg * loc.algStateLoc[0];
    if (hasVoltageMeasurement) {
        dst[voltageMeasurementState] =
            (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    }
    dst[outerIndex] =
        integrationBlocked(state[outerIndex], Vrmin / Kpr, Vrmax / Kpr, Kir * outerError) ?
        0.0 :
        Kir * outerError;
    if (hasRegulatorLag) {
        const auto regulatorIndex =
            stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag);
        dst[regulatorIndex] = (regulatorOutput - state[regulatorIndex]) / Ta;
    }
    dst[innerIndex] =
        integrationBlocked(state[innerIndex], Vmmin / Kpm, Vmmax / Kpm, Kim * innerError) ?
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
    const auto algebraicRow = loc.algOffset;
    const auto differentialRow = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    const auto outerIndex = stateIndex(outerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const auto innerIndex = stateIndex(innerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const double measuredVoltage = hasVoltageMeasurement ?
        state[voltageMeasurementState] :
        inputs[exciterVoltageInLocation];
    const double outerError = Vref + vBias + inputs[exciterVssInLocation] - measuredVoltage;
    const double outerUnlimited = Kpr * outerError + state[outerIndex];
    const double regulatorOutput = hasRegulatorLag ?
        state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag)] :
        std::clamp(outerUnlimited, static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    const double innerError = regulatorOutput - Kg * loc.algStateLoc[0];
    const double innerUnlimited = Kpm * innerError + state[innerIndex];
    const bool innerLimited = (innerUnlimited <= Vmmin) || (innerUnlimited >= Vmmax);
    const bool outerLimited = (outerUnlimited <= Vrmin) || (outerUnlimited >= Vrmax);
    if (hasAlgebraic(sMode)) {
        const auto rectifier = detail::computeRectifierData(inputs, Kp, Ki, Kc, Xl, ThetaP, Vbmax);
        const double innerOutput =
            std::clamp(innerUnlimited, static_cast<double>(Vmmin), static_cast<double>(Vmmax));
        matrixData.assign(algebraicRow,
                          algebraicRow,
                          -1.0 - (innerLimited ? 0.0 : rectifier.voltage * Kpm * Kg));
        if (!innerLimited && !isAlgebraicOnly(sMode)) {
            if (hasRegulatorLag) {
                matrixData.assign(algebraicRow,
                                  differentialRow +
                                      stateIndex(regulatorLagState,
                                                 hasVoltageMeasurement,
                                                 hasRegulatorLag),
                                  rectifier.voltage * Kpm);
            } else if (!outerLimited) {
                matrixData.assign(algebraicRow,
                                  differentialRow + outerIndex,
                                  rectifier.voltage * Kpm);
                if (!hasVoltageMeasurement) {
                    matrixData.assignCheckCol(algebraicRow,
                                              inputLocs[exciterVoltageInLocation],
                                              -rectifier.voltage * Kpm * Kpr);
                }
            }
            matrixData.assign(algebraicRow, differentialRow + innerIndex, rectifier.voltage);
        }
        const std::array<index_t, 5> signalIndices{exciterIdInLocation,
                                                   exciterIqInLocation,
                                                   exciterVdInLocation,
                                                   exciterVqInLocation,
                                                   exciterXadIfdInLocation};
        for (index_t ii = 0; ii < 5; ++ii) {
            matrixData.assignCheckCol(algebraicRow,
                                      inputLocs[signalIndices[ii]],
                                      innerOutput * rectifier.derivatives[ii]);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    if (hasVoltageMeasurement) {
        matrixData.assign(differentialRow,
                          differentialRow,
                          -1.0 / Tr - stateData.cj);
        matrixData.assignCheckCol(differentialRow,
                                  inputLocs[exciterVoltageInLocation],
                                  1.0 / Tr);
    }
    const bool outerBlocked =
        integrationBlocked(state[outerIndex], Vrmin / Kpr, Vrmax / Kpr, Kir * outerError);
    const auto outerRow = differentialRow + outerIndex;
    matrixData.assign(outerRow, outerRow, -stateData.cj);
    if (!outerBlocked) {
        if (hasVoltageMeasurement) {
            matrixData.assign(outerRow, differentialRow, -Kir);
        } else {
            matrixData.assignCheckCol(outerRow, inputLocs[exciterVoltageInLocation], -Kir);
        }
        matrixData.assignCheckCol(outerRow, inputLocs[exciterVssInLocation], Kir);
    }
    if (hasRegulatorLag) {
        const auto regulatorIndex =
            stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag);
        const auto regulatorRow = differentialRow + regulatorIndex;
        matrixData.assign(regulatorRow, regulatorRow, -1.0 / Ta - stateData.cj);
        if (!outerLimited) {
            if (hasVoltageMeasurement) {
                matrixData.assign(regulatorRow, differentialRow, -Kpr / Ta);
            } else {
                matrixData.assignCheckCol(regulatorRow,
                                          inputLocs[exciterVoltageInLocation],
                                          -Kpr / Ta);
            }
            matrixData.assign(regulatorRow, outerRow, 1.0 / Ta);
            matrixData.assignCheckCol(regulatorRow,
                                      inputLocs[exciterVssInLocation],
                                      Kpr / Ta);
        }
    }
    const bool innerBlocked =
        integrationBlocked(state[innerIndex], Vmmin / Kpm, Vmmax / Kpm, Kim * innerError);
    const auto innerRow = differentialRow + innerIndex;
    matrixData.assign(innerRow, innerRow, -stateData.cj);
    if (!innerBlocked) {
        if (hasRegulatorLag) {
            matrixData.assign(innerRow,
                              differentialRow +
                                  stateIndex(regulatorLagState,
                                             hasVoltageMeasurement,
                                             hasRegulatorLag),
                              Kim);
        } else if (!outerLimited) {
            matrixData.assign(innerRow, outerRow, Kim);
            if (!hasVoltageMeasurement) {
                matrixData.assignCheckCol(innerRow,
                                          inputLocs[exciterVoltageInLocation],
                                          -Kim * Kpr);
            }
        }
        matrixData.assign(innerRow, algebraicRow, -Kim * Kg);
    }
}

void ExciterESST4B::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    const auto outerIndex = stateIndex(outerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const auto innerIndex = stateIndex(innerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    const auto diffSize = 2 + static_cast<index_t>(hasVoltageMeasurement) +
        static_cast<index_t>(hasRegulatorLag);
    for (index_t ii = 0; ii < diffSize; ++ii) {
        m_state[ii + 1] += timeStep * m_dstate_dt[ii + 1];
    }
    m_state[outerIndex + 1] = std::clamp(m_state[outerIndex + 1],
                                         static_cast<double>(Vrmin / Kpr),
                                         static_cast<double>(Vrmax / Kpr));
    m_state[innerIndex + 1] = std::clamp(m_state[innerIndex + 1],
                                         static_cast<double>(Vmmin / Kpm),
                                         static_cast<double>(Vmmax / Kpm));
    const double measuredVoltage = hasVoltageMeasurement ?
        m_state[voltageMeasurementState + 1] :
        inputs[exciterVoltageInLocation];
    const double outerError = Vref + vBias + inputs[exciterVssInLocation] - measuredVoltage;
    const double regulatorOutput = hasRegulatorLag ?
        m_state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag) + 1] :
        std::clamp(Kpr * outerError + m_state[outerIndex + 1],
                   static_cast<double>(Vrmin),
                   static_cast<double>(Vrmax));
    const double innerError = regulatorOutput - Kg * m_state[0];
    const double innerOutput = std::clamp(Kpm * innerError + m_state[innerIndex + 1],
                                          static_cast<double>(Vmmin),
                                          static_cast<double>(Vmmax));
    m_state[0] = rectifierVoltage(inputs) * innerOutput;
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
    stringVec names{"efd"};
    if (Tr > 0.0) {
        names.emplace_back("vmeas");
    }
    names.emplace_back("vrint");
    if (Ta > 0.0) {
        names.emplace_back("va");
    }
    names.emplace_back("vmint");
    return names;
}

index_t ExciterESST4B::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    if (field == "vmeas") {
        return hasVoltageMeasurement ? offset + voltageMeasurementState : kInvalidLocation;
    }
    if (field == "vrint") {
        return offset + stateIndex(outerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    }
    if (field == "va") {
        return hasRegulatorLag ?
            offset + stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag) :
            kInvalidLocation;
    }
    if (field == "vmint") {
        return offset + stateIndex(innerIntegralState, hasVoltageMeasurement, hasRegulatorLag);
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
