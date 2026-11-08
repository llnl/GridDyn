/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterAC7B.h"

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
    constexpr index_t pidIntegralState = 1;
    constexpr index_t pidDerivativeState = 2;
    constexpr index_t piIntegralState = 3;
    constexpr index_t feedbackState = 4;
    constexpr index_t exciterVoltageState = 5;
    constexpr double initializationTolerance = 1e-7;
    // PSS/E's legacy rectifier initialization uses rounded reciprocal-sqrt(3)
    // and sqrt(3) constants.  These intentionally remain model constants rather
    // than std::numbers::egamma or std::numbers::sqrt3.
    constexpr double rectifierReciprocalSqrtThree = 0.577;
    constexpr double rectifierSqrtThree = 1.732;

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

const ModelDescription& ExciterAC7B::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterAC7B>(
        "ExciterAC7B",
        &Exciter::getModelDescription(),
        {parameter({"tr"}, &ExciterAC7B::Tr, units::second, "Voltage measurement time constant"),
         parameter({"kpr"}, &ExciterAC7B::Kpr, units::one, "Kpr gain or coefficient"),
         parameter({"kir"}, &ExciterAC7B::Kir, units::one, "Kir gain or coefficient"),
         parameter({"kdr", "kd_pid"}, &ExciterAC7B::Kdr, units::one, "Kdr gain or coefficient"),
         parameter(
             {"tdr", "td"}, &ExciterAC7B::Tdr, units::second, "Derivative filter time constant"),
         parameter({"kpa"}, &ExciterAC7B::Kpa, units::one, "Kpa gain or coefficient"),
         parameter({"kia"}, &ExciterAC7B::Kia, units::one, "Kia gain or coefficient"),
         parameter({"vamax", "va_max"}, &ExciterAC7B::Vamax, units::one, "Vamax upper limit"),
         parameter({"vamin", "va_min"}, &ExciterAC7B::Vamin, units::one, "Vamin lower limit"),
         parameter({"kp"}, &ExciterAC7B::Kp, units::one, "Kp gain or coefficient"),
         parameter({"kl"}, &ExciterAC7B::Kl, units::one, "Kl gain or coefficient"),
         parameter({"te"}, &ExciterAC7B::Te, units::second, "Exciter time constant"),
         parameter({"kc"}, &ExciterAC7B::Kc, units::one, "Kc gain or coefficient"),
         parameter({"kd"}, &ExciterAC7B::Kd, units::one, "Kd gain or coefficient"),
         parameter({"ke"}, &ExciterAC7B::Ke, units::one, "Ke gain or coefficient"),
         parameter({"kf1"}, &ExciterAC7B::Kf1, units::one, "Kf1 gain or coefficient"),
         parameter({"kf2"}, &ExciterAC7B::Kf2, units::one, "Kf2 gain or coefficient"),
         parameter({"kf3"}, &ExciterAC7B::Kf3, units::one, "Kf3 gain or coefficient"),
         parameter({"tf3"}, &ExciterAC7B::Tf3, units::second, "Tf3 time constant"),
         parameter({"vfemax", "vemax"}, &ExciterAC7B::Vemax, units::one, "Vemax upper limit"),
         parameter({"vemin"}, &ExciterAC7B::Vemin, units::one, "Vemin lower limit"),
         parameter({"e1"}, &ExciterAC7B::E1, units::one, "E1 model setting"),
         parameter({"se1"}, &ExciterAC7B::Se1, units::one, "Se1 model setting"),
         parameter({"e2"}, &ExciterAC7B::E2, units::one, "E2 model setting"),
         parameter({"se2"}, &ExciterAC7B::Se2, units::one, "Se2 model setting"),
         localState("pid_limited",
                    &ExciterAC7B::opFlags,
                    ExciterAC7B::PID_LIMITED,
                    "Retained pid_limited mode"),
         localState("pid_limit_high",
                    &ExciterAC7B::opFlags,
                    ExciterAC7B::PID_LIMIT_HIGH,
                    "Retained pid_limit_high mode"),
         localState("pi_limited",
                    &ExciterAC7B::opFlags,
                    ExciterAC7B::PI_LIMITED,
                    "Retained pi_limited mode"),
         localState("pi_limit_high",
                    &ExciterAC7B::opFlags,
                    ExciterAC7B::PI_LIMIT_HIGH,
                    "Retained pi_limit_high mode"),
         localState("exciter_limited",
                    &ExciterAC7B::opFlags,
                    ExciterAC7B::EXCITER_LIMITED,
                    "Retained exciter_limited mode"),
         localState("exciter_limit_high",
                    &ExciterAC7B::opFlags,
                    ExciterAC7B::EXCITER_LIMIT_HIGH,
                    "Retained exciter_limit_high mode"),
         state("efd", units::one, "Field voltage"),
         state("vmeas", units::one, "Measured terminal voltage"),
         state("pidint", units::one, "PID integral state"),
         state("pidder", units::one, "PID derivative state"),
         state("piint", units::one, "PI integral state"),
         state("xf3", units::one, "xf3 solver state"),
         state("ve", units::one, "Exciter voltage")});
    return description;
}

