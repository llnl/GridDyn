/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterESST2A.h"

#include "ExciterSignalHelper.h"
#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t regulatorState = 1;
    constexpr index_t feedbackState = 2;
    constexpr index_t fieldState = 3;
    constexpr double initializationTolerance = 1e-7;

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

const ModelDescription& ExciterESST2A::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterESST2A>(
        "ExciterESST2A",
        &Exciter::getModelDescription(),
        {parameter({"tr"},
                   &ExciterESST2A::Tr,
                   units::second,
                   "Voltage measurement time constant",
                   parameterValidators::nonnegative),
         parameter({"kp"}, &ExciterESST2A::Kp, units::one, "Kp gain or coefficient"),
         parameter({"ki"}, &ExciterESST2A::Ki, units::one, "Ki gain or coefficient"),
         parameter({"kc"},
                   &ExciterESST2A::Kc,
                   units::one,
                   "Kc gain or coefficient",
                   parameterValidators::nonnegative),
         parameter({"kf"}, &ExciterESST2A::Kf, units::one, "Kf gain or coefficient"),
         parameter({"tf"},
                   &ExciterESST2A::Tf,
                   units::second,
                   "Feedback time constant",
                   parameterValidators::positive),
         parameter({"ke"}, &ExciterESST2A::Ke, units::one, "Ke gain or coefficient"),
         parameter({"te"},
                   &ExciterESST2A::Te,
                   units::second,
                   "Exciter time constant",
                   parameterValidators::positive),
         parameter({"efdmax", "efmax"},
                   &ExciterESST2A::Efdmax,
                   units::one,
                   "Efdmax upper limit",
                   parameterValidators::nonnegative),
         flag({"uel_enabled"}, &ExciterESST2A::underSelectorEnabled, "underSelectorEnabled option"),
         localState("regulator_limited",
                    &ExciterESST2A::opFlags,
                    ExciterESST2A::REGULATOR_LIMITED,
                    "Retained regulator_limited mode"),
         localState("regulator_limit_high",
                    &ExciterESST2A::opFlags,
                    ExciterESST2A::REGULATOR_LIMIT_HIGH,
                    "Retained regulator_limit_high mode"),
         localState("field_limited",
                    &ExciterESST2A::opFlags,
                    ExciterESST2A::FIELD_LIMITED,
                    "Retained field_limited mode"),
         localState("field_limit_high",
                    &ExciterESST2A::opFlags,
                    ExciterESST2A::FIELD_LIMIT_HIGH,
                    "Retained field_limit_high mode"),
         state("efd", units::one, "Field voltage"),
         state("vmeas", units::one, "Measured terminal voltage"),
         state("va", units::one, "Amplifier state"),
         state("wf", units::one, "Washout state"),
         state("efstate", units::one, "efstate solver state")});
    return description;
}

void ExciterESST2A::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ExciterESST2A::getModelDescription();
    getParamString<ExciterESST2A, Exciter>(this,
                                           pstr,
                                           description.numericNames(),
                                           description.stringNames(),
                                           description.flagNames(),
                                           pstype);
}

ExciterESST2A::ExciterESST2A(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 240.0;
    Ta = 0.01;
    Vrmax = 4.5;
    Vrmin = -4.5;
}

CoreObject* ExciterESST2A::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterESST2A, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Kp = Kp;
    result->Ki = Ki;
    result->Kc = Kc;
    result->Kf = Kf;
    result->Tf = Tf;
    result->Ke = Ke;
    result->Te = Te;
    result->Efdmax = Efdmax;
    result->underSelectorEnabled = underSelectorEnabled;
    return result;
}

