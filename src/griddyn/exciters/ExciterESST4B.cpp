/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterESST4B.h"

#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
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

const ModelDescription& ExciterESST4B::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterESST4B>(
        "ExciterESST4B",
        &Exciter::getModelDescription(),
        {parameter({"tr"}, &ExciterESST4B::Tr, units::second, "Voltage measurement time constant"),
         parameter({"kpr"}, &ExciterESST4B::Kpr, units::one, "Kpr gain or coefficient"),
         parameter({"kir"}, &ExciterESST4B::Kir, units::one, "Kir gain or coefficient"),
         parameter({"ta"}, &ExciterESST4B::Ta, units::second, "Ta time constant"),
         parameter({"kpm"}, &ExciterESST4B::Kpm, units::one, "Kpm gain or coefficient"),
         parameter({"kim"}, &ExciterESST4B::Kim, units::one, "Kim gain or coefficient"),
         parameter({"vmmax"}, &ExciterESST4B::Vmmax, units::one, "Vmmax upper limit"),
         parameter({"vmmin"}, &ExciterESST4B::Vmmin, units::one, "Vmmin lower limit"),
         parameter({"kg"}, &ExciterESST4B::Kg, units::one, "Kg gain or coefficient"),
         parameter({"kp"}, &ExciterESST4B::Kp, units::one, "Kp gain or coefficient"),
         parameter({"ki"}, &ExciterESST4B::Ki, units::one, "Ki gain or coefficient"),
         parameter({"vbmax"}, &ExciterESST4B::Vbmax, units::one, "Vbmax upper limit"),
         parameter({"kc"}, &ExciterESST4B::Kc, units::one, "Kc gain or coefficient"),
         parameter({"xl"}, &ExciterESST4B::Xl, units::one, "Xl model setting"),
         parameter(
             {"thetap", "theta_p"}, &ExciterESST4B::ThetaP, units::rad, "ThetaP model setting"),
         state({"efd", "field"}, units::one, 0, "Field voltage"),
         state<ExciterESST4B>(
             {"vmeas"},
             units::one,
             [](const auto& model) { return model.Tr > 0.0 ? 1 : kInvalidLocation; },
             "Measured terminal voltage"),
         state<ExciterESST4B>(
             {"vrint"},
             units::one,
             [](const auto& model) {
                 return 1 + stateIndex(outerIntegralState, model.Tr > 0.0, model.Ta > 0.0);
             },
             "Regulator integral state"),
         state<ExciterESST4B>(
             {"va"},
             units::one,
             [](const auto& model) {
                 return model.Ta > 0.0 ? 1 + stateIndex(regulatorLagState, model.Tr > 0.0, true) :
                                         kInvalidLocation;
             },
             "Amplifier state"),
         state<ExciterESST4B>(
             {"vmint"},
             units::one,
             [](const auto& model) {
                 return 1 + stateIndex(innerIntegralState, model.Tr > 0.0, model.Ta > 0.0);
             },
             "Voltage regulator integral state")});
    return description;
}

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
        !std::isfinite(Xl) || !std::isfinite(ThetaP) || (Tr < 0.0) || (Ta < 0.0) || (Kpr <= 0.0) ||
        (Kpm <= 0.0) || (Kir < 0.0) || (Kim < 0.0) || (Vrmax < Vrmin) || (Vmmax < Vmmin) ||
        (Vbmax <= 0.0)) {
        throw InvalidParameterValue("ESST4B gains, time constants, or limits");
    }
    const bool hasVoltageMeasurement = (Tr > 0.0);
    const bool hasRegulatorLag = (Ta > 0.0);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize =
        2 + static_cast<index_t>(hasVoltageMeasurement) + static_cast<index_t>(hasRegulatorLag);
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
    const double originalInnerUpperLimit = Vmmax;
    const double originalRegulatorUpperLimit = Vrmax;
    if ((innerOutput < Vmmin - initializationTolerance) ||
        (regulatorOutput < Vrmin - initializationTolerance)) {
        const std::string objectPath = fullObjectName(this);
        throw InvalidParameterValue("ESST4B initial output below lower limit for " + objectPath +
                                    ": Efd=" + std::to_string(fieldVoltage) +
                                    ", rectifier voltage=" + std::to_string(rectifierVoltageValue) +
                                    ", inner output=" + std::to_string(innerOutput) +
                                    " (Vmmin=" + std::to_string(Vmmin) +
                                    "), regulator output=" + std::to_string(regulatorOutput) +
                                    " (Vrmin=" + std::to_string(Vrmin) + ")");
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
    const double innerMargin = std::max(0.2 * std::max(0.0, Vmmax - originalInnerUpperLimit), 1e-4);
    if (innerOutput >= Vmmax) {
        state[innerIndex] += innerMargin;
    } else if (innerOutput <= Vmmin) {
        state[innerIndex] -= innerMargin;
    }
    // A nonzero stabilizer signal can leave the outer integrator moving at
    // initialization. Only add overdrive when the initial PI error is zero.
    if ((std::abs(inputs[exciterVssInLocation]) <= 1e-10) && (Kpr > 0.0) && (Kir > 0.0)) {
        const double regulatorMargin =
            std::max(0.2 * std::max(0.0, Vrmax - originalRegulatorUpperLimit), 1e-4);
        const double inputMargin = std::max(regulatorMargin / Kpr, regulatorMargin / Kir);
        if (regulatorOutput >= Vrmax) {
            vBias += inputMargin;
        } else if (regulatorOutput <= Vrmin) {
            vBias -= inputMargin;
        }
    }
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
    const double measuredVoltage =
        hasVoltageMeasurement ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
    const double regulatorOutput = hasRegulatorLag ?
        state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag)] :
        std::clamp(Kpr * (Vref + vBias + inputs[exciterVssInLocation] - measuredVoltage) +
                       state[outerIndex],
                   static_cast<double>(Vrmin),
                   static_cast<double>(Vrmax));
    if (hasAlgebraic(sMode)) {
        const double innerError = regulatorOutput - Kg * loc.algStateLoc[0];
        const double innerOutput = std::clamp(Kpm * innerError + state[innerIndex],
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
    const double measuredVoltage =
        hasVoltageMeasurement ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
    const double outerError = Vref + vBias + inputs[exciterVssInLocation] - measuredVoltage;
    const double outerUnlimited = Kpr * outerError + state[outerIndex];
    const double regulatorOutput =
        std::clamp(outerUnlimited, static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    const double innerError =
        (hasRegulatorLag ?
             state[stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag)] :
             regulatorOutput) -
        Kg * loc.algStateLoc[0];
    const double innerOutput = std::clamp(Kpm * innerError + state[innerIndex],
                                          static_cast<double>(Vmmin),
                                          static_cast<double>(Vmmax));
    if (hasVoltageMeasurement) {
        dst[voltageMeasurementState] =
            (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    }
    // VRMAX and VMMAX limit the PI outputs, not their additive integrator
    // states.  The latter can be offset by the proportional paths.
    dst[outerIndex] = integrationBlocked(regulatorOutput, Vrmin, Vrmax, Kir * outerError) ?
        0.0 :
        Kir * outerError;
    if (hasRegulatorLag) {
        const auto regulatorIndex =
            stateIndex(regulatorLagState, hasVoltageMeasurement, hasRegulatorLag);
        dst[regulatorIndex] = (regulatorOutput - state[regulatorIndex]) / Ta;
    }
    dst[innerIndex] =
        integrationBlocked(innerOutput, Vmmin, Vmmax, Kim * innerError) ? 0.0 : Kim * innerError;
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
    const double measuredVoltage =
        hasVoltageMeasurement ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
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
        matrixData.assign(differentialRow, differentialRow, -1.0 / Tr - stateData.cj);
        matrixData.assignCheckCol(differentialRow, inputLocs[exciterVoltageInLocation], 1.0 / Tr);
    }
    const bool outerBlocked = integrationBlocked(std::clamp(outerUnlimited,
                                                            static_cast<double>(Vrmin),
                                                            static_cast<double>(Vrmax)),
                                                 Vrmin,
                                                 Vrmax,
                                                 Kir * outerError);
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
            matrixData.assignCheckCol(regulatorRow, inputLocs[exciterVssInLocation], Kpr / Ta);
        }
    }
    const bool innerBlocked = integrationBlocked(std::clamp(innerUnlimited,
                                                            static_cast<double>(Vmmin),
                                                            static_cast<double>(Vmmax)),
                                                 Vmmin,
                                                 Vmmax,
                                                 Kim * innerError);
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
    const auto diffSize =
        2 + static_cast<index_t>(hasVoltageMeasurement) + static_cast<index_t>(hasRegulatorLag);
    for (index_t ii = 0; ii < diffSize; ++ii) {
        m_state[ii + 1] += timeStep * m_dstate_dt[ii + 1];
    }
    const double measuredVoltage = hasVoltageMeasurement ? m_state[voltageMeasurementState + 1] :
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

// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
