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
    double speedDeviation;
    double limitedError;
    double normalRequest;
    double temperatureRequest;
    double accelerationRequest;
    double fuelRequest;
    double valve;
    double fuelFlow;
    double turbineInput;
    double turbineOutput;
    double temperatureInput;
    double temperatureLeadOutput;
    double mechanicalPower;
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
        // TODO: Support TENG with a reusable transport-delay/history component.
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
    const double pe = inputs[govElectricalPowerInLocation];
    const double fuel = power / Kturb + Wfnl;
    if ((fuel < Pmin - 1e-7) || (fuel > Pmax + 1e-7)) {
        throw InvalidParameterValue("GGOV1 initial valve position outside limits");
    }
    double* x = m_state.data() + 1;
    x[peState] = pe;
    x[resetState] = 0.0;
    x[integralState] = fuel;
    x[derivativeState] = 0.0;
    x[valveState] = fuel;
    x[turbineState] = power;
    x[temperatureLeadState] = fuel;
    x[temperatureState] = fuel;
    x[loadIntegralState] = fuel;
    x[accelerationState] = 0.0;
    const double feedback =
        (Rselect == 1) ? pe : (((Rselect == -1) || (Rselect == -2)) ? fuel : 0.0);
    powerReference = R * feedback;
    Pset = pe;
    m_state[0] = power;
    fieldSet[govpSetInLocation] = Pset;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

GovernorGgov1::Signals GovernorGgov1::evaluate(const IOdata& inputs, const double x[]) const
{
    Signals s{};
    const double omega = inputs[govOmegaInLocation];
    s.speedDeviation = omega - 1.0;
    const double temperatureError = Ldref / Kturb + Wfnl - x[temperatureState];
    s.temperatureRequest = std::min(1.0, x[loadIntegralState] + Kpload * temperatureError);
    const double acceleration = (s.speedDeviation - x[accelerationState]) / TaAccel;
    s.accelerationRequest = x[valveState] + Ka * accelerationStep * (Aset - acceleration);
    const auto evaluateNormalRequest = [this, &s, x](double feedback) {
        const double error =
            deadZone(-s.speedDeviation + powerReference + x[resetState] - R * feedback, db);
        const double limitedError =
            std::clamp(error, static_cast<double>(minerr), static_cast<double>(maxerr));
        const double derivativeOutput = Kdgov * (limitedError - x[derivativeState]) / Tdgov;
        return std::pair{limitedError, Kpgov * limitedError + x[integralState] + derivativeOutput};
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
            const double selected =
                std::clamp(std::min({normalRequest, s.temperatureRequest, s.accelerationRequest}),
                           static_cast<double>(Pmin),
                           static_cast<double>(Pmax));
            if (request < selected) {
                lower = request;
            } else {
                upper = request;
            }
        }
        s.fuelRequest = 0.5 * (lower + upper);
        const auto [limitedError, normalRequest] = evaluateNormalRequest(s.fuelRequest);
        s.limitedError = limitedError;
        s.normalRequest = normalRequest;
    } else {
        const double feedback =
            (Rselect == 1) ? x[peState] : ((Rselect == -1) ? x[valveState] : 0.0);
        const auto [limitedError, normalRequest] = evaluateNormalRequest(feedback);
        s.limitedError = limitedError;
        s.normalRequest = normalRequest;
        s.fuelRequest =
            std::clamp(std::min({s.normalRequest, s.temperatureRequest, s.accelerationRequest}),
                       static_cast<double>(Pmin),
                       static_cast<double>(Pmax));
    }
    s.valve = std::clamp(x[valveState], static_cast<double>(Pmin), static_cast<double>(Pmax));
    s.fuelFlow = (fuelFlag == 1) ? omega * s.valve : s.valve;
    s.turbineInput = Kturb * (s.fuelFlow - Wfnl);
    s.turbineOutput = x[turbineState] + (Tc / Tb) * (s.turbineInput - x[turbineState]);
    const double speedFactor = (Dm < 0.0) ? std::pow(omega, Dm) : 1.0;
    s.temperatureInput = s.fuelFlow * speedFactor;
    s.temperatureLeadOutput =
        x[temperatureLeadState] + (Tsa / Tsb) * (s.temperatureInput - x[temperatureLeadState]);
    s.mechanicalPower = (Dm >= 0.0) ? s.turbineOutput - Dm * s.speedDeviation : s.turbineOutput;
    return s;
}