void ExciterESST2A::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    opFlags.reset(REGULATOR_LIMITED);
    opFlags.reset(REGULATOR_LIMIT_HIGH);
    opFlags.reset(FIELD_LIMITED);
    opFlags.reset(FIELD_LIMIT_HIGH);
    const std::array<double, 14> parameters{
        Tr, Vrmax, Vrmin, Ka, Ta, Kp, Ki, Kc, Kf, Tf, Ke, Te, Efdmax, Vref};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Tr < 0.0) || (Ka <= 0.0) || (Ta < 0.0) || (Vrmax < Vrmin) || (Tf <= 0.0) || (Te <= 0.0) ||
        (Kc < 0.0) || (Efdmax < 0.0)) {
        throw InvalidParameterValue("ESST2A gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    // Dense upper bound for differential state/input derivatives plus the
    // algebraic output row. There is no EFD algebraic input to the dynamics.
    offsets.local().local.jacSize = stateCount * (stateCount + exciterInputCount) + stateCount + 1;
    offsets.local().local.algRoots = 2;
}

double ExciterESST2A::rectifierVoltage(const IOdata& inputs) const
{
    if ((Kp <= 0.0) && (Ki <= 0.0)) {
        return 1.0;
    }
    return detail::computeRectifierData(
               inputs, Kp, Ki, Kc, 0.0, 0.0, std::numeric_limits<double>::infinity())
        .voltage;
}

ExciterESST2A::Evaluation ExciterESST2A::evaluate(const IOdata& inputs, const double state[]) const
{
    using detail::addSignals;
    using detail::clampSignal;
    using detail::constantSignal;
    using detail::inputSignal;
    using detail::scaleSignal;
    using detail::stateSignal;
    using detail::subtractSignals;
    using Signal = detail::ExciterSignal<stateCount>;

    Evaluation evaluation;
    std::array<Signal, stateCount> rates{};
    const Signal measuredVoltage = (Tr > 0.0) ?
        stateSignal<stateCount>(state[voltageMeasurementState], voltageMeasurementState) :
        inputSignal<stateCount>(inputs, exciterVoltageInLocation);
    if (Tr > 0.0) {
        rates[voltageMeasurementState] =
            scaleSignal(subtractSignals(inputSignal<stateCount>(inputs, exciterVoltageInLocation),
                                        measuredVoltage),
                        1.0 / Tr);
    }

    Signal rectifier = constantSignal<stateCount>(1.0);
    if ((Kp > 0.0) || (Ki > 0.0)) {
        const auto data = detail::computeRectifierData(
            inputs, Kp, Ki, Kc, 0.0, 0.0, std::numeric_limits<double>::infinity());
        rectifier.value = data.voltage;
        const std::array<index_t, 5> signalIndices{exciterIdInLocation,
                                                   exciterIqInLocation,
                                                   exciterVdInLocation,
                                                   exciterVqInLocation,
                                                   exciterXadIfdInLocation};
        for (index_t index = 0; index < 5; ++index) {
            rectifier.input[signalIndices[index]] = data.derivatives[index];
        }
    }

    const Signal fieldStateSignal =
        clampSignal(stateSignal<stateCount>(state[fieldState], fieldState), 0.0, Efdmax);
    const Signal feedbackStateSignal = stateSignal<stateCount>(state[feedbackState], feedbackState);
    const Signal feedback =
        scaleSignal(subtractSignals(fieldStateSignal, feedbackStateSignal), Kf / Tf);
    rates[feedbackState] =
        scaleSignal(subtractSignals(fieldStateSignal, feedbackStateSignal), 1.0 / Tf);

    Signal input = addSignals(constantSignal<stateCount>(Vref + vBias - 1.0),
                              inputSignal<stateCount>(inputs, exciterVsetInLocation));
    input = addSignals(input, inputSignal<stateCount>(inputs, exciterVssInLocation));
    // GridDyn OEL actions are positive reductions; OpenIPSL VOEL is signed.
    input = subtractSignals(input, inputSignal<stateCount>(inputs, exciterVoelInLocation));
    input = subtractSignals(subtractSignals(input, measuredVoltage), feedback);
    if (underSelectorEnabled && input.value < inputs[exciterVuelInLocation]) {
        input = inputSignal<stateCount>(inputs, exciterVuelInLocation);
    }
    const Signal regulatorStateSignal =
        stateSignal<stateCount>(state[regulatorState], regulatorState);
    const Signal regulator =
        clampSignal(Ta > 0.0 ? regulatorStateSignal : scaleSignal(input, Ka), Vrmin, Vrmax);
    const Signal regulatorDrive = subtractSignals(scaleSignal(input, Ka), regulatorStateSignal);
    if (Ta > 0.0 && !opFlags[REGULATOR_LIMITED]) {
        rates[regulatorState] = scaleSignal(regulatorDrive, 1.0 / Ta);
    }

    const Signal fieldDrive = subtractSignals(detail::multiplySignals(rectifier, regulator),
                                              scaleSignal(fieldStateSignal, Ke));
    if (!opFlags[FIELD_LIMITED]) {
        rates[fieldState] = scaleSignal(fieldDrive, 1.0 / Te);
    }
    evaluation.fieldOutput = fieldStateSignal.value;
    evaluation.regulatorDrive = regulatorDrive.value;
    evaluation.fieldDrive = fieldDrive.value;
    evaluation.fieldStateDerivatives = fieldStateSignal.state;
    for (index_t row = 0; row < stateCount; ++row) {
        evaluation.rates[row] = rates[row].value;
        evaluation.rateStateDerivatives[row] = rates[row].state;
        evaluation.rateInputDerivatives[row] = rates[row].input;
    }
    return evaluation;
}

void ExciterESST2A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) ||
        !std::isfinite(inputs[exciterVoelInLocation]) ||
        (underSelectorEnabled && !std::isfinite(inputs[exciterVuelInLocation])) ||
        !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("ESST2A initial voltage signals");
    }
    if ((Kp > 0.0) || (Ki > 0.0)) {
        for (const index_t inputIndex : {exciterIdInLocation,
                                         exciterIqInLocation,
                                         exciterVdInLocation,
                                         exciterVqInLocation,
                                         exciterXadIfdInLocation}) {
            if (!finiteMachineSignal(inputs[inputIndex])) {
                throw InvalidParameterValue(
                    "ESST2A requires synchronous-machine dq and field signals");
            }
        }
    }
    if (desiredOutput[0] < -initializationTolerance ||
        desiredOutput[0] > Efdmax + initializationTolerance) {
        throw InvalidParameterValue("ESST2A initial field voltage outside limits");
    }
    // Accept roundoff near a bound, but initialize output, feedback, and the
    // field equation at the same projected value.
    const double fieldVoltage = std::clamp(desiredOutput[0], 0.0, static_cast<double>(Efdmax));
    const double rectifier = rectifierVoltage(inputs);
    if (std::abs(rectifier) <= 1e-12) {
        throw InvalidParameterValue("ESST2A initial rectifier voltage");
    }
    const double regulator = Ke * fieldVoltage / rectifier;
    const double originalRegulatorUpperLimit = Vrmax;
    if ((regulator < Vrmin - initializationTolerance) ||
        !adjustInitialUpperLimit(regulator, Vrmax, "ESST2A initial regulator output")) {
        throw InvalidParameterValue("ESST2A initial state outside limits");
    }
    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[regulatorState] = regulator;
    state[feedbackState] = fieldVoltage;
    state[fieldState] = fieldVoltage;
    vBias = (regulator / Ka) + state[voltageMeasurementState] - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation] +
        inputs[exciterVoelInLocation];
    const double correction = std::max(0.0, Vrmax - originalRegulatorUpperLimit);
    const double guard = 10.0 * limiterRootTolerance + std::abs(Ka) * 1e-5;
    if (Ta > 0.0 && regulator >= Vrmax) {
        vBias += std::max(0.2 * correction, guard) / Ka;
    } else if (Ta > 0.0 && regulator <= Vrmin) {
        vBias -= guard / Ka;
    }
    fieldSet.resize(std::max<std::size_t>(fieldSet.size(), 2U));
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
    const auto initial = evaluate(inputs, state);
    if (!std::isfinite(initial.fieldOutput) ||
        std::abs(initial.fieldOutput - m_state[0]) > initializationTolerance ||
        std::any_of(initial.rates.begin(), initial.rates.end(), [](double rate) {
            return !std::isfinite(rate) || std::abs(rate) > initializationTolerance;
        })) {
        throw InvalidParameterValue("ESST2A initial selector or field equations are inconsistent");
    }
}

