/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorIeeeG3.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::governors {

GovernorIeeeG3::GovernorIeeeG3(const std::string& name): Governor(name)
{
    Pmax = 1.0;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}
const ModelDescription& GovernorIeeeG3::getModelDescription() const
{
    static const auto description = ModelDescription::define<GovernorIeeeG3>(
        "GovernorIeeeG3",
        &Governor::getModelDescription(),
        {parameter({"tg"}, &GovernorIeeeG3::mTg, units::second, "tg"),
         parameter({"tp"}, &GovernorIeeeG3::mTp, units::second, "tp"),
         parameter({"uo"}, &GovernorIeeeG3::mUo, units::one, "uo"),
         parameter({"uc"}, &GovernorIeeeG3::mUc, units::one, "uc"),
         parameter({"rperm"}, &GovernorIeeeG3::mRperm, units::one, "rperm"),
         parameter({"rtemp"}, &GovernorIeeeG3::mRtemp, units::one, "rtemp"),
         parameter({"tr"}, &GovernorIeeeG3::mTr, units::second, "tr"),
         parameter({"tw"}, &GovernorIeeeG3::mTw, units::second, "tw"),
         parameter({"a11"}, &GovernorIeeeG3::mA11, units::one, "a11"),
         parameter({"a13"}, &GovernorIeeeG3::mA13, units::one, "a13"),
         parameter({"a21"}, &GovernorIeeeG3::mA21, units::one, "a21"),
         parameter({"a23"}, &GovernorIeeeG3::mA23, units::one, "a23"),
         state({"pmech", "pm", "output"}, units::puMW, 0, "Mechanical power"),
         state({"gate"}, units::one, 1, "Unbounded gate integrator"),
         state({"washout"}, units::one, &GovernorIeeeG3::mWashState, 1, "Droop lag"),
         state({"pilot"}, units::one, &GovernorIeeeG3::mPilotState, 1, "Pilot servo"),
         state({"water"}, units::one, &GovernorIeeeG3::mWaterState, 1, "Turbine lag")});
    return description;
}
CoreObject* GovernorIeeeG3::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorIeeeG3, Governor>(this, obj);
    if (out != nullptr) {
        out->mTg = mTg;
        out->mTp = mTp;
        out->mUo = mUo;
        out->mUc = mUc;
        out->mRperm = mRperm;
        out->mRtemp = mRtemp;
        out->mTr = mTr;
        out->mTw = mTw;
        out->mA11 = mA11;
        out->mA13 = mA13;
        out->mA21 = mA21;
        out->mA23 = mA23;
        out->mWashState = mWashState;
        out->mPilotState = mPilotState;
        out->mWaterState = mWaterState;
    }
    return out;
}
void GovernorIeeeG3::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    const std::array parameters{mTg,
                                mTp,
                                mUo,
                                mUc,
                                mRperm,
                                mRtemp,
                                mTr,
                                mTw,
                                mA11,
                                mA13,
                                mA21,
                                mA23,
                                static_cast<double>(Pmax),
                                static_cast<double>(Pmin)};
    if (std::ranges::any_of(parameters, [](double value) { return !std::isfinite(value); }) ||
        mTg <= 0 || mTp < 0 || mTr < 0 || mTw < 0 || mRperm <= 0 || mRtemp < 0 || mUo <= 0 ||
        mUc >= 0 || mA11 <= 0 || mA23 <= 0 || Pmin > Pmax) {
        throw InvalidParameterValue("IEEEG3 time constants, turbine coefficients, or limits");
    }
    index_t next = 1;
    mWashState = (mTr > 0) ? next++ : kInvalidLocation;
    mPilotState = (mTp > 0) ? next++ : kInvalidLocation;
    mWaterState = (mTw > 0) ? next++ : kInvalidLocation;
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = next;
    local.jacSize = 28;
    local.algRoots = 0;
    local.diffRoots = 0;
    prevTime = time0;
}
void GovernorIeeeG3::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desired,
                                          IOdata& fieldSet)
{
    if (inputs.size() < 2 || desired.empty() || !std::isfinite(desired[0]) ||
        !std::isfinite(inputs[0]) || std::abs(inputs[0] - 1.0) > 1e-8) {
        throw InvalidParameterValue(
            "IEEEG3 initialization requires nominal speed and finite power");
    }
    const double gate = desired[0] / mA23;
    if (gate < Pmin || !adjustInitialUpperLimit(gate, "IEEEG3 initial gate")) {
        throw InvalidParameterValue("IEEEG3 initial gate outside limits");
    }
    Pset = desired[0];
    auto* states = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    states[0] = gate;
    if (mWashState != kInvalidLocation) {
        states[mWashState] = gate;
    }
    if (mPilotState != kInvalidLocation) {
        states[mPilotState] = 0.0;
    }
    if (mWaterState != kInvalidLocation) {
        states[mWaterState] = gate;
    }
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = desired[0];
    fieldSet = {Pset, Pset};
}
detail::GovernorEvaluation<4> GovernorIeeeG3::evaluate(const IOdata& inputs,
                                                       const double* states) const
{
    using detail::clamp;
    using detail::constant;
    using detail::GovernorEvaluation;
    using detail::input;
    using detail::state;
    if (inputs.size() < 2) {
        throw InvalidParameterValue("IEEEG3 input vector");
    }
    GovernorEvaluation<4> result;
    const auto gate = clamp(state<4>(states[0], 0), Pmin, Pmax);
    const auto wash =
        (mWashState == kInvalidLocation) ? gate : state<4>(states[mWashState], mWashState);
    const auto error = constant<4>(1.0) - input<4>(inputs, govOmegaInLocation) +
        (mRperm / mA23) * input<4>(inputs, govpSetInLocation) - mRperm * gate -
        mRtemp * (gate - wash);
    const auto pilot =
        (mPilotState == kInvalidLocation) ? error : state<4>(states[mPilotState], mPilotState);
    // TGTypeIII limits the output of an unbounded gate integrator. Feedback
    // uses the limited gate, so accumulated overtravel delays release.
    result.mRate[0] = clamp((1.0 / mTg) * pilot, mUc, mUo);
    if (mWashState != kInvalidLocation) {
        result.mRate[mWashState] = (1.0 / mTr) * (gate - wash);
    }
    if (mPilotState != kInvalidLocation) {
        result.mRate[mPilotState] = (1.0 / mTp) * (error - pilot);
    }
    if (mWaterState != kInvalidLocation) {
        const auto water = state<4>(states[mWaterState], mWaterState);
        result.mRate[mWaterState] = (1.0 / (mA11 * mTw)) * (gate - water);
        const double hydraulicGain = mA13 * mA21 / mA11;
        // Equivalent to TGTypeIII's turbine integrator, scaled to a gate lag:
        // Pm = (a23 - a13*a21/a11)*gate + (a13*a21/a11)*water.
        result.mOutput = (mA23 - hydraulicGain) * gate + hydraulicGain * water;
    } else {
        result.mOutput = mA23 * gate;
    }
    return result;
}

