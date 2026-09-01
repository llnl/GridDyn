/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "GovernorGgov1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::governors {
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t peState = 0;
    constexpr index_t resetState = 1;
    constexpr index_t integralState = 2;
    constexpr index_t derivativeState = 3;
    constexpr index_t valveState = 4;
    constexpr index_t turbineState = 5;
    constexpr index_t temperatureLeadState = 6;
    constexpr index_t temperatureState = 7;
    constexpr index_t loadIntegralState = 8;
    constexpr index_t accelerationState = 9;
    constexpr double accelerationStep = 0.005;

    double deadZone(double value, double width)
    {
        if (value > width) {
            return value - width;
        }
        if (value < -width) {
            return value + width;
        }
        return 0.0;
    }
}  // namespace

struct GovernorGgov1::Signals {
    double mSpeedDeviation;
    double mLimitedError;
    double mNormalRequest;
    double mTemperatureRequest;
    double mAccelerationRequest;
    double mFuelRequest;
    double mValve;
    double mFuelFlow;
    double mTurbineInput;
    double mTurbineOutput;
    double mTemperatureInput;
    double mTemperatureLeadOutput;
    double mMechanicalPower;
};

GovernorGgov1::GovernorGgov1(const std::string& objName): Governor(objName)
{
    m_inputSize = 3;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}

CoreObject* GovernorGgov1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorGgov1, Governor>(this, obj);
    if (out == nullptr) {
        return obj;
    }
#define COPY(x) out->x = x
    COPY(Rselect);
    COPY(fuelFlag);
    COPY(R);
    COPY(Tpelec);
    COPY(maxerr);
    COPY(minerr);
    COPY(Kpgov);
    COPY(Kigov);
    COPY(Kdgov);
    COPY(Tdgov);
    COPY(Tact);
    COPY(Kturb);
    COPY(Wfnl);
    COPY(Tb);
    COPY(Tc);
    COPY(Teng);
    COPY(Tfload);
    COPY(Kpload);
    COPY(Kiload);
    COPY(Ldref);
    COPY(Dm);
    COPY(Ropen);
    COPY(Rclose);
    COPY(Kimw);
    COPY(Aset);
    COPY(Ka);
    COPY(TaAccel);
    COPY(Trate);
    COPY(db);
    COPY(Tsa);
    COPY(Tsb);
    COPY(Rup);
    COPY(Rdown);
    COPY(powerReference);
#undef COPY
    return out;
}

void GovernorGgov1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 33> parameters{R,     Tpelec, maxerr, minerr, Kpgov,  Kigov,   Kdgov,
                                            Tdgov, Pmax,   Pmin,   Tact,   Kturb,  Wfnl,    Tb,
                                            Tc,    Teng,   Tfload, Kpload, Kiload, Ldref,   Dm,
                                            Ropen, Rclose, Kimw,   Aset,   Ka,     TaAccel, Trate,
                                            db,    Tsa,    Tsb,    Rup,    Rdown};
    if (std::any_of(parameters.begin(), parameters.end(), [](double value) {
            return !std::isfinite(value);
        })) {
        throw InvalidParameterValue("GGOV1 parameters must be finite");
    }
    if (((Rselect != -2) && (Rselect != -1) && (Rselect != 0) && (Rselect != 1)) ||
        ((fuelFlag != 0) && (fuelFlag != 1)) || (Tpelec <= 0.0) || (Tdgov <= 0.0) ||
        (Tact <= 0.0) || (Tb <= 0.0) || (Tfload <= 0.0) || (TaAccel <= 0.0) || (Tsb <= 0.0) ||
        (Kturb <= 0.0) || (R < 0.0) || (Kpgov < 0.0) || (Kdgov < 0.0) || (Pmax < Pmin) ||
        (maxerr < minerr) || (Ropen < 0.0) || (Rclose > 0.0)) {
        throw InvalidParameterValue("GGOV1 selectors, gains, time constants, or limits");
    }
    if (std::abs(Teng) > 1e-9) {
        // TODO(phlpt): Support TENG with a reusable transport-delay/history component.
        // Do not substitute a first-order lag: that changes the GGOV1 diesel-engine dynamics.
        throw InvalidParameterValue("GGOV1 TENG transport delay is not supported");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 10;
    offsets.local().local.jacSize = 70;
    prevTime = time0;
}