void ExciterESST2A::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto evaluation = evaluate(inputs, loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluation.fieldOutput - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] = evaluation.rates[index] - loc.dstateLoc[index];
        }
    }
}

void ExciterESST2A::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto evaluation = evaluate(inputs, loc.diffStateLoc);
    std::copy_n(evaluation.rates.begin(), loc.diffSize, loc.destDiffLoc);
}

void ExciterESST2A::algebraicUpdate(const IOdata& /*inputs*/,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[0] = std::clamp(loc.diffStateLoc[fieldState], 0.0, static_cast<double>(Efdmax));
    }
}

void ExciterESST2A::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            for (index_t column = 0; column < stateCount; ++column) {
                if (evaluation.fieldStateDerivatives[column] != 0.0) {
                    matrixData.assign(loc.algOffset,
                                      loc.diffOffset + column,
                                      evaluation.fieldStateDerivatives[column]);
                }
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t row = 0; row < stateCount; ++row) {
        for (index_t column = 0; column < stateCount; ++column) {
            double value = evaluation.rateStateDerivatives[row][column];
            if (row == column) {
                value -= stateData.cj;
            }
            // Preserve a diagonal even at cj=0 for held CVODE integrators.
            if (row == column || value != 0.0) {
                matrixData.assign(loc.diffOffset + row, loc.diffOffset + column, value);
            }
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            if (evaluation.rateInputDerivatives[row][input] != 0.0) {
                matrixData.assignCheckCol(loc.diffOffset + row,
                                          inputLocs[input],
                                          evaluation.rateInputDerivatives[row][input]);
            }
        }
    }
}