ExciterAC7B::ExciterAC7B(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Vrmax = 5.79;
    Vrmin = -5.79;
}

CoreObject* ExciterAC7B::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterAC7B, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    result->Tr = Tr;
    result->Kpr = Kpr;
    result->Kir = Kir;
    result->Kdr = Kdr;
    result->Tdr = Tdr;
    result->Kpa = Kpa;
    result->Kia = Kia;
    result->Vamax = Vamax;
    result->Vamin = Vamin;
    result->Kp = Kp;
    result->Kl = Kl;
    result->Te = Te;
    result->Kc = Kc;
    result->Kd = Kd;
    result->Ke = Ke;
    result->Kf1 = Kf1;
    result->Kf2 = Kf2;
    result->Kf3 = Kf3;
    result->Tf3 = Tf3;
    result->Vemax = Vemax;
    result->Vemin = Vemin;
    result->E1 = E1;
    result->Se1 = Se1;
    result->E2 = E2;
    result->Se2 = Se2;
    result->saturation = saturation;
    return result;
}

void ExciterAC7B::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    const std::array<double, 27> parameters{Tr,    Kpr,   Kir,   Kdr,   Tdr, Vrmax, Vrmin,
                                            Kpa,   Kia,   Vamax, Vamin, Kp,  Kl,    Te,
                                            Kc,    Kd,    Ke,    Kf1,   Kf2, Kf3,   Tf3,
                                            Vemax, Vemin, E1,    Se1,   E2,  Se2};
    const bool disabledSaturation = (Se1 == 0.0) && (Se2 == 0.0);
    const bool invalidSaturation = !disabledSaturation &&
        ((E1 <= 0.0) || (E2 <= 0.0) || (E1 == E2) || (Se1 <= 0.0) || (Se2 <= 0.0));
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Tr < 0.0) || (Tdr < 0.0) || (Te <= 0.0) || (Tf3 < 0.0) || (Vrmax < Vrmin) ||
        (Vamax < Vamin) || (Vemax < Vemin) || invalidSaturation) {
        throw InvalidParameterValue("AC7B gains, time constants, limits, or saturation points");
    }
    saturation.setType(disabledSaturation ?
                           utilities::Saturation::SaturationType::NONE :
                           utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    if (!disabledSaturation) {
        saturation.setParam(E1, Se1, E2, Se2);
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = stateCount;
    offsets.local().local.jacSize = 120;
    offsets.local().local.algRoots = 3;
}

double ExciterAC7B::solveExciterVoltage(double fieldVoltage, double fieldCurrent) const
{
    const double loading = Kc * fieldCurrent;
    const double middleMagnitude =
        std::sqrt(((fieldVoltage * fieldVoltage) + (loading * loading)) / 0.75);
    const double middleSign = (fieldVoltage != 0.0) ? fieldVoltage : loading;
    const std::array<double, 5> candidates{fieldVoltage,
                                           fieldVoltage + (rectifierReciprocalSqrtThree * loading),
                                           std::copysign(middleMagnitude, middleSign),
                                           (fieldVoltage / rectifierSqrtThree) + loading,
                                           loading};
    double bestVoltage = candidates.front();
    double bestMismatch = std::numeric_limits<double>::infinity();
    for (const double candidate : candidates) {
        const double normalizedCurrent = (std::abs(candidate) > 1e-14) ? loading / candidate : 0.0;
        const double output = candidate * detail::computeRectifierFactor(normalizedCurrent).factor;
        const double mismatch = std::abs(output - fieldVoltage);
        if (mismatch < bestMismatch) {
            bestMismatch = mismatch;
            bestVoltage = candidate;
        }
    }
    if (!std::isfinite(bestVoltage) ||
        (bestMismatch > 1e-9 * std::max(1.0, std::abs(fieldVoltage)))) {
        throw InvalidParameterValue("AC7B initial rectifier equation has no finite solution");
    }
    return bestVoltage;
}

double ExciterAC7B::solveFieldFeedbackLimit(double fieldCurrent, double initialVoltage) const
{
    const double target = Vemax - (Kd * fieldCurrent);
    double voltage = initialVoltage;
    if (!std::isfinite(voltage)) {
        voltage = (std::abs(Ke) > 1e-12) ? target / Ke : Vemax;
    }
    for (int iteration = 0; iteration < 40; ++iteration) {
        const auto sat = saturation.evaluate(voltage);
        const double mismatch = ((Ke + sat.value) * voltage) - target;
        if (std::abs(mismatch) <= 1e-11 * std::max(1.0, std::abs(target))) {
            return voltage;
        }
        const double slope = Ke + sat.value + (voltage * sat.derivative);
        if (!std::isfinite(slope) || (std::abs(slope) < 1e-12)) {
            break;
        }
        voltage -= mismatch / slope;
    }
    throw InvalidParameterValue("AC7B exciter upper-limit equation has no finite solution");
}

ExciterAC7B::Evaluation
    ExciterAC7B::evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const
{
    using detail::addSignals;
    using detail::algebraicSignal;
    using detail::clampSignal;
    using detail::constantSignal;
    using detail::divideSignals;
    using detail::inputSignal;
    using detail::multiplySignals;
    using detail::rectifierFactorSignal;
    using detail::scaleSignal;
    using detail::stateSignal;
    using detail::subtractSignals;
    using Signal = detail::ExciterSignal<stateCount>;

    Evaluation evaluation;
    std::array<Signal, stateCount> rates{};
    const Signal terminalVoltage = inputSignal<stateCount>(inputs, exciterVoltageInLocation);
    const Signal measuredVoltage = (Tr > 0.0) ?
        stateSignal<stateCount>(state[voltageMeasurementState], voltageMeasurementState) :
        terminalVoltage;
    if (Tr > 0.0) {
        rates[voltageMeasurementState] =
            scaleSignal(subtractSignals(terminalVoltage, measuredVoltage), 1.0 / Tr);
    }

    const Signal exciterVoltage =
        stateSignal<stateCount>(state[exciterVoltageState], exciterVoltageState);
    const auto saturationData = saturation.evaluate(exciterVoltage.value);
    const Signal saturationSignal =
        detail::applyFunction(exciterVoltage, saturationData.value, saturationData.derivative);
    const Signal fieldCurrent = inputSignal<stateCount>(inputs, exciterXadIfdInLocation);
    const Signal saturationGain = addSignals(constantSignal<stateCount>(Ke), saturationSignal);
    const Signal fieldFeedback =
        addSignals(multiplySignals(saturationGain, exciterVoltage), scaleSignal(fieldCurrent, Kd));
    const Signal fieldOutput =
        multiplySignals(rectifierFactorSignal(
                            divideSignals(scaleSignal(fieldCurrent, Kc), exciterVoltage)),
                        exciterVoltage);

    const Signal feedbackFilter = stateSignal<stateCount>(state[feedbackState], feedbackState);
    Signal derivativeFeedback = constantSignal<stateCount>(0.0);
    if (Tf3 > 0.0) {
        rates[feedbackState] =
            scaleSignal(subtractSignals(fieldFeedback, feedbackFilter), 1.0 / Tf3);
        derivativeFeedback = scaleSignal(subtractSignals(fieldFeedback, feedbackFilter), Kf3 / Tf3);
    }

    Signal input = addSignals(constantSignal<stateCount>(Vref + vBias - 1.0),
                              inputSignal<stateCount>(inputs, exciterVsetInLocation));
    input = addSignals(input, inputSignal<stateCount>(inputs, exciterVssInLocation));
    input = subtractSignals(subtractSignals(input, measuredVoltage), derivativeFeedback);

    const Signal derivativeState =
        stateSignal<stateCount>(state[pidDerivativeState], pidDerivativeState);
    Signal derivativeOutput = constantSignal<stateCount>(0.0);
    if (Tdr > 0.0) {
        rates[pidDerivativeState] = scaleSignal(subtractSignals(input, derivativeState), 1.0 / Tdr);
        derivativeOutput = scaleSignal(subtractSignals(input, derivativeState), Kdr / Tdr);
    }
    const Signal pidIntegral = stateSignal<stateCount>(state[pidIntegralState], pidIntegralState);
    const Signal pidDrive =
        addSignals(addSignals(scaleSignal(input, Kpr), pidIntegral), derivativeOutput);
    const Signal pidOutput = clampSignal(pidDrive, Vrmin, Vrmax);
    const Signal pidIntegratorDrive = scaleSignal(input, Kir);
    if (!opFlags[PID_LIMITED]) {
        rates[pidIntegralState] = pidIntegratorDrive;
    }

    const Signal regulatorFeedback =
        addSignals(scaleSignal(algebraicSignal<stateCount>(fieldVoltage), Kf1),
                   scaleSignal(fieldFeedback, Kf2));
    const Signal piError = subtractSignals(pidOutput, regulatorFeedback);
    const Signal piIntegral = stateSignal<stateCount>(state[piIntegralState], piIntegralState);
    const Signal piDrive = addSignals(scaleSignal(piError, Kpa), piIntegral);
    const Signal piOutput = clampSignal(piDrive, Vamin, Vamax);
    const Signal piIntegratorDrive = scaleSignal(piError, Kia);
    if (!opFlags[PI_LIMITED]) {
        rates[piIntegralState] = piIntegratorDrive;
    }

    const Signal lowerControlLimit = scaleSignal(fieldFeedback, -Kl);
    // PSLF ESAC7B data can use Kp=0 to select the unity (non-potential)
    // source. The published AC7B equation Kp*Vt*Va has no usable equilibrium
    // for literal Kp=0 unless the field feedback is also zero. Keep the
    // normal potential-source path for nonzero Kp, and use Va directly for
    // the zero-Kp compatibility mode.
    const Signal sourceVoltage =
        (Kp == 0.0) ? constantSignal<stateCount>(1.0) : scaleSignal(terminalVoltage, Kp);
    Signal exciterControl = multiplySignals(sourceVoltage, piOutput);
    if (exciterControl.value < lowerControlLimit.value) {
        exciterControl = lowerControlLimit;
    }
    // VE limits constrain the integrator state.  The input limiter only
    // imposes I_C >= -K_L V_FE; it does not clamp I_C to the VE bounds.
    const Signal exciterDrive = subtractSignals(exciterControl, fieldFeedback);
    if (!opFlags[EXCITER_LIMITED]) {
        rates[exciterVoltageState] = scaleSignal(exciterDrive, 1.0 / Te);
    }

    const Signal algebraicResidual =
        subtractSignals(fieldOutput, algebraicSignal<stateCount>(fieldVoltage));
    evaluation.fieldOutput = fieldOutput.value;
    evaluation.pidDrive = pidDrive.value;
    evaluation.pidIntegratorDrive = pidIntegratorDrive.value;
    evaluation.piDrive = piDrive.value;
    evaluation.piIntegratorDrive = piIntegratorDrive.value;
    evaluation.fieldFeedback = fieldFeedback.value;
    evaluation.exciterDrive = exciterDrive.value;
    evaluation.algebraicDerivative = algebraicResidual.algebraic;
    evaluation.fieldStateDerivatives = algebraicResidual.state;
    evaluation.fieldInputDerivatives = algebraicResidual.input;
    for (index_t row = 0; row < stateCount; ++row) {
        evaluation.rates[row] = rates[row].value;
        evaluation.rateAlgebraicDerivatives[row] = rates[row].algebraic;
        evaluation.rateStateDerivatives[row] = rates[row].state;
        evaluation.rateInputDerivatives[row] = rates[row].input;
    }
    return evaluation;
}

void ExciterAC7B::dynObjectInitializeB(const IOdata& inputs,
                                       const IOdata& desiredOutput,
                                       IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0]) ||
        (((Kc != 0.0) || (Kd != 0.0)) && !finiteMachineSignal(inputs[exciterXadIfdInLocation]))) {
        throw InvalidParameterValue("AC7B initial signals");
    }
    const double fieldVoltage = desiredOutput[0];
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const double exciterVoltage = solveExciterVoltage(fieldVoltage, fieldCurrent);
    const double fieldFeedback =
        (Ke + saturation(exciterVoltage)) * exciterVoltage + (Kd * fieldCurrent);
    const double terminalVoltage = std::max(1e-8, inputs[exciterVoltageInLocation]);
    // See the matching runtime branch in evaluate(): zero Kp denotes PSLF's
    // unity-source option, rather than a literal zero multiplier.
    const double sourceVoltage = (Kp == 0.0) ? 1.0 : Kp * terminalVoltage;
    const double piOutput = fieldFeedback / sourceVoltage;
    const double pidOutput = (Kf1 * fieldVoltage) + (Kf2 * fieldFeedback);
    const double originalPidUpperLimit = Vrmax;
    const double originalPiUpperLimit = Vamax;
    const double rectifierMismatch =
        (exciterVoltage *
         detail::computeRectifierFactor(
             (std::abs(exciterVoltage) > 1e-14) ? Kc * fieldCurrent / exciterVoltage : 0.0)
             .factor) -
        fieldVoltage;
    if ((pidOutput < Vrmin - initializationTolerance) ||
        !adjustInitialUpperLimit(pidOutput, Vrmax, "AC7B initial PID output") ||
        (piOutput < Vamin - initializationTolerance) ||
        !adjustInitialUpperLimit(piOutput, Vamax, "AC7B initial PI output") ||
        (exciterVoltage < Vemin - initializationTolerance) ||
        (fieldFeedback > Vemax + initializationTolerance) ||
        (std::abs(rectifierMismatch) > initializationTolerance)) {
        throw InvalidParameterValue(
            "AC7B initial state outside limits: PID=" + std::to_string(pidOutput) +
            ", PI=" + std::to_string(piOutput) + ", VE=" + std::to_string(exciterVoltage) +
            ", VFE=" + std::to_string(fieldFeedback));
    }

    m_state[0] = fieldVoltage;
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[pidDerivativeState] = 0.0;
    state[pidIntegralState] = pidOutput;
    state[piIntegralState] = piOutput;
    state[feedbackState] = fieldFeedback;
    state[exciterVoltageState] = exciterVoltage;
    vBias = state[voltageMeasurementState] - Vref - (inputs[exciterVsetInLocation] - 1.0) -
        inputs[exciterVssInLocation];
    const double guard = 10.0 * limiterRootTolerance;
    if ((Kpa > 0.0) && (Kia > 0.0)) {
        const double correction = std::max(0.0, Vamax - originalPiUpperLimit);
        const double margin = std::max(0.2 * correction, guard);
        const double pidIncrement = std::max(margin / Kpa, margin / Kia);
        if ((piOutput >= Vamax) && (pidOutput + pidIncrement < Vrmax)) {
            state[pidIntegralState] += pidIncrement;
            opFlags.set(PI_LIMITED);
            opFlags.set(PI_LIMIT_HIGH);
        } else if ((piOutput <= Vamin) && (pidOutput - pidIncrement > Vrmin)) {
            state[pidIntegralState] -= pidIncrement;
            opFlags.set(PI_LIMITED);
            opFlags.reset(PI_LIMIT_HIGH);
        }
    }
    if ((Kpr > 0.0) && (Kir > 0.0)) {
        const double correction = std::max(0.0, Vrmax - originalPidUpperLimit);
        const double margin = std::max(0.2 * correction, guard);
        const double inputMargin = std::max({margin / Kpr, margin / Kir, 1e-5});
        if (pidOutput >= Vrmax) {
            vBias += inputMargin;
            state[pidDerivativeState] += inputMargin;
            opFlags.set(PID_LIMITED);
            opFlags.set(PID_LIMIT_HIGH);
        } else if (pidOutput <= Vrmin) {
            vBias -= inputMargin;
            state[pidDerivativeState] -= inputMargin;
            opFlags.set(PID_LIMITED);
            opFlags.reset(PID_LIMIT_HIGH);
        }
    }
    fieldSet.resize(std::max<std::size_t>(fieldSet.size(), 2U));
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state, true);
}

