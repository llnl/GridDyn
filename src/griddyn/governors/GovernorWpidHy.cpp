/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorWpidHy.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace griddyn::governors {

GovernorWpidHy::GovernorWpidHy(const std::string& name): Governor(name)
{
    Pmax = 1.0;
    Pmin = 0.0;
    m_inputSize = 3;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}
const ModelDescription& GovernorWpidHy::getModelDescription() const
{
    static const auto description = ModelDescription::define<GovernorWpidHy>(
        "GovernorWpidHy",
        &Governor::getModelDescription(),
        {parameter({"treg"}, &GovernorWpidHy::mTreg, units::second, "treg"),
         parameter({"reg"}, &GovernorWpidHy::mReg, units::one, "reg"),
         parameter({"kp"}, &GovernorWpidHy::mKp, units::one, "kp"),
         parameter({"ki"}, &GovernorWpidHy::mKi, units::one, "ki"),
         parameter({"kd"}, &GovernorWpidHy::mKd, units::second, "PID derivative gain"),
         parameter({"ta"}, &GovernorWpidHy::mTa, units::second, "ta"),
         parameter({"tb"}, &GovernorWpidHy::mTb, units::second, "tb"),
         parameter({"tw"}, &GovernorWpidHy::mTw, units::second, "tw"),
         parameter({"velmax"}, &GovernorWpidHy::mVelmax, units::one, "velmax"),
         parameter({"velmin"}, &GovernorWpidHy::mVelmin, units::one, "velmin"),
         parameter({"gatmax"}, &GovernorWpidHy::mGatmax, units::one, "gatmax"),
         parameter({"gatmin"}, &GovernorWpidHy::mGatmin, units::one, "gatmin"),
         parameter({"d"}, &GovernorWpidHy::mD, units::one, "d"),
         parameter({"g0"}, &GovernorWpidHy::mG0, units::one, "g0"),
         parameter({"g1"}, &GovernorWpidHy::mG1, units::one, "g1"),
         parameter({"g2"}, &GovernorWpidHy::mG2, units::one, "g2"),
         parameter({"p1"}, &GovernorWpidHy::mP1, units::one, "p1"),
         parameter({"p2"}, &GovernorWpidHy::mP2, units::one, "p2"),
         parameter({"p3"}, &GovernorWpidHy::mP3, units::one, "p3"),
         localState("power_reference_offset",
                    &GovernorWpidHy::mPowerReferenceOffset,
                    "Initial mechanical command minus terminal electrical power"),
         state({"pmech", "pm", "output"}, units::puMW, 0, "Mechanical power"),
         state({"regulator"}, units::one, 1, "Electrical power filter"),
         state({"integral"}, units::one, 2, "PID integral"),
         state({"derivative"}, units::one, 3, "PID derivative lag"),
         state({"lag1"}, units::one, 4, "First cutoff lag"),
         state({"lag2"}, units::one, 5, "Second cutoff lag"),
         state({"servo"}, units::one, 6, "Servo derivative lag"),
         state({"gate"}, units::one, 7, "Unbounded gate integrator"),
         state({"water"}, units::one, 8, "Turbine lag")});
    return description;
}
CoreObject* GovernorWpidHy::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorWpidHy, Governor>(this, obj);
    if (out != nullptr) {
        out->mTreg = mTreg;
        out->mReg = mReg;
        out->mKp = mKp;
        out->mKi = mKi;
        out->mKd = mKd;
        out->mTa = mTa;
        out->mTb = mTb;
        out->mTw = mTw;
        out->mVelmax = mVelmax;
        out->mVelmin = mVelmin;
        out->mGatmax = mGatmax;
        out->mGatmin = mGatmin;
        out->mD = mD;
        out->mG0 = mG0;
        out->mG1 = mG1;
        out->mG2 = mG2;
        out->mP1 = mP1;
        out->mP2 = mP2;
        out->mP3 = mP3;
        out->mDerivativeTime = mDerivativeTime;
        out->mGateCurve = mGateCurve;
        out->mPowerReferenceOffset = mPowerReferenceOffset;
    }
    return out;
}
void GovernorWpidHy::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    const std::array parameters{mTreg,
                                mReg,
                                mKp,
                                mKi,
                                mKd,
                                mTa,
                                mTb,
                                mTw,
                                mVelmax,
                                mVelmin,
                                mGatmax,
                                mGatmin,
                                mD,
                                mG0,
                                mG1,
                                mG2,
                                mP1,
                                mP2,
                                mP3,
                                static_cast<double>(Pmax),
                                static_cast<double>(Pmin)};
    if (std::ranges::any_of(parameters, [](double value) { return !std::isfinite(value); }) ||
        mTreg <= 0 || mTa <= 0 || mTb <= 0 || mTw <= 0 || mKp < 0 || mKi < 0 || mKd < 0 ||
        mVelmax < 0 || mVelmin > 0 || mVelmin >= mVelmax || mGatmin >= mGatmax || Pmin > Pmax) {
        throw InvalidParameterValue("WPIDHY time constants, gains, or limits");
    }
    mGateCurve = {{mG0, 0.0}, {mG1, mP1}, {mG2, mP2}, {1.0, mP3}};
    for (std::size_t index = 1; index < mGateCurve.size(); ++index) {
        if (mGateCurve[index].first <= mGateCurve[index - 1].first ||
            mGateCurve[index].second <= mGateCurve[index - 1].second) {
            throw InvalidParameterValue(
                "WPIDHY requires strictly increasing G0/G1/G2/1 and 0/P1/P2/P3");
        }
    }
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 8;
    local.jacSize = 56;
    local.algRoots = 0;
    local.diffRoots = 0;
    prevTime = time0;
}
std::pair<double, double> GovernorWpidHy::curve(double gate) const
{
    auto upper =
        std::upper_bound(mGateCurve.begin(),
                         mGateCurve.end(),
                         gate,
                         [](double value, const auto& point) { return value < point.first; });
    std::size_t index = 0;
    if (upper == mGateCurve.end()) {
        index = mGateCurve.size() - 2;
    } else if (upper != mGateCurve.begin()) {
        index = static_cast<std::size_t>(upper - mGateCurve.begin() - 1);
    }
    const auto [lowerGate, lowerPower] = mGateCurve[index];
    const auto [upperGate, upperPower] = mGateCurve[index + 1];
    const double slope = (upperPower - lowerPower) / (upperGate - lowerGate);
    return {lowerPower + (slope * (gate - lowerGate)), slope};
}
void GovernorWpidHy::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desired,
                                          IOdata& fieldSet)
{
    if (inputs.size() < 3 || desired.empty() || !std::isfinite(desired[0]) ||
        !std::isfinite(inputs[govElectricalPowerInLocation]) || !std::isfinite(inputs[0]) ||
        !std::isfinite(inputs[govpSetInLocation]) || std::abs(inputs[0] - 1.0) > 1e-8) {
        throw InvalidParameterValue(
            "WPIDHY initialization requires nominal speed and machine electrical power");
    }
    if (desired[0] < Pmin || !adjustInitialUpperLimit(desired[0], "WPIDHY initial power")) {
        throw InvalidParameterValue("WPIDHY initial power outside limits");
    }
    const auto upper =
        std::upper_bound(mGateCurve.begin(),
                         mGateCurve.end(),
                         desired[0],
                         [](double value, const auto& point) { return value < point.second; });
    std::size_t index = 0;
    if (upper == mGateCurve.end()) {
        index = mGateCurve.size() - 2;
    } else if (upper != mGateCurve.begin()) {
        index = static_cast<std::size_t>(upper - mGateCurve.begin() - 1);
    }
    const auto [lowerGate, lowerPower] = mGateCurve[index];
    const auto [upperGate, upperPower] = mGateCurve[index + 1];
    const double gate = lowerGate +
        ((desired[0] - lowerPower) * (upperGate - lowerGate) / (upperPower - lowerPower));
    if (gate < mGatmin || gate > mGatmax) {
        throw InvalidParameterValue("WPIDHY initial gate outside GAT limits");
    }
    // OpenIPSL initializes PREF from PELEC. GridDyn supplies a mechanical
    // power command, so retain its initial offset while accepting later steps.
    // The generator supplies kNullVal when asking its governor to infer
    // the initial mechanical command from desired power.
    Pset = (inputs[govpSetInLocation] == kNullVal) ? desired[0] : inputs[govpSetInLocation];
    mPowerReferenceOffset = Pset - inputs[govElectricalPowerInLocation];
    auto* states = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    std::fill_n(states, 8, 0.0);
    states[6] = gate;
    states[7] = desired[0];
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = desired[0];
    fieldSet = {Pset, Pset};
}
detail::GovernorEvaluation<8> GovernorWpidHy::evaluate(const IOdata& inputs,
                                                       const double* states) const
{
    using detail::clamp;
    using detail::constant;
    using detail::GovernorEvaluation;
    using detail::GovernorSignal;
    using detail::input;
    using detail::state;
    if (inputs.size() < 3) {
        throw InvalidParameterValue("WPIDHY requires machine electrical power input");
    }
    GovernorEvaluation<8> result;
    std::array<GovernorSignal<8>, 8> stateSignals;
    for (index_t index = 0; index < 8; ++index) {
        stateSignals[index] = state<8>(states[index], index);
    }
    const auto speed = input<8>(inputs, govOmegaInLocation) - constant<8>(1.0);
    const auto error = stateSignals[0] - speed;
    result.mRate[0] = (1.0 / mTreg) *
        (mReg *
             (input<8>(inputs, govElectricalPowerInLocation) - input<8>(inputs, govpSetInLocation) +
              constant<8>(mPowerReferenceOffset)) -
         stateSignals[0]);
    result.mRate[1] = mKi * error;
    result.mRate[2] = (1.0 / mDerivativeTime) * (error - stateSignals[2]);
    const auto control =
        mKp * error + stateSignals[1] + (mKd / mDerivativeTime) * (error - stateSignals[2]);
    result.mRate[3] = (1.0 / mTa) * (control - stateSignals[3]);
    result.mRate[4] = (1.0 / mTa) * (stateSignals[3] - stateSignals[4]);
    result.mRate[5] = (1.0 / mTb) * (stateSignals[4] - stateSignals[5]);
    // OpenIPSL uses an unbounded gate integrator followed by a hard output limit.
    result.mRate[6] = clamp((1.0 / mTb) * (stateSignals[4] - stateSignals[5]), mVelmin, mVelmax);
    const auto gate = clamp(stateSignals[6], mGatmin, mGatmax);
    const auto [power, slope] = curve(gate.mValue);
    const auto turbineInput = constant<8>(power - (slope * gate.mValue)) + slope * gate;
    result.mRate[7] = (2.0 / mTw) * (turbineInput - stateSignals[7]);
    result.mOutput = clamp(3.0 * stateSignals[7] - 2.0 * turbineInput, Pmin, Pmax) - mD * speed;
    return result;
}