void ExciterESST2A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t index = 0; index < stateCount; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    updateLimitFlags(inputs, m_state.data() + 1);
    m_state[0] = m_state[fieldState + 1];
    prevTime = time;
}

bool ExciterESST2A::updateLimitFlags(const IOdata& inputs, double state[])
{
    constexpr double tolerance = 1e-7;
    const auto evaluation = evaluate(inputs, state);
    bool changed = false;
    const auto setLimited = [this,
                             &changed](int limitedFlag, int highFlag, bool limited, bool high) {
        if (opFlags[limitedFlag] != limited) {
            opFlags.set(limitedFlag, limited);
            changed = true;
        }
        if (limited && (opFlags[highFlag] != high)) {
            opFlags.set(highFlag, high);
            changed = true;
        }
    };

    const bool regulatorAbove = state[regulatorState] > Vrmax + tolerance;
    const bool regulatorBelow = state[regulatorState] < Vrmin - tolerance;
    state[regulatorState] =
        std::clamp(state[regulatorState], static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    if (Ta == 0.0) {
        opFlags.reset(REGULATOR_LIMITED);
    } else if (opFlags[REGULATOR_LIMITED]) {
        const bool release = opFlags[REGULATOR_LIMIT_HIGH] ? (evaluation.regulatorDrive < 0.0) :
                                                             (evaluation.regulatorDrive > 0.0);
        if (release) {
            setLimited(REGULATOR_LIMITED, REGULATOR_LIMIT_HIGH, false, false);
        }
    } else if (regulatorAbove ||
               ((state[regulatorState] >= Vrmax - tolerance) &&
                (evaluation.regulatorDrive > 0.0))) {
        setLimited(REGULATOR_LIMITED, REGULATOR_LIMIT_HIGH, true, true);
    } else if (regulatorBelow ||
               ((state[regulatorState] <= Vrmin + tolerance) &&
                (evaluation.regulatorDrive < 0.0))) {
        setLimited(REGULATOR_LIMITED, REGULATOR_LIMIT_HIGH, true, false);
    }

    const bool fieldAbove = state[fieldState] > Efdmax + tolerance;
    const bool fieldBelow = state[fieldState] < -tolerance;
    state[fieldState] = std::clamp(state[fieldState], 0.0, static_cast<double>(Efdmax));
    if (opFlags[FIELD_LIMITED]) {
        const bool release = opFlags[FIELD_LIMIT_HIGH] ? (evaluation.fieldDrive < 0.0) :
                                                         (evaluation.fieldDrive > 0.0);
        if (release) {
            setLimited(FIELD_LIMITED, FIELD_LIMIT_HIGH, false, false);
        }
    } else if (fieldAbove ||
               ((state[fieldState] >= Efdmax - tolerance) && (evaluation.fieldDrive > 0.0))) {
        setLimited(FIELD_LIMITED, FIELD_LIMIT_HIGH, true, true);
    } else if (fieldBelow || ((state[fieldState] <= tolerance) && (evaluation.fieldDrive < 0.0))) {
        setLimited(FIELD_LIMITED, FIELD_LIMIT_HIGH, true, false);
    }
    return changed;
}

void ExciterESST2A::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.diffStateLoc);
    const index_t root = offsets.getRootOffset(sMode);
    if (Ta == 0.0) {
        roots[root] = 1.0;
    } else if (opFlags[REGULATOR_LIMITED]) {
        // Release only after the drive is inward; the opposite margin
        // triggers on the outward side and leaves the integrator held.
        roots[root] = (opFlags[REGULATOR_LIMIT_HIGH] ? -evaluation.regulatorDrive :
                                                       evaluation.regulatorDrive) -
            limiterRootTolerance;
    } else {
        roots[root] = std::min(Vrmax - loc.diffStateLoc[regulatorState],
                               loc.diffStateLoc[regulatorState] - Vrmin) +
            limiterRootTolerance;
        opFlags.set(REGULATOR_LIMIT_HIGH, loc.diffStateLoc[regulatorState] >= Vrmax);
    }
    if (opFlags[FIELD_LIMITED]) {
        roots[root + 1] =
            (opFlags[FIELD_LIMIT_HIGH] ? -evaluation.fieldDrive : evaluation.fieldDrive) -
            limiterRootTolerance;
    } else {
        roots[root + 1] =
            std::min(Efdmax - loc.diffStateLoc[fieldState], loc.diffStateLoc[fieldState]) +
            limiterRootTolerance;
        opFlags.set(FIELD_LIMIT_HIGH, loc.diffStateLoc[fieldState] >= Efdmax);
    }
}