void ExciterAC7B::residual(const IOdata& inputs,
                           const StateData& stateData,
                           double resid[],
                           const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = evaluation.fieldOutput - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] = evaluation.rates[index] - loc.dstateLoc[index];
        }
    }
}

void ExciterAC7B::derivative(const IOdata& inputs,
                             const StateData& stateData,
                             double deriv[],
                             const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    std::copy_n(evaluation.rates.begin(), loc.diffSize, loc.destDiffLoc);
}

void ExciterAC7B::algebraicUpdate(const IOdata& inputs,
                                  const StateData& stateData,
                                  double update[],
                                  const SolverMode& sMode,
                                  double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[0] = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc).fieldOutput;
    }
}

void ExciterAC7B::jacobianElements(const IOdata& inputs,
                                   const StateData& stateData,
                                   MatrixData<double>& matrixData,
                                   const IOlocs& inputLocs,
                                   const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, evaluation.algebraicDerivative);
        if (!isAlgebraicOnly(sMode)) {
            for (index_t column = 0; column < stateCount; ++column) {
                matrixData.assign(loc.algOffset,
                                  loc.diffOffset + column,
                                  evaluation.fieldStateDerivatives[column]);
            }
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            matrixData.assignCheckCol(loc.algOffset,
                                      inputLocs[input],
                                      evaluation.fieldInputDerivatives[input]);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t row = 0; row < stateCount; ++row) {
        matrixData.assign(loc.diffOffset + row,
                          loc.algOffset,
                          evaluation.rateAlgebraicDerivatives[row]);
        for (index_t column = 0; column < stateCount; ++column) {
            double value = evaluation.rateStateDerivatives[row][column];
            if (row == column) {
                value -= stateData.cj;
            }
            matrixData.assign(loc.diffOffset + row, loc.diffOffset + column, value);
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            matrixData.assignCheckCol(loc.diffOffset + row,
                                      inputLocs[input],
                                      evaluation.rateInputDerivatives[row][input]);
        }
    }
}

