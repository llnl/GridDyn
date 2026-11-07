/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorGPWSCC.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::governors {
namespace {
    constexpr index_t pmechState = 0;
    constexpr index_t filterState = 0;
    constexpr index_t integratorState = 1;
    constexpr index_t derivativeState = 2;
    constexpr index_t electricalPowerState = 3;
    constexpr index_t valveState = 4;
    constexpr index_t gateState = 5;
    constexpr index_t turbineState = 6;
    constexpr double initializationTolerance = 1e-7;
}  // namespace

GovernorGPWSCC::GovernorGPWSCC(const std::string& objName): Governor(objName)
{
    m_inputSize = 3;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
    updateOutputLimits();
}

const ModelDescription& GovernorGPWSCC::getModelDescription() const
{
    static const auto description = ModelDescription::define<GovernorGPWSCC>(
        "GovernorGPWSCC",
        &Governor::getModelDescription(),
        {parameter({"mwcap"},
                   &GovernorGPWSCC::MWCap,
                   units::MW,
                   "Turbine MW capacity",
                   parameterValidators::positive),
         parameter({"mvabase", "mva"},
                   &GovernorGPWSCC::MVABase,
                   units::MW,
                   "Machine MVA base",
                   parameterValidators::positive),
         parameter({"r"},
                   &GovernorGPWSCC::R,
                   units::one,
                   "Speed droop",
                   parameterValidators::nonnegative),
         parameter({"td"},
                   &GovernorGPWSCC::Td,
                   units::second,
                   "Derivative filter time",
                   parameterValidators::positive),
         parameter({"tf"},
                   &GovernorGPWSCC::Tf,
                   units::second,
                   "Electrical power filter time",
                   parameterValidators::positive),
         parameter({"tp"},
                   &GovernorGPWSCC::Tp,
                   units::second,
                   "Power filter time",
                   parameterValidators::positive),
         parameter({"velopen"},
                   &GovernorGPWSCC::Velopen,
                   units::one,
                   "Gate opening rate",
                   parameterValidators::nonnegative),
         parameter({"velclose"},
                   &GovernorGPWSCC::Velclose,
                   units::one,
                   "Gate closing rate",
                   parameterValidators::nonpositive),
         parameter({"kp"}, &GovernorGPWSCC::Kp, units::one, "Proportional gain"),
         parameter({"kd"}, &GovernorGPWSCC::Kd, units::one, "Derivative gain"),
         parameter({"ki"}, &GovernorGPWSCC::Ki, units::one, "Integral gain"),
         parameter({"kg"},
                   &GovernorGPWSCC::Kg,
                   units::one,
                   "Gate servo gain",
                   parameterValidators::nonnegative),
         parameter({"tturb"},
                   &GovernorGPWSCC::Tturb,
                   units::second,
                   "Turbine time constant",
                   parameterValidators::nonnegative),
         parameter({"aturb"}, &GovernorGPWSCC::Aturb, units::one, "Turbine lead coefficient"),
         parameter({"bturb"}, &GovernorGPWSCC::Bturb, units::one, "Turbine lag coefficient"),
         parameter({"tt"},
                   &GovernorGPWSCC::Tt,
                   units::second,
                   "Transducer time constant",
                   parameterValidators::nonnegative),
         parameter({"db1"},
                   &GovernorGPWSCC::db1,
                   units::one,
                   "Speed deadband width",
                   parameterValidators::nonnegative),
         parameter({"eps"},
                   &GovernorGPWSCC::eps,
                   units::one,
                   "Deadband release margin",
                   parameterValidators::nonnegative),
         parameter({"db2"},
                   &GovernorGPWSCC::db2,
                   units::one,
                   "Gate deadband width",
                   parameterValidators::nonnegative),
         parameter(
             {"gmax", "gatemax"}, &GovernorGPWSCC::gateMaximum, units::one, "Maximum gate opening"),
         parameter(
             {"gmin", "gatemin"}, &GovernorGPWSCC::gateMinimum, units::one, "Minimum gate opening"),
         parameter({"paux"}, &GovernorGPWSCC::Paux, units::puMW, "Auxiliary power"),
         parameter({"gv1"}, &GovernorGPWSCC::curveGate, 0, units::one, "Gate curve point 1"),
         parameter({"pgv1"}, &GovernorGPWSCC::curvePower, 0, units::one, "Power curve point 1"),
         parameter({"gv2"}, &GovernorGPWSCC::curveGate, 1, units::one, "Gate curve point 2"),
         parameter({"pgv2"}, &GovernorGPWSCC::curvePower, 1, units::one, "Power curve point 2"),
         parameter({"gv3"}, &GovernorGPWSCC::curveGate, 2, units::one, "Gate curve point 3"),
         parameter({"pgv3"}, &GovernorGPWSCC::curvePower, 2, units::one, "Power curve point 3"),
         parameter({"gv4"}, &GovernorGPWSCC::curveGate, 3, units::one, "Gate curve point 4"),
         parameter({"pgv4"}, &GovernorGPWSCC::curvePower, 3, units::one, "Power curve point 4"),
         parameter({"gv5"}, &GovernorGPWSCC::curveGate, 4, units::one, "Gate curve point 5"),
         parameter({"pgv5"}, &GovernorGPWSCC::curvePower, 4, units::one, "Power curve point 5"),
         parameter({"gv6"}, &GovernorGPWSCC::curveGate, 5, units::one, "Gate curve point 6"),
         parameter({"pgv6"}, &GovernorGPWSCC::curvePower, 5, units::one, "Power curve point 6"),
         localState("initialized_input_reference", &GovernorGPWSCC::initializedInputReference),
         state("pmech", units::puMW, "Mechanical power"),
         state("td_filter", units::one, "Speed filter"),
         state("pid_integral", units::one, "PID integral"),
         state("derivative_filter", units::one, "Derivative filter"),
         state("pelec_filter", units::one, "Electrical power filter"),
         state("valve", units::one, "Valve state"),
         state("gate", units::one, "Gate position"),
         state("turbine", units::one, "Turbine state")});
    return description;
}

CoreObject* GovernorGPWSCC::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorGPWSCC, Governor>(this, obj);
    if (out == nullptr) {
        return obj;
    }
#define COPY(parameter) out->parameter = parameter
    COPY(MWCap);
    COPY(MVABase);
    COPY(R);
    COPY(Td);
    COPY(Tf);
    COPY(Tp);
    COPY(Velopen);
    COPY(Velclose);
    COPY(Kp);
    COPY(Kd);
    COPY(Ki);
    COPY(Kg);
    COPY(Tturb);
    COPY(Aturb);
    COPY(Bturb);
    COPY(Tt);
    COPY(db1);
    COPY(eps);
    COPY(db2);
    COPY(gateMaximum);
    COPY(gateMinimum);
    COPY(Paux);
    COPY(curveGate);
    COPY(curvePower);
    COPY(initializedInputReference);
#undef COPY
    out->updateOutputLimits();
    return out;
}