void ExciterESST2A::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    const index_t root = offsets.getRootOffset(sMode);
    if (((rootMask[root] != 0) || (rootMask[root + 1] != 0)) &&
        updateLimitFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterESST2A::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    if (updateLimitFlags(inputs, m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterESST2A::localStateNames() const
{
    return {"efd", "vmeas", "va", "wf", "efstate"};
}

index_t ExciterESST2A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    if ((field == "vmeas") || (field == "vc")) {
        return offset + voltageMeasurementState;
    }
    if ((field == "va") || (field == "regulator")) {
        return offset + regulatorState;
    }
    if ((field == "wf") || (field == "feedback")) {
        return offset + feedbackState;
    }
    if (field == "efstate") {
        return offset + fieldState;
    }
    return kInvalidLocation;
}

void ExciterESST2A::set(std::string_view param, std::string_view val)
{
    if (!ExciterESST2A::getModelDescription().trySetLocal(*this, param, val)) {
        Exciter::set(param, val);
    }
}

void ExciterESST2A::set(std::string_view param, double val, units::unit unitType)
{
    if (ExciterESST2A::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        return;
    }
    if ((param == "ta" && val < 0.0) || (param == "ka" && val <= 0.0)) {
        throw InvalidParameterValue("ESST2A gain or time constant");
    }
    Exciter::set(param, val, unitType);
}

double ExciterESST2A::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            ExciterESST2A::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return Exciter::get(param, unitType);
}

bool ExciterESST2A::supportsLimiterSignal(ExciterLimiterSignal signal) const
{
    return signal == ExciterLimiterSignal::OVER || underSelectorEnabled;
}
// NOLINTEND(readability-math-missing-parentheses)

void ExciterESST2A::setFlag(std::string_view param, bool value)
{
    if (!ExciterESST2A::getModelDescription().trySetFlagLocal(*this, param, value)) {
        Exciter::setFlag(param, value);
    }
}

bool ExciterESST2A::getFlag(std::string_view param) const
{
    if (const auto value = ExciterESST2A::getModelDescription().tryGetFlagLocal(*this, param)) {
        return *value;
    }
    return Exciter::getFlag(param);
}

}  // namespace griddyn::exciters