void GovernorGgov1::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0]) ||
        !std::isfinite(inputs[govElectricalPowerInLocation])) {
        throw InvalidParameterValue("GGOV1 initial power");
    }
    const double power = desiredOutput[0];
    const double electricalPower = inputs[govElectricalPowerInLocation];
    const double fuel = power / Kturb + Wfnl;
    if ((fuel < Pmin - 1e-7) || (fuel > Pmax + 1e-7)) {
        throw InvalidParameterValue("GGOV1 initial valve position outside limits");
    }
    double* state = m_state.data() + 1;
    state[peState] = electricalPower;
    state[resetState] = 0.0;
    state[integralState] = fuel;
    state[derivativeState] = 0.0;
    state[valveState] = fuel;
    state[turbineState] = power;
    state[temperatureLeadState] = fuel;
    state[temperatureState] = fuel;
    state[loadIntegralState] = fuel;
    state[accelerationState] = 0.0;
    double feedback = 0.0;
    if (Rselect == 1) {
        feedback = electricalPower;
    } else if ((Rselect == -1) || (Rselect == -2)) {
        feedback = fuel;
    }
    powerReference = R * feedback;
    Pset = electricalPower;
    m_state[0] = power;
    fieldSet[govpSetInLocation] = Pset;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

GovernorGgov1::Signals GovernorGgov1::evaluate(const IOdata& inputs, const double state[]) const
{
    Signals signals{};
    const double omega = inputs[govOmegaInLocation];
    signals.mSpeedDeviation = omega - 1.0;
    const double temperatureError = Ldref / Kturb + Wfnl - state[temperatureState];
    signals.mTemperatureRequest =
        std::min(1.0, state[loadIntegralState] + Kpload * temperatureError);
    const double acceleration = (signals.mSpeedDeviation - state[accelerationState]) / TaAccel;
    signals.mAccelerationRequest =
        state[valveState] + Ka * accelerationStep * (Aset - acceleration);
    const auto evaluateNormalRequest = [this, &signals, state](double feedback) {
        const double error =
            deadZone(-signals.mSpeedDeviation + powerReference + state[resetState] - R * feedback,
                     db);
        const double limitedError =
            std::clamp(error, static_cast<double>(minerr), static_cast<double>(maxerr));
        const double derivativeOutput = Kdgov * (limitedError - state[derivativeState]) / Tdgov;
        return std::pair{limitedError,
                         Kpgov * limitedError + state[integralState] + derivativeOutput};
    };
    if (Rselect == -2) {
        // OpenIPSL feeds the post-selector, position-limited request GOVOUT1
        // back into the droop path for Rselect=-2.  Solve that scalar
        // algebraic loop; for the validated nonnegative gains it is monotone.
        double lower = Pmin;
        double upper = Pmax;
        for (int iteration = 0; iteration < 60; ++iteration) {
            const double request = 0.5 * (lower + upper);
            const auto [unusedError, normalRequest] = evaluateNormalRequest(request);
            const double selected = std::clamp(std::min({normalRequest,
                                                         signals.mTemperatureRequest,
                                                         signals.mAccelerationRequest}),
                                               static_cast<double>(Pmin),
                                               static_cast<double>(Pmax));
            if (request < selected) {
                lower = request;
            } else {
                upper = request;
            }
        }
        signals.mFuelRequest = 0.5 * (lower + upper);
        const auto [limitedError, normalRequest] = evaluateNormalRequest(signals.mFuelRequest);
        signals.mLimitedError = limitedError;
        signals.mNormalRequest = normalRequest;
    } else {
        double feedback = 0.0;
        if (Rselect == 1) {
            feedback = state[peState];
        } else if (Rselect == -1) {
            feedback = state[valveState];
        }
        const auto [limitedError, normalRequest] = evaluateNormalRequest(feedback);
        signals.mLimitedError = limitedError;
        signals.mNormalRequest = normalRequest;
        signals.mFuelRequest = std::clamp(std::min({signals.mNormalRequest,
                                                    signals.mTemperatureRequest,
                                                    signals.mAccelerationRequest}),
                                          static_cast<double>(Pmin),
                                          static_cast<double>(Pmax));
    }
    signals.mValve =
        std::clamp(state[valveState], static_cast<double>(Pmin), static_cast<double>(Pmax));
    signals.mFuelFlow = (fuelFlag == 1) ? omega * signals.mValve : signals.mValve;
    signals.mTurbineInput = Kturb * (signals.mFuelFlow - Wfnl);
    signals.mTurbineOutput =
        state[turbineState] + (Tc / Tb) * (signals.mTurbineInput - state[turbineState]);
    const double speedFactor = (Dm < 0.0) ? std::pow(omega, Dm) : 1.0;
    signals.mTemperatureInput = signals.mFuelFlow * speedFactor;
    signals.mTemperatureLeadOutput = state[temperatureLeadState] +
        (Tsa / Tsb) * (signals.mTemperatureInput - state[temperatureLeadState]);
    signals.mMechanicalPower = (Dm >= 0.0) ? signals.mTurbineOutput - Dm * signals.mSpeedDeviation :
                                             signals.mTurbineOutput;
    return signals;
}