double GovernorGPWSCC::powerScale() const
{
    return MWCap / MVABase;
}

bool GovernorGPWSCC::hasIdentityCurve() const
{
    return std::ranges::all_of(curveGate, [](double value) { return value == 0.0; }) &&
        std::ranges::all_of(curvePower, [](double value) { return value == 0.0; });
}

std::size_t GovernorGPWSCC::lastCurvePoint() const
{
    for (std::size_t index = curveGate.size(); index-- > 0U;) {
        if ((curveGate[index] != 0.0) || (curvePower[index] != 0.0)) {
            return index;
        }
    }
    return 0U;
}

bool GovernorGPWSCC::hasUsableCurve() const
{
    if (hasIdentityCurve()) {
        return true;
    }
    const auto last = lastCurvePoint();
    if (last == 0U) {
        return false;
    }
    for (std::size_t index = 1U; index <= last; ++index) {
        if (!std::isfinite(curveGate[index - 1U]) || !std::isfinite(curvePower[index - 1U]) ||
            !std::isfinite(curveGate[index]) || !std::isfinite(curvePower[index]) ||
            (curveGate[index] <= curveGate[index - 1U]) ||
            (curvePower[index] < curvePower[index - 1U])) {
            return false;
        }
    }
    return true;
}

GovernorGPWSCC::DeadbandEvaluation GovernorGPWSCC::evaluateDeadband(double value, double width)
{
    if (value > width) {
        return {.value = value - width, .derivative = 1.0};
    }
    if (value < -width) {
        return {.value = value + width, .derivative = 1.0};
    }
    return {.value = 0.0, .derivative = 0.0};
}