void GovernorGgov1::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).mechanicalPower - loc.algStateLoc[0];
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
    const double* x = loc.diffStateLoc;
    double* dx = loc.destDiffLoc;
    const auto s = evaluate(inputs, x);
    dx[peState] = (inputs[govElectricalPowerInLocation] - x[peState]) / Tpelec;
    const double resetDrive = Kimw * (inputs[govpSetInLocation] - x[peState]);
    const double resetLimit = 1.1 * std::abs(R);
    dx[resetState] = (((x[resetState] >= resetLimit) && (resetDrive > 0.0)) ||
                      ((x[resetState] <= -resetLimit) && (resetDrive < 0.0))) ?
        0.0 :
        resetDrive;
    dx[integralState] = Kigov * s.limitedError;
    dx[derivativeState] = (s.limitedError - x[derivativeState]) / Tdgov;
    double rate = std::clamp((s.fuelRequest - s.valve) / Tact,
                             static_cast<double>(Rclose),
                             static_cast<double>(Ropen));
    if (((s.valve >= Pmax) && (rate > 0.0)) || ((s.valve <= Pmin) && (rate < 0.0))) {
        rate = 0.0;
    }
    dx[valveState] = rate;
    dx[turbineState] = (s.turbineInput - x[turbineState]) / Tb;
    dx[temperatureLeadState] = (s.temperatureInput - x[temperatureLeadState]) / Tsb;
    dx[temperatureState] = (s.temperatureLeadOutput - x[temperatureState]) / Tfload;
    dx[loadIntegralState] = Kiload * (Ldref / Kturb + Wfnl - x[temperatureState]);
    dx[accelerationState] = (s.speedDeviation - x[accelerationState]) / TaAccel;
}

void GovernorGgov1::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    auto loc = offsets.getLocations(stateData, update, sMode, this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).mechanicalPower;
    }
}