void GovernorGgov1::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).mMechanicalPower - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t ii = 0; ii < 10; ++ii) {
            loc.destDiffLoc[ii] -= loc.dstateLoc[ii];
        }
    }
}

void GovernorGgov1::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = loc.diffStateLoc;
    double* stateDerivative = loc.destDiffLoc;
    const auto signals = evaluate(inputs, state);
    stateDerivative[peState] = (inputs[govElectricalPowerInLocation] - state[peState]) / Tpelec;
    const double resetDrive = Kimw * (inputs[govpSetInLocation] - state[peState]);
    const double resetLimit = 1.1 * std::abs(R);
    stateDerivative[resetState] = (((state[resetState] >= resetLimit) && (resetDrive > 0.0)) ||
                                   ((state[resetState] <= -resetLimit) && (resetDrive < 0.0))) ?
        0.0 :
        resetDrive;
    stateDerivative[integralState] = Kigov * signals.mLimitedError;
    stateDerivative[derivativeState] = (signals.mLimitedError - state[derivativeState]) / Tdgov;
    double rate = std::clamp((signals.mFuelRequest - signals.mValve) / Tact,
                             static_cast<double>(Rclose),
                             static_cast<double>(Ropen));
    if (((signals.mValve >= Pmax) && (rate > 0.0)) || ((signals.mValve <= Pmin) && (rate < 0.0))) {
        rate = 0.0;
    }
    stateDerivative[valveState] = rate;
    stateDerivative[turbineState] = (signals.mTurbineInput - state[turbineState]) / Tb;
    stateDerivative[temperatureLeadState] =
        (signals.mTemperatureInput - state[temperatureLeadState]) / Tsb;
    stateDerivative[temperatureState] =
        (signals.mTemperatureLeadOutput - state[temperatureState]) / Tfload;
    stateDerivative[loadIntegralState] = Kiload * (Ldref / Kturb + Wfnl - state[temperatureState]);
    stateDerivative[accelerationState] =
        (signals.mSpeedDeviation - state[accelerationState]) / TaAccel;
}

void GovernorGgov1::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    auto loc = offsets.getLocations(stateData, update, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).mMechanicalPower;
    }
}