void GovernorIeeeG3::set(std::string_view param, double value, units::unit unitType)
{
    if (!isSetHandled(getModelDescription().trySet(*this, param, value, unitType))) {
        Governor::set(param, value, unitType);
    }
}
void GovernorIeeeG3::residual(const IOdata& inputs,
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
void GovernorIeeeG3::derivative(const IOdata& inputs,
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
void GovernorIeeeG3::algebraicUpdate(const IOdata& inputs,
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
void GovernorIeeeG3::jacobianElements(const IOdata& inputs,
                                      const StateData& data,
                                      MatrixData<double>& matrix,
                                      const IOlocs& inputLocs,
                                      const SolverMode& mode)
{
    const auto loc = offsets.getLocations(data, mode, this);
    detail::writeJacobian(evaluate(inputs, loc.diffStateLoc), loc, data, matrix, inputLocs, mode);
}
void GovernorIeeeG3::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*mode*/)
{
    const double timeStep = time - prevTime;
    if (!std::isfinite(timeStep) || timeStep < 0.0) {
        throw InvalidParameterValue("GovernorIeeeG3 invalid timestep");
    }
    double* states = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    double maxStep = std::min(0.01, mTg / 4.0);
    for (const double timeConstant : {mTp, mTr, mA11 * mTw}) {
        if (timeConstant > 0.0) {
            maxStep = std::min(maxStep, timeConstant / 4.0);
        }
    }
    detail::advance<4>(states,
                       offsets.local().local.diffSize,
                       timeStep,
                       maxStep,
                       [&](const double* trial) { return evaluate(inputs, trial); });
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = evaluate(inputs, states).mOutput.mValue;
    prevTime = time;
}

}  // namespace griddyn::governors