void GovernorGgov1::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto ra = loc.algOffset;
    const auto rd = loc.diffOffset;
    const double* x = loc.diffStateLoc;
    const auto s = evaluate(inputs, x);
    const double omega = inputs[govOmegaInLocation];
    const double fuelDomega = (fuelFlag == 1) ? s.valve : 0.0;
    const double fuelDvalve = (fuelFlag == 1) ? omega : 1.0;
    if (hasAlgebraic(sMode)) {
        md.assign(ra, ra, -1.0);
        md.assign(ra, rd + turbineState, 1.0 - Tc / Tb);
        md.assign(ra, rd + valveState, (Tc / Tb) * Kturb * fuelDvalve);
        md.assignCheckCol(ra,
                          inputLocs[govOmegaInLocation],
                          (Tc / Tb) * Kturb * fuelDomega - ((Dm >= 0.0) ? Dm : 0.0));
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    md.assign(rd + peState, rd + peState, -1.0 / Tpelec - stateData.cj);
    md.assignCheckCol(rd + peState, inputLocs[govElectricalPowerInLocation], 1.0 / Tpelec);
    md.assign(rd + resetState, rd + resetState, -stateData.cj);
    const double resetDrive = Kimw * (inputs[govpSetInLocation] - x[peState]);
    const double resetLimit = 1.1 * std::abs(R);
    const bool resetBlocked = ((x[resetState] >= resetLimit) && (resetDrive > 0.0)) ||
        ((x[resetState] <= -resetLimit) && (resetDrive < 0.0));
    if (!resetBlocked) {
        md.assign(rd + resetState, rd + peState, -Kimw);
        md.assignCheckCol(rd + resetState, inputLocs[govpSetInLocation], Kimw);
    }

    const double feedback = (Rselect == 1) ?
        x[peState] :
        ((Rselect == -1) ? x[valveState] : ((Rselect == -2) ? s.fuelRequest : 0.0));
    const double rawError = -s.speedDeviation + powerReference + x[resetState] - R * feedback;
    double eSlope = (db == 0.0) || (rawError > db) || (rawError < -db) ? 1.0 : 0.0;
    const double dz = deadZone(rawError, db);
    if ((dz <= minerr) || (dz >= maxerr)) {
        eSlope = 0.0;
    }
    const double kd = Kdgov / Tdgov;
    const bool normalRequestActive = (s.normalRequest < s.temperatureRequest) &&
        (s.normalRequest < s.accelerationRequest) && (s.normalRequest > Pmin) &&
        (s.normalRequest < Pmax);
    const double implicitScale =
        ((Rselect == -2) && normalRequestActive) ? 1.0 / (1.0 + (Kpgov + kd) * R * eSlope) : 1.0;
    const double eIntegral =
        ((Rselect == -2) && normalRequestActive) ? -R * eSlope * implicitScale : 0.0;
    const double eDerivative =
        ((Rselect == -2) && normalRequestActive) ? R * eSlope * kd * implicitScale : 0.0;
    eSlope *= implicitScale;
    const double ew = -eSlope;
    const double er = eSlope;
    const double ep = (Rselect == 1) ? -R * eSlope : 0.0;
    const double ev = (Rselect == -1) ? -R * eSlope : 0.0;
    md.assign(rd + integralState, rd + integralState, Kigov * eIntegral - stateData.cj);
    md.assign(rd + integralState, rd + derivativeState, Kigov * eDerivative);
    md.assignCheckCol(rd + integralState, inputLocs[govOmegaInLocation], Kigov * ew);
    md.assign(rd + integralState, rd + resetState, Kigov * er);
    md.assign(rd + integralState, rd + peState, Kigov * ep);
    md.assign(rd + integralState, rd + valveState, Kigov * ev);
    md.assign(rd + derivativeState, rd + integralState, eIntegral / Tdgov);
    md.assign(rd + derivativeState,
              rd + derivativeState,
              (eDerivative - 1.0) / Tdgov - stateData.cj);
    md.assignCheckCol(rd + derivativeState, inputLocs[govOmegaInLocation], ew / Tdgov);
    md.assign(rd + derivativeState, rd + resetState, er / Tdgov);
    md.assign(rd + derivativeState, rd + peState, ep / Tdgov);
    md.assign(rd + derivativeState, rd + valveState, ev / Tdgov);

    // Active-branch derivative of the low-value selector.
    double qw = (Kpgov + kd) * ew;
    double qr = (Kpgov + kd) * er;
    double qp = (Kpgov + kd) * ep;
    double qv = (Kpgov + kd) * ev;
    double qi = 1.0 + (Kpgov + kd) * eIntegral;
    double qd = -kd + (Kpgov + kd) * eDerivative;
    double qt = 0.0;
    double ql = 0.0;
    double qa = 0.0;
    if ((s.temperatureRequest <= s.normalRequest) &&
        (s.temperatureRequest <= s.accelerationRequest)) {
        qw = qr = qp = qv = qi = qd = 0.0;
        if (x[loadIntegralState] + Kpload * (Ldref / Kturb + Wfnl - x[temperatureState]) < 1.0) {
            qt = -Kpload;
            ql = 1.0;
        }
    } else if (s.accelerationRequest <= s.normalRequest) {
        qw = -Ka * accelerationStep / TaAccel;
        qr = qp = qi = qd = 0.0;
        qv = 1.0;
        qa = Ka * accelerationStep / TaAccel;
    }
    const double minRequest =
        std::min({s.normalRequest, s.temperatureRequest, s.accelerationRequest});
    if ((minRequest <= Pmin) || (minRequest >= Pmax)) {
        qw = qr = qp = qv = qi = qd = qt = ql = qa = 0.0;
    }
    const double unlimitedRate = (s.fuelRequest - s.valve) / Tact;
    const bool valveBlocked = (unlimitedRate <= Rclose) || (unlimitedRate >= Ropen) ||
        ((s.valve >= Pmax) && (unlimitedRate > 0.0)) ||
        ((s.valve <= Pmin) && (unlimitedRate < 0.0));
    if (valveBlocked) {
        md.assign(rd + valveState, rd + valveState, -stateData.cj);
    } else {
        md.assignCheckCol(rd + valveState, inputLocs[govOmegaInLocation], qw / Tact);
        md.assign(rd + valveState, rd + resetState, qr / Tact);
        md.assign(rd + valveState, rd + peState, qp / Tact);
        md.assign(rd + valveState, rd + valveState, (qv - 1.0) / Tact - stateData.cj);
        md.assign(rd + valveState, rd + integralState, qi / Tact);
        md.assign(rd + valveState, rd + derivativeState, qd / Tact);
        md.assign(rd + valveState, rd + temperatureState, qt / Tact);
        md.assign(rd + valveState, rd + loadIntegralState, ql / Tact);
        md.assign(rd + valveState, rd + accelerationState, qa / Tact);
    }
    md.assign(rd + turbineState, rd + turbineState, -1.0 / Tb - stateData.cj);
    md.assign(rd + turbineState, rd + valveState, Kturb * fuelDvalve / Tb);
    md.assignCheckCol(rd + turbineState, inputLocs[govOmegaInLocation], Kturb * fuelDomega / Tb);
    const double sf = (Dm < 0.0) ? std::pow(omega, Dm) : 1.0;
    const double tempDv = fuelDvalve * sf;
    double tempDw = fuelDomega * sf;
    if (Dm < 0.0) {
        tempDw += s.fuelFlow * Dm * std::pow(omega, Dm - 1.0);
    }
    md.assign(rd + temperatureLeadState, rd + temperatureLeadState, -1.0 / Tsb - stateData.cj);
    md.assign(rd + temperatureLeadState, rd + valveState, tempDv / Tsb);
    md.assignCheckCol(rd + temperatureLeadState, inputLocs[govOmegaInLocation], tempDw / Tsb);
    md.assign(rd + temperatureState, rd + temperatureState, -1.0 / Tfload - stateData.cj);
    md.assign(rd + temperatureState, rd + temperatureLeadState, (1.0 - Tsa / Tsb) / Tfload);
    md.assign(rd + temperatureState, rd + valveState, (Tsa / Tsb) * tempDv / Tfload);
    md.assignCheckCol(rd + temperatureState,
                      inputLocs[govOmegaInLocation],
                      (Tsa / Tsb) * tempDw / Tfload);
    md.assign(rd + loadIntegralState, rd + loadIntegralState, -stateData.cj);
    md.assign(rd + loadIntegralState, rd + temperatureState, -Kiload);
    md.assign(rd + accelerationState, rd + accelerationState, -1.0 / TaAccel - stateData.cj);
    md.assignCheckCol(rd + accelerationState, inputLocs[govOmegaInLocation], 1.0 / TaAccel);
}