void GovernorGgov1::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto algebraicRow = loc.algOffset;
    const auto differentialRow = loc.diffOffset;
    const double* state = loc.diffStateLoc;
    const auto signals = evaluate(inputs, state);
    const double omega = inputs[govOmegaInLocation];
    const double fuelDomega = (fuelFlag == 1) ? signals.mValve : 0.0;
    const double fuelDvalve = (fuelFlag == 1) ? omega : 1.0;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(algebraicRow, algebraicRow, -1.0);
        matrixData.assign(algebraicRow, differentialRow + turbineState, 1.0 - Tc / Tb);
        matrixData.assign(algebraicRow,
                          differentialRow + valveState,
                          (Tc / Tb) * Kturb * fuelDvalve);
        matrixData.assignCheckCol(algebraicRow,
                                  inputLocs[govOmegaInLocation],
                                  (Tc / Tb) * Kturb * fuelDomega - ((Dm >= 0.0) ? Dm : 0.0));
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(differentialRow + peState,
                      differentialRow + peState,
                      -1.0 / Tpelec - stateData.cj);
    matrixData.assignCheckCol(differentialRow + peState,
                              inputLocs[govElectricalPowerInLocation],
                              1.0 / Tpelec);
    matrixData.assign(differentialRow + resetState, differentialRow + resetState, -stateData.cj);
    const double resetDrive = Kimw * (inputs[govpSetInLocation] - state[peState]);
    const double resetLimit = 1.1 * std::abs(R);
    const bool resetBlocked = ((state[resetState] >= resetLimit) && (resetDrive > 0.0)) ||
        ((state[resetState] <= -resetLimit) && (resetDrive < 0.0));
    if (!resetBlocked) {
        matrixData.assign(differentialRow + resetState, differentialRow + peState, -Kimw);
        matrixData.assignCheckCol(differentialRow + resetState, inputLocs[govpSetInLocation], Kimw);
    }

    double feedback = 0.0;
    if (Rselect == 1) {
        feedback = state[peState];
    } else if (Rselect == -1) {
        feedback = state[valveState];
    } else if (Rselect == -2) {
        feedback = signals.mFuelRequest;
    }
    const double rawError =
        -signals.mSpeedDeviation + powerReference + state[resetState] - R * feedback;
    double eSlope = (db == 0.0) || (rawError > db) || (rawError < -db) ? 1.0 : 0.0;
    const double deadZoneValue = deadZone(rawError, db);
    if ((deadZoneValue <= minerr) || (deadZoneValue >= maxerr)) {
        eSlope = 0.0;
    }
    const double derivativeGain = Kdgov / Tdgov;
    const bool normalRequestActive = (signals.mNormalRequest < signals.mTemperatureRequest) &&
        (signals.mNormalRequest < signals.mAccelerationRequest) &&
        (signals.mNormalRequest > Pmin) && (signals.mNormalRequest < Pmax);
    const double implicitScale = ((Rselect == -2) && normalRequestActive) ?
        1.0 / (1.0 + (Kpgov + derivativeGain) * R * eSlope) :
        1.0;
    const double eIntegral =
        ((Rselect == -2) && normalRequestActive) ? -R * eSlope * implicitScale : 0.0;
    const double eDerivative = ((Rselect == -2) && normalRequestActive) ?
        R * eSlope * derivativeGain * implicitScale :
        0.0;
    eSlope *= implicitScale;
    const double errorOmegaDerivative = -eSlope;
    const double errorResetDerivative = eSlope;
    const double errorPowerDerivative = (Rselect == 1) ? -R * eSlope : 0.0;
    const double errorValveDerivative = (Rselect == -1) ? -R * eSlope : 0.0;
    matrixData.assign(differentialRow + integralState,
                      differentialRow + integralState,
                      Kigov * eIntegral - stateData.cj);
    matrixData.assign(differentialRow + integralState,
                      differentialRow + derivativeState,
                      Kigov * eDerivative);
    matrixData.assignCheckCol(differentialRow + integralState,
                              inputLocs[govOmegaInLocation],
                              Kigov * errorOmegaDerivative);
    matrixData.assign(differentialRow + integralState,
                      differentialRow + resetState,
                      Kigov * errorResetDerivative);
    matrixData.assign(differentialRow + integralState,
                      differentialRow + peState,
                      Kigov * errorPowerDerivative);
    matrixData.assign(differentialRow + integralState,
                      differentialRow + valveState,
                      Kigov * errorValveDerivative);
    matrixData.assign(differentialRow + derivativeState,
                      differentialRow + integralState,
                      eIntegral / Tdgov);
    matrixData.assign(differentialRow + derivativeState,
                      differentialRow + derivativeState,
                      (eDerivative - 1.0) / Tdgov - stateData.cj);
    matrixData.assignCheckCol(differentialRow + derivativeState,
                              inputLocs[govOmegaInLocation],
                              errorOmegaDerivative / Tdgov);
    matrixData.assign(differentialRow + derivativeState,
                      differentialRow + resetState,
                      errorResetDerivative / Tdgov);
    matrixData.assign(differentialRow + derivativeState,
                      differentialRow + peState,
                      errorPowerDerivative / Tdgov);
    matrixData.assign(differentialRow + derivativeState,
                      differentialRow + valveState,
                      errorValveDerivative / Tdgov);

    // Active-branch derivative of the low-value selector.
    double requestOmegaDerivative = (Kpgov + derivativeGain) * errorOmegaDerivative;
    double requestResetDerivative = (Kpgov + derivativeGain) * errorResetDerivative;
    double requestPowerDerivative = (Kpgov + derivativeGain) * errorPowerDerivative;
    double requestValveDerivative = (Kpgov + derivativeGain) * errorValveDerivative;
    double requestIntegralDerivative = 1.0 + (Kpgov + derivativeGain) * eIntegral;
    double requestFilterDerivative = -derivativeGain + (Kpgov + derivativeGain) * eDerivative;
    double requestTemperatureDerivative = 0.0;
    double requestLoadDerivative = 0.0;
    double requestAccelerationDerivative = 0.0;
    if ((signals.mTemperatureRequest <= signals.mNormalRequest) &&
        (signals.mTemperatureRequest <= signals.mAccelerationRequest)) {
        requestOmegaDerivative = requestResetDerivative = requestPowerDerivative = 0.0;
        requestValveDerivative = requestIntegralDerivative = requestFilterDerivative = 0.0;
        if (state[loadIntegralState] + Kpload * (Ldref / Kturb + Wfnl - state[temperatureState]) <
            1.0) {
            requestTemperatureDerivative = -Kpload;
            requestLoadDerivative = 1.0;
        }
    } else if (signals.mAccelerationRequest <= signals.mNormalRequest) {
        requestOmegaDerivative = -Ka * accelerationStep / TaAccel;
        requestResetDerivative = requestPowerDerivative = requestIntegralDerivative = 0.0;
        requestFilterDerivative = 0.0;
        requestValveDerivative = 1.0;
        requestAccelerationDerivative = Ka * accelerationStep / TaAccel;
    }
    const double minRequest = std::min(
        {signals.mNormalRequest, signals.mTemperatureRequest, signals.mAccelerationRequest});
    if ((minRequest <= Pmin) || (minRequest >= Pmax)) {
        requestOmegaDerivative = requestResetDerivative = requestPowerDerivative = 0.0;
        requestValveDerivative = requestIntegralDerivative = requestFilterDerivative = 0.0;
        requestTemperatureDerivative = requestLoadDerivative = requestAccelerationDerivative = 0.0;
    }
    const double unlimitedRate = (signals.mFuelRequest - signals.mValve) / Tact;
    const bool valveBlocked = (unlimitedRate <= Rclose) || (unlimitedRate >= Ropen) ||
        ((signals.mValve >= Pmax) && (unlimitedRate > 0.0)) ||
        ((signals.mValve <= Pmin) && (unlimitedRate < 0.0));
    if (valveBlocked) {
        matrixData.assign(differentialRow + valveState,
                          differentialRow + valveState,
                          -stateData.cj);
    } else {
        matrixData.assignCheckCol(differentialRow + valveState,
                                  inputLocs[govOmegaInLocation],
                                  requestOmegaDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + resetState,
                          requestResetDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + peState,
                          requestPowerDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + valveState,
                          (requestValveDerivative - 1.0) / Tact - stateData.cj);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + integralState,
                          requestIntegralDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + derivativeState,
                          requestFilterDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + temperatureState,
                          requestTemperatureDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + loadIntegralState,
                          requestLoadDerivative / Tact);
        matrixData.assign(differentialRow + valveState,
                          differentialRow + accelerationState,
                          requestAccelerationDerivative / Tact);
    }
    matrixData.assign(differentialRow + turbineState,
                      differentialRow + turbineState,
                      -1.0 / Tb - stateData.cj);
    matrixData.assign(differentialRow + turbineState,
                      differentialRow + valveState,
                      Kturb * fuelDvalve / Tb);
    matrixData.assignCheckCol(differentialRow + turbineState,
                              inputLocs[govOmegaInLocation],
                              Kturb * fuelDomega / Tb);
    const double speedFactor = (Dm < 0.0) ? std::pow(omega, Dm) : 1.0;
    const double temperatureValveDerivative = fuelDvalve * speedFactor;
    double temperatureOmegaDerivative = fuelDomega * speedFactor;
    if (Dm < 0.0) {
        temperatureOmegaDerivative += signals.mFuelFlow * Dm * std::pow(omega, Dm - 1.0);
    }
    matrixData.assign(differentialRow + temperatureLeadState,
                      differentialRow + temperatureLeadState,
                      -1.0 / Tsb - stateData.cj);
    matrixData.assign(differentialRow + temperatureLeadState,
                      differentialRow + valveState,
                      temperatureValveDerivative / Tsb);
    matrixData.assignCheckCol(differentialRow + temperatureLeadState,
                              inputLocs[govOmegaInLocation],
                              temperatureOmegaDerivative / Tsb);
    matrixData.assign(differentialRow + temperatureState,
                      differentialRow + temperatureState,
                      -1.0 / Tfload - stateData.cj);
    matrixData.assign(differentialRow + temperatureState,
                      differentialRow + temperatureLeadState,
                      (1.0 - Tsa / Tsb) / Tfload);
    matrixData.assign(differentialRow + temperatureState,
                      differentialRow + valveState,
                      (Tsa / Tsb) * temperatureValveDerivative / Tfload);
    matrixData.assignCheckCol(differentialRow + temperatureState,
                              inputLocs[govOmegaInLocation],
                              (Tsa / Tsb) * temperatureOmegaDerivative / Tfload);
    matrixData.assign(differentialRow + loadIntegralState,
                      differentialRow + loadIntegralState,
                      -stateData.cj);
    matrixData.assign(differentialRow + loadIntegralState,
                      differentialRow + temperatureState,
                      -Kiload);
    matrixData.assign(differentialRow + accelerationState,
                      differentialRow + accelerationState,
                      -1.0 / TaAccel - stateData.cj);
    matrixData.assignCheckCol(differentialRow + accelerationState,
                              inputLocs[govOmegaInLocation],
                              1.0 / TaAccel);
}