void ExciterAC7B::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    for (index_t index = 0; index < stateCount; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    updateLimitFlags(inputs, m_state.data() + 1, true);
    m_state[0] = evaluate(inputs, m_state[0], m_state.data() + 1).fieldOutput;
    prevTime = time;
}

bool ExciterAC7B::updateLimitFlags(const IOdata& inputs, double state[], bool projectStates)
{
    constexpr double tolerance = 1e-7;
    const auto evaluation = evaluate(inputs, m_state[0], state);
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

    if (opFlags[PID_LIMITED]) {
        const bool release = opFlags[PID_LIMIT_HIGH] ?
            ((evaluation.pidDrive < Vrmax - tolerance) || (evaluation.pidIntegratorDrive < 0.0)) :
            ((evaluation.pidDrive > Vrmin + tolerance) || (evaluation.pidIntegratorDrive > 0.0));
        if (release) {
            setLimited(PID_LIMITED, PID_LIMIT_HIGH, false, false);
        }
    } else if ((evaluation.pidDrive >= Vrmax - tolerance) &&
               (evaluation.pidIntegratorDrive > 0.0)) {
        setLimited(PID_LIMITED, PID_LIMIT_HIGH, true, true);
    } else if ((evaluation.pidDrive <= Vrmin + tolerance) &&
               (evaluation.pidIntegratorDrive < 0.0)) {
        setLimited(PID_LIMITED, PID_LIMIT_HIGH, true, false);
    }

    if (opFlags[PI_LIMITED]) {
        const bool release = opFlags[PI_LIMIT_HIGH] ?
            ((evaluation.piDrive < Vamax - tolerance) || (evaluation.piIntegratorDrive < 0.0)) :
            ((evaluation.piDrive > Vamin + tolerance) || (evaluation.piIntegratorDrive > 0.0));
        if (release) {
            setLimited(PI_LIMITED, PI_LIMIT_HIGH, false, false);
        }
    } else if ((evaluation.piDrive >= Vamax - tolerance) && (evaluation.piIntegratorDrive > 0.0)) {
        setLimited(PI_LIMITED, PI_LIMIT_HIGH, true, true);
    } else if ((evaluation.piDrive <= Vamin + tolerance) && (evaluation.piIntegratorDrive < 0.0)) {
        setLimited(PI_LIMITED, PI_LIMIT_HIGH, true, false);
    }

    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const bool exciterAbove = evaluation.fieldFeedback > Vemax + tolerance;
    const bool exciterBelow = state[exciterVoltageState] < Vemin - tolerance;
    if (projectStates) {
        state[exciterVoltageState] =
            std::max(state[exciterVoltageState], static_cast<double>(Vemin));
        const double voltage = state[exciterVoltageState];
        const double feedback = (Ke + saturation(voltage)) * voltage + (Kd * fieldCurrent);
        if (feedback > Vemax) {
            state[exciterVoltageState] = solveFieldFeedbackLimit(fieldCurrent, voltage);
        }
    }
    if (opFlags[EXCITER_LIMITED]) {
        const bool release = opFlags[EXCITER_LIMIT_HIGH] ?
            ((evaluation.fieldFeedback < Vemax - tolerance) || (evaluation.exciterDrive < 0.0)) :
            ((state[exciterVoltageState] > Vemin + tolerance) || (evaluation.exciterDrive > 0.0));
        if (release) {
            setLimited(EXCITER_LIMITED, EXCITER_LIMIT_HIGH, false, false);
        }
    } else if (exciterAbove ||
               ((evaluation.fieldFeedback >= Vemax - tolerance) &&
                (evaluation.exciterDrive > 0.0))) {
        setLimited(EXCITER_LIMITED, EXCITER_LIMIT_HIGH, true, true);
    } else if (exciterBelow ||
               ((state[exciterVoltageState] <= Vemin + tolerance) &&
                (evaluation.exciterDrive < 0.0))) {
        setLimited(EXCITER_LIMITED, EXCITER_LIMIT_HIGH, true, false);
    }
    return changed;
}