GovernorGPWSCC::CurveEvaluation GovernorGPWSCC::evaluateCurve(double gate) const
{
    if (!hasUsableCurve() || hasIdentityCurve()) {
        return {.value = gate, .slope = 1.0};
    }
    const auto last = lastCurvePoint();
    const auto evaluateSegment = [this, gate](std::size_t left, std::size_t right) {
        const double slope =
            (curvePower[right] - curvePower[left]) / (curveGate[right] - curveGate[left]);
        return CurveEvaluation{.value = curvePower[left] + (slope * (gate - curveGate[left])),
                               .slope = slope};
    };
    if (gate <= curveGate[0]) {
        return evaluateSegment(0U, 1U);
    }
    for (std::size_t index = 1U; index <= last; ++index) {
        if (gate <= curveGate[index]) {
            return evaluateSegment(index - 1U, index);
        }
    }
    return evaluateSegment(last - 1U, last);
}

double GovernorGPWSCC::inverseCurve(double power) const
{
    if (!hasUsableCurve() || hasIdentityCurve()) {
        return power;
    }
    const auto last = lastCurvePoint();
    const auto invertSegment = [this, power](std::size_t left, std::size_t right) {
        const double deltaPower = curvePower[right] - curvePower[left];
        if (deltaPower <= initializationTolerance) {
            return curveGate[right];
        }
        return curveGate[left] +
            ((power - curvePower[left]) * (curveGate[right] - curveGate[left]) / deltaPower);
    };
    if (power <= curvePower[0]) {
        return invertSegment(0U, 1U);
    }
    for (std::size_t index = 1U; index <= last; ++index) {
        if (power <= curvePower[index]) {
            return invertSegment(index - 1U, index);
        }
    }
    return invertSegment(last - 1U, last);
}

double GovernorGPWSCC::gateOutput(double rawGate) const
{
    return evaluateDeadband(rawGate, db2).value;
}

double GovernorGPWSCC::inverseGateOutput(double gate) const
{
    if (gate > 0.0) {
        return gate + db2;
    }
    if (gate < 0.0) {
        return gate - db2;
    }
    return 0.0;
}

void GovernorGPWSCC::updateOutputLimits()
{
    const double scale = powerScale();
    if (!std::isfinite(scale) || (scale <= 0.0)) {
        return;
    }
    Pmin = scale * evaluateCurve(gateOutput(gateMinimum)).value;
    Pmax = scale * evaluateCurve(gateOutput(gateMaximum)).value;
}

void GovernorGPWSCC::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const std::array<double, 25> parameters{
        MWCap,       MVABase, R,        Td,    Tf,
        Tp,          Velopen, Velclose, Kp,    Kd,
        Ki,          Kg,      Tturb,    Aturb, Bturb,
        Tt,          db1,     eps,      db2,   gateMaximum,
        gateMinimum, Paux,    Pmin,     Pmax,  initializedInputReference};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        !hasUsableCurve() || (MWCap <= 0.0) || (MVABase <= 0.0) || (R < 0.0) || (Td <= 0.0) ||
        (Tf <= 0.0) || (Tp <= 0.0) || (Velopen < 0.0) || (Velclose > 0.0) || (Velopen < Velclose) ||
        (Kg < 0.0) || (Tturb < 0.0) || (Tt < 0.0) || ((Tturb > 0.0) && (Bturb <= 0.0)) ||
        (db1 < 0.0) || (eps < 0.0) || (db2 < 0.0) || (gateMaximum < gateMinimum)) {
        throw InvalidParameterValue("GPWSCC parameters, curve, or gate limits");
    }
    updateOutputLimits();
    if (Pmax < Pmin) {
        throw InvalidParameterValue("GPWSCC gate curve decreases over its operating range");
    }
    setInitialLimitPolicy(flags);
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 7;
    local.jacSize = 32;
    prevTime = time0;
}