void GovernorWpidHy::set(std::string_view param, double value, units::unit unitType)
{
    if (!isSetHandled(getModelDescription().trySet(*this, param, value, unitType))) {
        Governor::set(param, value, unitType);
    }
}
void GovernorWpidHy::residual(const IOdata& inputs,
                              const StateData& data,
                              double resid[],
                              const SolverMode& mode)
{
    const auto loc = offsets.getLocations(data, resid, mode, this);
    const auto evaluation = evaluate(inputs, loc.diffStateLoc);
    if (hasAlgebraic(mode)) {
        loc.destLoc[0] = evaluation.mOutput.mValue - loc.algStateLoc[0];
    }
    if (hasDifferential(mode)) {
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] = evaluation.mRate[index].mValue - loc.dstateLoc[index];
        }
    }
}
void GovernorWpidHy::derivative(const IOdata& inputs,
                                const StateData& data,
                                double deriv[],
                                const SolverMode& mode)
{
    if (!hasDifferential(mode)) {
        return;
    }
    const auto loc = offsets.getLocations(data, deriv, mode, this);
    const auto evaluation = evaluate(inputs, loc.diffStateLoc);
    for (index_t index = 0; index < loc.diffSize; ++index) {
        loc.destDiffLoc[index] = evaluation.mRate[index].mValue;
    }
}
void GovernorWpidHy::algebraicUpdate(const IOdata& inputs,
                                     const StateData& data,
                                     double update[],
                                     const SolverMode& mode,
                                     double /*alpha*/)
{
    if (!hasAlgebraic(mode)) {
        return;
    }
    const auto loc = offsets.getLocations(data, update, mode, this);
    loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).mOutput.mValue;
}
void GovernorWpidHy::jacobianElements(const IOdata& inputs,
                                      const StateData& data,
                                      MatrixData<double>& matrix,
                                      const IOlocs& inputLocs,
                                      const SolverMode& mode)
{
    const auto loc = offsets.getLocations(data, mode, this);
    detail::writeJacobian(evaluate(inputs, loc.diffStateLoc), loc, data, matrix, inputLocs, mode);
}
void GovernorWpidHy::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*mode*/)
{
    const double timeStep = time - prevTime;
    if (!std::isfinite(timeStep) || timeStep < 0.0) {
        throw InvalidParameterValue("GovernorWpidHy invalid timestep");
    }
    double* states = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    const double maxStep = std::min({mTreg, mTa, mTb, mTw / 2.0, mDerivativeTime}) / 4.0;
    detail::advance<8>(states, 8, timeStep, maxStep, [&](const double* trial) {
        return evaluate(inputs, trial);
    });
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = evaluate(inputs, states).mOutput.mValue;
    prevTime = time;
}

}  // namespace griddyn::governors