void GovernorGgov1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double dt = time - prevTime;
    for (index_t ii = 0; ii < 10; ++ii) {
        m_state[ii + 1] += dt * m_dstate_dt[ii + 1];
    }
    m_state[valveState + 1] =
        std::clamp(m_state[valveState + 1], static_cast<double>(Pmin), static_cast<double>(Pmax));
    m_state[0] = evaluate(inputs, m_state.data() + 1).mechanicalPower;
    prevTime = time;
}

void GovernorGgov1::set(std::string_view p, std::string_view v)
{
    Governor::set(p, v);
}

void GovernorGgov1::set(std::string_view p, double v, units::unit unitType)
{
    if (p == "rselect") {
        Rselect = static_cast<int>(v);
    } else if ((p == "fswitch") || (p == "flag")) {
        fuelFlag = static_cast<int>(v);
    } else if (p == "r") {
        R = v;
    } else if ((p == "tpelec") || (p == "t_pelec")) {
        Tpelec = v;
    } else if (p == "maxerr") {
        maxerr = v;
    } else if (p == "minerr") {
        minerr = v;
    } else if (p == "kpgov") {
        Kpgov = v;
    } else if (p == "kigov") {
        Kigov = v;
    } else if (p == "kdgov") {
        Kdgov = v;
    } else if (p == "tdgov") {
        Tdgov = v;
    } else if ((p == "vmax") || (p == "pmax")) {
        Pmax = v;
    } else if ((p == "vmin") || (p == "pmin")) {
        Pmin = v;
    } else if (p == "tact") {
        Tact = v;
    } else if (p == "kturb") {
        Kturb = v;
    } else if (p == "wfnl") {
        Wfnl = v;
    } else if (p == "tb") {
        Tb = v;
    } else if (p == "tc") {
        Tc = v;
    } else if (p == "teng") {
        Teng = v;
    } else if (p == "tfload") {
        Tfload = v;
    } else if (p == "kpload") {
        Kpload = v;
    } else if (p == "kiload") {
        Kiload = v;
    } else if (p == "ldref") {
        Ldref = v;
    } else if (p == "dm") {
        Dm = v;
    } else if (p == "ropen") {
        Ropen = v;
    } else if (p == "rclose") {
        Rclose = v;
    } else if (p == "kimw") {
        Kimw = v;
    } else if (p == "aset") {
        Aset = v;
    } else if (p == "ka") {
        Ka = v;
    } else if (p == "ta") {
        TaAccel = v;
    } else if (p == "trate") {
        Trate = v;
    } else if (p == "db") {
        db = v;
    } else if (p == "tsa") {
        Tsa = v;
    } else if (p == "tsb") {
        Tsb = v;
    } else if (p == "rup") {
        Rup = v;
    } else if (p == "rdown") {
        Rdown = v;
    } else {
        Governor::set(p, v, unitType);
    }
}