void ExciterAC7B::rootTest(const IOdata& inputs,
                           const StateData& stateData,
                           double roots[],
                           const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, loc.algStateLoc[0], loc.diffStateLoc);
    const index_t root = offsets.getRootOffset(sMode);
    if (opFlags[PID_LIMITED]) {
        roots[root] = opFlags[PID_LIMIT_HIGH] ?
            std::max(Vrmax - evaluation.pidDrive, -evaluation.pidIntegratorDrive) :
            std::max(evaluation.pidDrive - Vrmin, evaluation.pidIntegratorDrive);
        roots[root] += limiterRootTolerance;
    } else {
        roots[root] = std::min(Vrmax - evaluation.pidDrive, evaluation.pidDrive - Vrmin) +
            limiterRootTolerance;
        opFlags.set(PID_LIMIT_HIGH, evaluation.pidDrive >= Vrmax);
    }
    if (opFlags[PI_LIMITED]) {
        roots[root + 1] = opFlags[PI_LIMIT_HIGH] ?
            std::max(Vamax - evaluation.piDrive, -evaluation.piIntegratorDrive) :
            std::max(evaluation.piDrive - Vamin, evaluation.piIntegratorDrive);
        roots[root + 1] += limiterRootTolerance;
    } else {
        roots[root + 1] =
            std::min(Vamax - evaluation.piDrive, evaluation.piDrive - Vamin) + limiterRootTolerance;
        opFlags.set(PI_LIMIT_HIGH, evaluation.piDrive >= Vamax);
    }
    if (opFlags[EXCITER_LIMITED]) {
        roots[root + 2] = opFlags[EXCITER_LIMIT_HIGH] ?
            std::max(Vemax - evaluation.fieldFeedback, -evaluation.exciterDrive) :
            std::max(loc.diffStateLoc[exciterVoltageState] - Vemin, evaluation.exciterDrive);
        roots[root + 2] += limiterRootTolerance;
    } else {
        roots[root + 2] = std::min(Vemax - evaluation.fieldFeedback,
                                   loc.diffStateLoc[exciterVoltageState] - Vemin) +
            limiterRootTolerance;
        opFlags.set(EXCITER_LIMIT_HIGH, evaluation.fieldFeedback >= Vemax);
    }
}