void GovernorGPWSCC::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[pmechState]) ||
        (inputs.size() <= govElectricalPowerInLocation) ||
        !std::isfinite(inputs[govOmegaInLocation]) ||
        !std::isfinite(inputs[govElectricalPowerInLocation])) {
        throw InvalidParameterValue("GPWSCC initial mechanical or electrical power");
    }
    const double scale = powerScale();
    const double desiredPower = desiredOutput[pmechState];
    const double originalGateMaximum = gateMaximum;
    const double desiredTurbinePower = desiredPower / scale;
    const double desiredCurveGate = inverseCurve(desiredTurbinePower);
    const double initialGate = inverseGateOutput(desiredCurveGate);
    if (initialGate < gateMinimum - initializationTolerance) {
        throw InvalidParameterValue("GPWSCC initial gate is below Pmin");
    }
    if (initialGate > gateMaximum + initializationTolerance) {
        if (!adjustInitialUpperLimit(desiredPower, "GPWSCC initial mechanical power")) {
            throw InvalidParameterValue("GPWSCC initial gate exceeds Pmax");
        }
        gateMaximum = initialGate;
        updateOutputLimits();
    }
    const double electricalPower = inputs[govElectricalPowerInLocation] / scale;
    initializedInputReference = desiredTurbinePower;
    Paux = R * electricalPower;

    m_state[pmechState] = desiredPower;
    double* state = m_state.data() + 1;
    state[filterState] = 0.0;
    state[integratorState] = initialGate;
    state[derivativeState] = 0.0;
    state[electricalPowerState] = electricalPower;
    state[valveState] = 0.0;
    state[gateState] = initialGate;
    state[turbineState] = desiredTurbinePower;
    const double correction = std::max(0.0, gateMaximum - originalGateMaximum);
    const double margin = std::max(0.2 * correction, 1e-4);
    const bool upperActive = (initialGate >= gateMaximum) && (Kg > 0.0) && (Velopen > 0.0);
    const bool lowerActive = (initialGate <= gateMinimum) && (Kg > 0.0) && (Velclose < 0.0);
    if (upperActive || lowerActive) {
        const double signedMargin = upperActive ? margin : -margin;
        state[filterState] = signedMargin;
        state[derivativeState] = signedMargin;
        state[integratorState] += (1.0 + std::abs(Kp)) * signedMargin;
        const double controllerOverdrive = (1.0 + std::abs(Kp) + Kp) * signedMargin;
        state[valveState] = Kg * controllerOverdrive;
        const double speedError =
            evaluateDeadband(1.0 - inputs[govOmegaInLocation], db1 + eps).value;
        const double initialFeedback = (Tt > 0.0) ? electricalPower : initialGate;
        const double baselineError = speedError + Paux - (R * initialFeedback);
        initialSetpointOverdrive =
            scale * (signedMargin - baselineError + ((Tt > 0.0) ? 0.0 : R * controllerOverdrive));
    }
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = desiredPower;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

GovernorGPWSCC::Signals GovernorGPWSCC::evaluate(const IOdata& inputs, const double state[]) const
{
    const double scale = powerScale();
    const double omega = inputs[govOmegaInLocation];
    const auto speed = evaluateDeadband(1.0 - omega, db1 + eps);
    const double controllerOutput = (Kp * state[filterState]) + state[integratorState] +
        (Kd * (state[filterState] - state[derivativeState]) / Tf);
    const double feedback = (Tt > 0.0) ? state[electricalPowerState] : controllerOutput;
    const double setpointChange =
        (effectivePowerSetpoint(inputs) / scale) - initializedInputReference;
    const double error = speed.value + Paux + setpointChange - (R * feedback);
    const double rawRate = state[valveState];
    const double limitedRate =
        std::clamp(rawRate, static_cast<double>(Velclose), static_cast<double>(Velopen));
    const bool gateAtUpper = state[gateState] >= gateMaximum;
    const bool gateAtLower = state[gateState] <= gateMinimum;
    const bool gateRateBlocked =
        (gateAtUpper && (limitedRate > 0.0)) || (gateAtLower && (limitedRate < 0.0));
    const bool gateRateActive = !gateRateBlocked;
    const auto curve = evaluateCurve(gateOutput(state[gateState]));
    const auto gateDeadband = evaluateDeadband(state[gateState], db2);
    const double gatePowerDerivative = curve.slope * gateDeadband.derivative;
    const double turbinePower = (Tturb > 0.0) ?
        ((Aturb / Bturb) * curve.value) + ((1.0 - (Aturb / Bturb)) * state[turbineState]) :
        curve.value;
    const bool integralBlocked =
        ((gateAtUpper && (controllerOutput >= state[gateState]) && (state[filterState] > 0.0)) ||
         (gateAtLower && (controllerOutput <= state[gateState]) && (state[filterState] < 0.0)));
    return {.error = error,
            .controllerOutput = controllerOutput,
            .gateRate = limitedRate,
            .gatePower = curve.value,
            .turbinePower = turbinePower,
            .mechanicalPower = scale * turbinePower,
            .speedDerivative = -speed.derivative,
            .gatePowerDerivative = gatePowerDerivative,
            .integralBlocked = integralBlocked,
            .gateRateActive = gateRateActive};
}