double GovernorGgov1::get(std::string_view p, units::unit unitType) const
{
#define GET_VALUE(name, member)                                                                    \
    if (p == name) {                                                                               \
        return member;                                                                             \
    }
    GET_VALUE("rselect", Rselect)
    GET_VALUE("fswitch", fuelFlag)
    GET_VALUE("r", R)
    GET_VALUE("tpelec", Tpelec) GET_VALUE("maxerr", maxerr) GET_VALUE("minerr", minerr)
        GET_VALUE("kpgov", Kpgov) GET_VALUE("kigov", Kigov) GET_VALUE("kdgov", Kdgov)
            GET_VALUE("tdgov", Tdgov) GET_VALUE("vmax", Pmax) GET_VALUE("vmin", Pmin)
                GET_VALUE("tact", Tact) GET_VALUE("kturb", Kturb) GET_VALUE("wfnl", Wfnl)
                    GET_VALUE("tb", Tb) GET_VALUE("tc", Tc) GET_VALUE("teng", Teng)
                        GET_VALUE("tfload", Tfload) GET_VALUE("kpload", Kpload)
                            GET_VALUE("kiload", Kiload) GET_VALUE("ldref", Ldref)
                                GET_VALUE("dm", Dm) GET_VALUE("ropen", Ropen)
                                    GET_VALUE("rclose", Rclose) GET_VALUE("kimw", Kimw)
                                        GET_VALUE("aset", Aset) GET_VALUE("ka", Ka)
                                            GET_VALUE("ta", TaAccel) GET_VALUE("trate", Trate)
                                                GET_VALUE("db", db) GET_VALUE("tsa", Tsa)
                                                    GET_VALUE("tsb", Tsb) GET_VALUE("rup", Rup)
                                                        GET_VALUE("rdown", Rdown)
#undef GET_VALUE
                                                            return Governor::get(p, unitType);
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