void GovernorGgov1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t ii = 0; ii < 10; ++ii) {
        m_state[ii + 1] += timeStep * m_dstate_dt[ii + 1];
    }
    m_state[valveState + 1] =
        std::clamp(m_state[valveState + 1], static_cast<double>(Pmin), static_cast<double>(Pmax));
    m_state[0] = evaluate(inputs, m_state.data() + 1).mMechanicalPower;
    prevTime = time;
}

void GovernorGgov1::set(std::string_view param, std::string_view value)
{
    Governor::set(param, value);
}

void GovernorGgov1::set(std::string_view param, double value, units::unit unitType)
{
    if (param == "rselect") {
        Rselect = static_cast<int>(value);
    } else if ((param == "fswitch") || (param == "flag")) {
        fuelFlag = static_cast<int>(value);
    } else if (param == "r") {
        R = value;
    } else if ((param == "tpelec") || (param == "t_pelec")) {
        Tpelec = value;
    } else if (param == "maxerr") {
        maxerr = value;
    } else if (param == "minerr") {
        minerr = value;
    } else if (param == "kpgov") {
        Kpgov = value;
    } else if (param == "kigov") {
        Kigov = value;
    } else if (param == "kdgov") {
        Kdgov = value;
    } else if (param == "tdgov") {
        Tdgov = value;
    } else if ((param == "vmax") || (param == "pmax")) {
        Pmax = value;
    } else if ((param == "vmin") || (param == "pmin")) {
        Pmin = value;
    } else if (param == "tact") {
        Tact = value;
    } else if (param == "kturb") {
        Kturb = value;
    } else if (param == "wfnl") {
        Wfnl = value;
    } else if (param == "tb") {
        Tb = value;
    } else if (param == "tc") {
        Tc = value;
    } else if (param == "teng") {
        Teng = value;
    } else if (param == "tfload") {
        Tfload = value;
    } else if (param == "kpload") {
        Kpload = value;
    } else if (param == "kiload") {
        Kiload = value;
    } else if (param == "ldref") {
        Ldref = value;
    } else if (param == "dm") {
        Dm = value;
    } else if (param == "ropen") {
        Ropen = value;
    } else if (param == "rclose") {
        Rclose = value;
    } else if (param == "kimw") {
        Kimw = value;
    } else if (param == "aset") {
        Aset = value;
    } else if (param == "ka") {
        Ka = value;
    } else if (param == "ta") {
        TaAccel = value;
    } else if (param == "trate") {
        Trate = value;
    } else if (param == "db") {
        db = value;
    } else if (param == "tsa") {
        Tsa = value;
    } else if (param == "tsb") {
        Tsb = value;
    } else if (param == "rup") {
        Rup = value;
    } else if (param == "rdown") {
        Rdown = value;
    } else {
        Governor::set(param, value, unitType);
    }
}