double GovernorGPWSCC::mechanicalPower(const Signals& signals)
{
    return signals.mechanicalPower;
}

void GovernorGPWSCC::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[pmechState] =
            mechanicalPower(evaluate(inputs, loc.diffStateLoc)) - loc.algStateLoc[pmechState];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t index = 0; index < 7; ++index) {
        loc.destDiffLoc[index] -= loc.dstateLoc[index];
    }
}

void GovernorGPWSCC::derivative(const IOdata& inputs,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    double* stateDerivative = loc.destDiffLoc;
    const auto signals = evaluate(inputs, state);
    stateDerivative[filterState] = (signals.error - state[filterState]) / Td;
    stateDerivative[integratorState] = signals.integralBlocked ? 0.0 : Ki * state[filterState];
    stateDerivative[derivativeState] = (state[filterState] - state[derivativeState]) / Tf;
    stateDerivative[electricalPowerState] = (Tt > 0.0) ?
        ((inputs[govElectricalPowerInLocation] / powerScale()) - state[electricalPowerState]) / Tt :
        0.0;
    stateDerivative[valveState] =
        ((Kg * (signals.controllerOutput - state[gateState])) - state[valveState]) / Tp;
    stateDerivative[gateState] = signals.gateRateActive ? signals.gateRate : 0.0;
    stateDerivative[turbineState] =
        (Tturb > 0.0) ? (signals.gatePower - state[turbineState]) / (Bturb * Tturb) : 0.0;
}

void GovernorGPWSCC::algebraicUpdate(const IOdata& inputs,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[pmechState] = mechanicalPower(evaluate(inputs, loc.diffStateLoc));
    }
}