void ExciterAC7B::rootTrigger(CoreTime /*time*/,
                              const IOdata& inputs,
                              const std::vector<int>& rootMask,
                              const SolverMode& sMode)
{
    const index_t root = offsets.getRootOffset(sMode);
    if ((rootMask[root] != 0) || (rootMask[root + 1] != 0) || (rootMask[root + 2] != 0)) {
        updateLimitFlags(inputs, m_state.data() + 1, true);
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterAC7B::rootCheck(const IOdata& inputs,
                                  const StateData& /*stateData*/,
                                  const SolverMode& /*sMode*/,
                                  CheckLevel /*level*/)
{
    if (updateLimitFlags(inputs, m_state.data() + 1, true)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterAC7B::localStateNames() const
{
    return {"efd", "vmeas", "pidint", "pidder", "piint", "xf3", "ve"};
}

index_t ExciterAC7B::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto offset = offsets.getDiffOffset(sMode);
    if ((field == "vmeas") || (field == "vc")) {
        return offset + voltageMeasurementState;
    }
    if (field == "pidint") {
        return offset + pidIntegralState;
    }
    if (field == "pidder") {
        return offset + pidDerivativeState;
    }
    if (field == "piint") {
        return offset + piIntegralState;
    }
    if (field == "xf3") {
        return offset + feedbackState;
    }
    if ((field == "ve") || (field == "exciter")) {
        return offset + exciterVoltageState;
    }
    return kInvalidLocation;
}

// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