double GovernorGgov1::get(std::string_view param, units::unit unitType) const
{
    if (param == "rselect") {
        return Rselect;
    }
    if (param == "fswitch") {
        return fuelFlag;
    }
    if (param == "r") {
        return R;
    }
    if (param == "tpelec") {
        return Tpelec;
    }
    if (param == "maxerr") {
        return maxerr;
    }
    if (param == "minerr") {
        return minerr;
    }
    if (param == "kpgov") {
        return Kpgov;
    }
    if (param == "kigov") {
        return Kigov;
    }
    if (param == "kdgov") {
        return Kdgov;
    }
    if (param == "tdgov") {
        return Tdgov;
    }
    if (param == "vmax") {
        return Pmax;
    }
    if (param == "vmin") {
        return Pmin;
    }
    if (param == "tact") {
        return Tact;
    }
    if (param == "kturb") {
        return Kturb;
    }
    if (param == "wfnl") {
        return Wfnl;
    }
    if (param == "tb") {
        return Tb;
    }
    if (param == "tc") {
        return Tc;
    }
    if (param == "teng") {
        return Teng;
    }
    if (param == "tfload") {
        return Tfload;
    }
    if (param == "kpload") {
        return Kpload;
    }
    if (param == "kiload") {
        return Kiload;
    }
    if (param == "ldref") {
        return Ldref;
    }
    if (param == "dm") {
        return Dm;
    }
    if (param == "ropen") {
        return Ropen;
    }
    if (param == "rclose") {
        return Rclose;
    }
    if (param == "kimw") {
        return Kimw;
    }
    if (param == "aset") {
        return Aset;
    }
    if (param == "ka") {
        return Ka;
    }
    if (param == "ta") {
        return TaAccel;
    }
    if (param == "trate") {
        return Trate;
    }
    if (param == "db") {
        return db;
    }
    if (param == "tsa") {
        return Tsa;
    }
    if (param == "tsb") {
        return Tsb;
    }
    if (param == "rup") {
        return Rup;
    }
    if (param == "rdown") {
        return Rdown;
    }
    return Governor::get(param, unitType);
}

stringVec GovernorGgov1::localStateNames() const
{
    return {"pmech",
            "pelec_f",
            "mw_reset",
            "gov_int",
            "derr_f",
            "valve",
            "turbine",
            "temp_ll",
            "temp",
            "load_int",
            "accel_f"};
}

index_t GovernorGgov1::findIndex(std::string_view field, const SolverMode& sMode) const
{
    return ((field == "pm") || (field == "pmech")) ? offsets.getAlgOffset(sMode) : kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::governors