void GovernorGPWSCC::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const index_t alg = loc.algOffset;
    const index_t diff = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    const auto signals = evaluate(inputs, state);
    const double scale = powerScale();
    const double controllerFilterDerivative = Kp + (Kd / Tf);
    const double controllerIntegratorDerivative = 1.0;
    const double controllerDerivativeDerivative = -Kd / Tf;
    const bool filteredFeedback = Tt > 0.0;
    const double errorFilterDerivative = filteredFeedback ? 0.0 : -R * controllerFilterDerivative;
    const double errorIntegratorDerivative =
        filteredFeedback ? 0.0 : -R * controllerIntegratorDerivative;
    const double errorDerivativeDerivative =
        filteredFeedback ? 0.0 : -R * controllerDerivativeDerivative;

    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg, alg, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            const double gateOutputDerivative = (Tturb > 0.0) ?
                scale * (Aturb / Bturb) * signals.gatePowerDerivative :
                scale * signals.gatePowerDerivative;
            matrixData.assign(alg, diff + gateState, gateOutputDerivative);
            if (Tturb > 0.0) {
                matrixData.assign(alg, diff + turbineState, scale * (1.0 - (Aturb / Bturb)));
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    matrixData.assign(diff + filterState,
                      diff + filterState,
                      ((errorFilterDerivative - 1.0) / Td) - stateData.cj);
    matrixData.assign(diff + filterState, diff + integratorState, errorIntegratorDerivative / Td);
    matrixData.assign(diff + filterState, diff + derivativeState, errorDerivativeDerivative / Td);
    if (filteredFeedback) {
        matrixData.assign(diff + filterState, diff + electricalPowerState, -R / Td);
    }
    matrixData.assignCheckCol(diff + filterState,
                              inputLocs[govOmegaInLocation],
                              signals.speedDerivative / Td);
    matrixData.assignCheckCol(diff + filterState, inputLocs[govpSetInLocation], 1.0 / (scale * Td));

    if (signals.integralBlocked) {
        matrixData.assign(diff + integratorState, diff + integratorState, -stateData.cj);
    } else {
        matrixData.assign(diff + integratorState, diff + filterState, Ki);
        matrixData.assign(diff + integratorState, diff + integratorState, -stateData.cj);
    }
    matrixData.assign(diff + derivativeState, diff + filterState, 1.0 / Tf);
    matrixData.assign(diff + derivativeState, diff + derivativeState, (-1.0 / Tf) - stateData.cj);

    matrixData.assign(diff + electricalPowerState,
                      diff + electricalPowerState,
                      (filteredFeedback ? -1.0 / Tt : 0.0) - stateData.cj);
    if (filteredFeedback) {
        matrixData.assignCheckCol(diff + electricalPowerState,
                                  inputLocs[govElectricalPowerInLocation],
                                  1.0 / (scale * Tt));
    }

    matrixData.assign(diff + valveState, diff + filterState, Kg * controllerFilterDerivative / Tp);
    matrixData.assign(diff + valveState,
                      diff + integratorState,
                      Kg * controllerIntegratorDerivative / Tp);
    matrixData.assign(diff + valveState,
                      diff + derivativeState,
                      Kg * controllerDerivativeDerivative / Tp);
    matrixData.assign(diff + valveState, diff + valveState, (-1.0 / Tp) - stateData.cj);
    matrixData.assign(diff + valveState, diff + gateState, -Kg / Tp);

    matrixData.assign(diff + gateState, diff + gateState, -stateData.cj);
    if (signals.gateRateActive && (state[valveState] > Velclose) && (state[valveState] < Velopen)) {
        matrixData.assign(diff + gateState, diff + valveState, 1.0);
    }

    matrixData.assign(diff + turbineState,
                      diff + turbineState,
                      (Tturb > 0.0) ? (-1.0 / (Bturb * Tturb)) - stateData.cj : -stateData.cj);
    if (Tturb > 0.0) {
        matrixData.assign(diff + turbineState,
                          diff + gateState,
                          signals.gatePowerDerivative / (Bturb * Tturb));
    }
}

void GovernorGPWSCC::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    for (index_t index = 0; index < 7; ++index) {
        m_state[diffOffset + index] += timeStep * m_dstate_dt[diffOffset + index];
    }
    m_state[diffOffset + gateState] = std::clamp(m_state[diffOffset + gateState],
                                                 static_cast<double>(gateMinimum),
                                                 static_cast<double>(gateMaximum));
    m_state[pmechState] = mechanicalPower(evaluate(inputs, m_state.data() + diffOffset));
    prevTime = time;
}

void GovernorGPWSCC::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorGPWSCC::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(
            GovernorGPWSCC::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        if (!param.empty() && param.front() != '#') {
            updateOutputLimits();
        }
        return;
    }
    Governor::set(param, val, unitType);
}

double GovernorGPWSCC::get(std::string_view param, units::unit unitType) const
{
    if (const auto result = GovernorGPWSCC::getModelDescription().tryGet(*this, param, unitType)) {
        return *result;
    }
    return Governor::get(param, unitType);
}

stringVec GovernorGPWSCC::localStateNames() const
{
    return {"pmech",
            "td_filter",
            "pid_integral",
            "derivative_filter",
            "pelec_filter",
            "valve",
            "gate",
            "turbine"};
}

index_t GovernorGPWSCC::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto diff = offsets.getDiffOffset(sMode);
    if ((field == "filter") || (field == "td_filter")) {
        return diff;
    }
    if ((field == "integral") || (field == "pid_integral")) {
        return (diff == kNullLocation) ? diff : diff + integratorState;
    }
    if ((field == "derivative") || (field == "derivative_filter")) {
        return (diff == kNullLocation) ? diff : diff + derivativeState;
    }
    if ((field == "pelec") || (field == "pelec_filter")) {
        return (diff == kNullLocation) ? diff : diff + electricalPowerState;
    }
    if (field == "valve") {
        return (diff == kNullLocation) ? diff : diff + valveState;
    }
    if (field == "gate") {
        return (diff == kNullLocation) ? diff : diff + gateState;
    }
    if (field == "turbine") {
        return (diff == kNullLocation) ? diff : diff + turbineState;
    }
    return kInvalidLocation;
}
}  // namespace griddyn::governors
