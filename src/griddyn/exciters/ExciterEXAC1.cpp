/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterEXAC1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::exciters {
// The published transfer-function equations are intentionally kept in their
// conventional form.  Parentheses at every multiplication obscure them.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t leadLagState = 1;
    constexpr index_t regulatorState = 2;
    constexpr index_t exciterState = 3;
    constexpr index_t washoutState = 4;
    constexpr double limitTolerance = 1e-7;
    // These decimal values are the specified PSS/E FEX curve coefficients,
    // not approximations of unrelated mathematical constants.
    constexpr double lowCurrentSlope = 0.577;  // NOLINT(modernize-use-std-numbers)
    constexpr double highCurrentSlope = 1.732;  // NOLINT(modernize-use-std-numbers)

    struct RectifierEvaluation {
        double mFactor;
        double mDerivative;
    };

    RectifierEvaluation rectifier(double normalizedCurrent)
    {
        if (normalizedCurrent <= 0.0) {
            return {.mFactor = 1.0, .mDerivative = 0.0};
        }
        if (normalizedCurrent <= 0.433) {
            return {.mFactor = 1.0 - lowCurrentSlope * normalizedCurrent,
                    .mDerivative = -lowCurrentSlope};
        }
        if (normalizedCurrent <= 0.75) {
            const double factor =
                std::sqrt(std::max(0.0, 0.75 - normalizedCurrent * normalizedCurrent));
            return {.mFactor = factor,
                    .mDerivative = (factor > 0.0) ? -normalizedCurrent / factor : 0.0};
        }
        if (normalizedCurrent <= 1.0) {
            return {.mFactor = highCurrentSlope * (1.0 - normalizedCurrent),
                    .mDerivative = -highCurrentSlope};
        }
        return {.mFactor = 0.0, .mDerivative = 0.0};
    }

    index_t stateIndex(index_t fullIndex, bool hasVoltageTransducer)
    {
        return hasVoltageTransducer ? fullIndex : fullIndex - 1;
    }
}  // namespace

ExciterEXAC1::ExciterEXAC1(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 80.0;
    Ta = 0.04;
    Vrmax = 8.0;
    Vrmin = 0.0;
}

CoreObject* ExciterEXAC1::clone(CoreObject* obj) const
{
    auto* clone = cloneBase<ExciterEXAC1, Exciter>(this, obj);
    if (clone == nullptr) {
        return obj;
    }
    clone->Tr = Tr;
    clone->Tb = Tb;
    clone->Tc = Tc;
    clone->Te = Te;
    clone->Kf = Kf;
    clone->Tf = Tf;
    clone->Kc = Kc;
    clone->Kd = Kd;
    clone->Ke = Ke;
    clone->E1 = E1;
    clone->Se1 = Se1;
    clone->E2 = E2;
    clone->Se2 = Se2;
    clone->saturation = saturation;
    return clone;
}

void ExciterEXAC1::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Tr) || !std::isfinite(Tb) || !std::isfinite(Tc) || !std::isfinite(Ka) ||
        !std::isfinite(Ta) || !std::isfinite(Vrmax) || !std::isfinite(Vrmin) ||
        !std::isfinite(Te) || !std::isfinite(Kf) || !std::isfinite(Tf) || !std::isfinite(Kc) ||
        !std::isfinite(Kd) || !std::isfinite(Ke) || !std::isfinite(E1) || !std::isfinite(Se1) ||
        !std::isfinite(E2) || !std::isfinite(Se2) || (Tr < 0.0) || (Tb <= 0.0) || (Ta <= 0.0) ||
        (Te <= 0.0) || (Tf <= 0.0) || (Ka <= 0.0) || (Vrmax < Vrmin) ||
        (regulatorUpperLimit() < regulatorLowerLimit())) {
        throw InvalidParameterValue("EXAC1 gains, time constants, or limits");
    }
    // ANDES disables ExcQuadSat when SE2 is zero.  Saturation::QUADRATIC
    // intentionally does not infer that convention, because its generic
    // two-point fit permits no zero denominator.
    if (Se2 == 0.0) {
        saturation.setType(utilities::Saturation::SaturationType::NONE);
    } else {
        if ((E1 <= 0.0) || (E2 <= 0.0) || (Se1 <= 0.0) || (Se2 <= 0.0)) {
            throw InvalidParameterValue("EXAC1 enabled saturation points");
        }
        saturation.setType(utilities::Saturation::SaturationType::QUADRATIC);
        saturation.setParam(E1, E1 * Se1, E2, E2 * Se2);
    }
    const bool hasVoltageTransducer = (Tr > 0.0);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = hasVoltageTransducer ? 5 : 4;
    offsets.local().local.algRoots = 1;
    offsets.local().local.jacSize = 34;
}

void ExciterEXAC1::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    const double voltage = inputs[exciterVoltageInLocation];
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    if (!std::isfinite(voltage) ||
        ((!std::isfinite(fieldCurrent) || (std::abs(fieldCurrent) > 1e20)) &&
         ((Kc != 0.0) || (Kd != 0.0)))) {
        throw InvalidParameterValue("EXAC1 requires compatible synchronous-machine signals");
    }
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("EXAC1 initial field voltage");
    }
    const double fieldVoltage = desiredOutput[0];
    double exciterVoltage = std::max(0.01, std::abs(fieldVoltage));
    for (int count = 0; count < 20; ++count) {
        const double current =
            ((Kc != 0.0) && (exciterVoltage != 0.0)) ? Kc * fieldCurrent / exciterVoltage : 0.0;
        const auto fex = rectifier(current);
        const double mismatch = exciterVoltage * fex.mFactor - fieldVoltage;
        const double slope = fex.mFactor - fex.mDerivative * current;
        if (std::abs(slope) < 1e-12) {
            break;
        }
        exciterVoltage = std::max(1e-8, exciterVoltage - mismatch / slope);
        if (std::abs(mismatch) < 1e-12) {
            break;
        }
    }
    const double finalCurrent =
        ((Kc != 0.0) && (exciterVoltage != 0.0)) ? Kc * fieldCurrent / exciterVoltage : 0.0;
    if (std::abs(exciterVoltage * rectifier(finalCurrent).mFactor - fieldVoltage) > 1e-7) {
        throw InvalidParameterValue(
            "EXAC1 initial field voltage is incompatible with rectifier loading");
    }
    const bool hasVoltageTransducer = (Tr > 0.0);
    double* state = m_state.data() + 1;
    if (hasVoltageTransducer) {
        state[voltageMeasurementState] = voltage;
    }
    const auto regulatorIndex = stateIndex(regulatorState, hasVoltageTransducer);
    const auto exciterIndex = stateIndex(exciterState, hasVoltageTransducer);
    const auto washoutIndex = stateIndex(washoutState, hasVoltageTransducer);
    state[exciterIndex] = exciterVoltage;
    state[washoutIndex] = vfe(inputs, state);
    state[regulatorIndex] = initialRegulatorState(state[washoutIndex]);
    if ((state[regulatorIndex] < regulatorLowerLimit() - limitTolerance) ||
        (state[regulatorIndex] > regulatorUpperLimit() + limitTolerance)) {
        throw InvalidParameterValue("EXAC1 initial regulator output outside limits");
    }
    state[stateIndex(leadLagState, hasVoltageTransducer)] = state[regulatorIndex] / Ka;
    m_state[0] = fieldVoltage;
    const double setpointInput = inputs[exciterVsetInLocation] - 1.0;
    vBias = voltage + referenceOffset(state[washoutIndex]) - Vref - setpointInput -
        inputs[exciterVssInLocation];
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(state);
}

double ExciterEXAC1::referenceInput(const IOdata& inputs) const
{
    return Vref + vBias + inputs[exciterVsetInLocation] - 1.0 + inputs[exciterVssInLocation];
}

double ExciterEXAC1::vfe(const IOdata& inputs, const double state[]) const
{
    const auto exciterIndex = stateIndex(exciterState, Tr > 0.0);
    const double fieldFeedback = Ke * state[exciterIndex] + saturation(state[exciterIndex]);
    return (Kd == 0.0) ? fieldFeedback : fieldFeedback + Kd * inputs[exciterXadIfdInLocation];
}

double ExciterEXAC1::rectifierFactor(const IOdata& inputs, double exciterVoltage) const
{
    if (Kc == 0.0) {
        return 1.0;
    }
    return rectifier((exciterVoltage != 0.0) ?
                         Kc * inputs[exciterXadIfdInLocation] / exciterVoltage :
                         0.0)
        .mFactor;
}

double ExciterEXAC1::fieldVoltage(const IOdata& inputs, const double state[]) const
{
    const auto exciterIndex = stateIndex(exciterState, Tr > 0.0);
    return state[exciterIndex] * rectifierFactor(inputs, state[exciterIndex]);
}

double ExciterEXAC1::regulatorTarget(const IOdata& /*inputs*/, const double state[]) const
{
    return state[stateIndex(regulatorState, Tr > 0.0)];
}

double ExciterEXAC1::regulatorUpperLimit() const
{
    return Vrmax;
}
double ExciterEXAC1::regulatorLowerLimit() const
{
    return Vrmin;
}
double ExciterEXAC1::initialRegulatorState(double vfeValue) const
{
    return vfeValue;
}
double ExciterEXAC1::referenceOffset(double vfeValue) const
{
    return vfeValue / Ka;
}
void ExciterEXAC1::regulatorTargetDerivatives(const IOdata& /*inputs*/,
                                              const double state[],
                                              double& regulatorDerivative,
                                              double& exciterDerivative,
                                              double& fieldCurrentDerivative) const
{
    static_cast<void>(state);
    regulatorDerivative = 1.0;
    exciterDerivative = 0.0;
    fieldCurrentDerivative = 0.0;
}

int ExciterEXAC1::regulatorLimitStatus(const double state[]) const
{
    const auto regulatorIndex = stateIndex(regulatorState, Tr > 0.0);
    if (state[regulatorIndex] >= regulatorUpperLimit()) {
        return 1;
    }
    return (state[regulatorIndex] <= regulatorLowerLimit()) ? -1 : 0;
}

bool ExciterEXAC1::updateLimitFlags(const double state[])
{
    const int status = regulatorLimitStatus(state);
    const bool changed = (opFlags[REGULATOR_LIMITED] != (status != 0)) ||
        (opFlags[REGULATOR_LIMIT_HIGH] != (status > 0));
    opFlags.set(REGULATOR_LIMITED, status != 0);
    opFlags.set(REGULATOR_LIMIT_HIGH, status > 0);
    return changed;
}

void ExciterEXAC1::residual(const IOdata& inputs,
                            const StateData& stateData,
                            double resid[],
                            const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] =
            fieldVoltage(inputs, locations.diffStateLoc) - locations.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < locations.diffSize; ++index) {
            locations.destDiffLoc[index] -= locations.dstateLoc[index];
        }
    }
}

void ExciterEXAC1::derivative(const IOdata& inputs,
                              const StateData& stateData,
                              double deriv[],
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* derivativeValues = locations.destDiffLoc;
    const bool hasVoltageTransducer = (Tr > 0.0);
    const auto leadLagIndex = stateIndex(leadLagState, hasVoltageTransducer);
    const auto regulatorIndex = stateIndex(regulatorState, hasVoltageTransducer);
    const auto exciterIndex = stateIndex(exciterState, hasVoltageTransducer);
    const auto washoutIndex = stateIndex(washoutState, hasVoltageTransducer);
    const double measuredVoltage = hasVoltageTransducer ? state[voltageMeasurementState] :
                                                        inputs[exciterVoltageInLocation];
    const double fieldFeedback = vfe(inputs, state);
    const double input = referenceInput(inputs) - measuredVoltage -
        Kf * (fieldFeedback - state[washoutIndex]) / Tf;
    const double leadOutput = state[leadLagIndex] + Tc * (input - state[leadLagIndex]) / Tb;
    const double regulatorDerivative = (Ka * leadOutput - state[regulatorIndex]) / Ta;
    const int status = regulatorLimitStatus(state);
    if (hasVoltageTransducer) {
        derivativeValues[voltageMeasurementState] =
            (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    }
    derivativeValues[leadLagIndex] = (input - state[leadLagIndex]) / Tb;
    derivativeValues[regulatorIndex] = ((status > 0) && (regulatorDerivative > 0.0)) ||
            ((status < 0) && (regulatorDerivative < 0.0)) ?
        0.0 :
        regulatorDerivative;
    derivativeValues[exciterIndex] = (regulatorTarget(inputs, state) - fieldFeedback) / Te;
    derivativeValues[washoutIndex] = (fieldFeedback - state[washoutIndex]) / Tf;
}

void ExciterEXAC1::jacobianElements(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t algOffset = locations.algOffset;
    const index_t diffOffset = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const bool hasVoltageTransducer = (Tr > 0.0);
    const auto leadLagIndex = stateIndex(leadLagState, hasVoltageTransducer);
    const auto regulatorIndex = stateIndex(regulatorState, hasVoltageTransducer);
    const auto exciterIndex = stateIndex(exciterState, hasVoltageTransducer);
    const auto washoutIndex = stateIndex(washoutState, hasVoltageTransducer);
    const double exciterVoltage = state[exciterIndex];
    const double normalizedCurrent =
        (exciterVoltage != 0.0) ? Kc * inputs[exciterXadIfdInLocation] / exciterVoltage : 0.0;
    const auto fex = rectifier(normalizedCurrent);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(algOffset, algOffset, -1.0);
        matrixData.assign(algOffset,
                          diffOffset + exciterIndex,
                          fex.mFactor - fex.mDerivative * normalizedCurrent);
        matrixData.assignCheckCol(algOffset,
                                  inputLocs[exciterXadIfdInLocation],
                                  fex.mDerivative * Kc);
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double saturationSlope = Ke + saturation.deriv(exciterVoltage);
    const double feedbackGain = Kf / Tf;
    const double leadRatio = Tc / Tb;
    const double measuredVoltage = hasVoltageTransducer ? state[voltageMeasurementState] :
                                                        inputs[exciterVoltageInLocation];
    const double regulatorDerivative =
        (Ka *
             (state[leadLagIndex] +
              leadRatio *
                  (referenceInput(inputs) - measuredVoltage -
                   feedbackGain * (vfe(inputs, state) - state[washoutIndex]) -
                   state[leadLagIndex])) -
         state[regulatorIndex]) /
        Ta;
    const int status = regulatorLimitStatus(state);
    const bool frozen = ((status > 0) && (regulatorDerivative > 0.0)) ||
        ((status < 0) && (regulatorDerivative < 0.0));

    if (hasVoltageTransducer) {
        matrixData.assign(diffOffset + voltageMeasurementState,
                          diffOffset + voltageMeasurementState,
                          -1.0 / Tr - stateData.cj);
        matrixData.assignCheckCol(diffOffset + voltageMeasurementState,
                                  inputLocs[exciterVoltageInLocation],
                                  1.0 / Tr);
        matrixData.assign(diffOffset + leadLagIndex, diffOffset + voltageMeasurementState, -1.0 / Tb);
    } else {
        matrixData.assignCheckCol(diffOffset + leadLagIndex,
                                  inputLocs[exciterVoltageInLocation],
                                  -1.0 / Tb);
    }
    matrixData.assign(diffOffset + leadLagIndex,
                      diffOffset + leadLagIndex,
                      -1.0 / Tb - stateData.cj);
    matrixData.assign(diffOffset + leadLagIndex,
                      diffOffset + exciterIndex,
                      -feedbackGain * saturationSlope / Tb);
    matrixData.assign(diffOffset + leadLagIndex, diffOffset + washoutIndex, feedbackGain / Tb);
    matrixData.assignCheckCol(diffOffset + leadLagIndex,
                              inputLocs[exciterVsetInLocation],
                              1.0 / Tb);
    matrixData.assignCheckCol(diffOffset + leadLagIndex, inputLocs[exciterVssInLocation], 1.0 / Tb);
    matrixData.assignCheckCol(diffOffset + leadLagIndex,
                              inputLocs[exciterXadIfdInLocation],
                              -feedbackGain * Kd / Tb);

    if (frozen) {
        matrixData.assign(diffOffset + regulatorIndex, diffOffset + regulatorIndex, -stateData.cj);
    } else {
        if (hasVoltageTransducer) {
            matrixData.assign(diffOffset + regulatorIndex,
                              diffOffset + voltageMeasurementState,
                              -Ka * leadRatio / Ta);
        } else {
            matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                      inputLocs[exciterVoltageInLocation],
                                      -Ka * leadRatio / Ta);
        }
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + leadLagIndex,
                          Ka * (1.0 - leadRatio) / Ta);
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + regulatorIndex,
                          -1.0 / Ta - stateData.cj);
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + exciterIndex,
                          -Ka * leadRatio * feedbackGain * saturationSlope / Ta);
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + washoutIndex,
                          Ka * leadRatio * feedbackGain / Ta);
        matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                  inputLocs[exciterVsetInLocation],
                                  Ka * leadRatio / Ta);
        matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                  inputLocs[exciterVssInLocation],
                                  Ka * leadRatio / Ta);
        matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                  inputLocs[exciterXadIfdInLocation],
                                  -Ka * leadRatio * feedbackGain * Kd / Ta);
    }

    double regulatorGain = 0.0;
    double targetExciterGain = 0.0;
    double targetCurrentGain = 0.0;
    regulatorTargetDerivatives(inputs, state, regulatorGain, targetExciterGain, targetCurrentGain);
    matrixData.assign(diffOffset + exciterIndex, diffOffset + regulatorIndex, regulatorGain / Te);
    matrixData.assign(diffOffset + exciterIndex,
                      diffOffset + exciterIndex,
                      (targetExciterGain - saturationSlope) / Te - stateData.cj);
    matrixData.assignCheckCol(diffOffset + exciterIndex,
                              inputLocs[exciterXadIfdInLocation],
                              (targetCurrentGain - Kd) / Te);
    matrixData.assign(diffOffset + washoutIndex, diffOffset + exciterIndex, saturationSlope / Tf);
    matrixData.assign(diffOffset + washoutIndex,
                      diffOffset + washoutIndex,
                      -1.0 / Tf - stateData.cj);
    matrixData.assignCheckCol(diffOffset + washoutIndex,
                              inputLocs[exciterXadIfdInLocation],
                              Kd / Tf);
}

void ExciterEXAC1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    double* state = m_state.data() + 1;
    const double* derivatives = m_dstate_dt.data() + 1;
    for (index_t index = 0; index < offsets.local().local.diffSize; ++index) {
        state[index] += timeStep * derivatives[index];
    }
    m_state[0] = fieldVoltage(inputs, state);
    updateLimitFlags(state);
    prevTime = time;
}

void ExciterEXAC1::rootTest(const IOdata& /*inputs*/,
                            const StateData& stateData,
                            double roots[],
                            const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double regulator =
        locations.diffStateLoc[stateIndex(regulatorState, Tr > 0.0)];
    roots[rootOffset] =
        std::min(regulatorUpperLimit() - regulator, regulator - regulatorLowerLimit());
}

void ExciterEXAC1::rootTrigger(CoreTime /*time*/,
                               const IOdata& /*inputs*/,
                               const std::vector<int>& rootMask,
                               const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] != 0) && updateLimitFlags(m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterEXAC1::rootCheck(const IOdata& /*inputs*/,
                                   const StateData& /*stateData*/,
                                   const SolverMode& /*sMode*/,
                                   CheckLevel /*level*/)
{
    if (updateLimitFlags(m_state.data() + 1)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

void ExciterEXAC1::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterEXAC1::set(std::string_view param, double val, units::unit unitType)
{
    const auto positive = [val](const char* label) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue(std::string("EXAC1 ") + label +
                                        " must be positive and finite");
        }
    };
    const auto nonnegative = [val](const char* label) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue(std::string("EXAC1 ") + label +
                                        " must be nonnegative and finite");
        }
    };
    const auto finite = [val](const char* label) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue(std::string("EXAC1 ") + label + " must be finite");
        }
    };
    if (param == "tr") {
        nonnegative("TR");
        Tr = val;
    } else if (param == "tb") {
        positive("TB");
        Tb = val;
    } else if (param == "tc") {
        finite("TC");
        Tc = val;
    } else if (param == "te") {
        positive("TE");
        Te = val;
    } else if (param == "tf") {
        positive("TF");
        Tf = val;
    } else if (param == "kf") {
        finite("KF");
        Kf = val;
    } else if (param == "kc") {
        finite("KC");
        Kc = val;
    } else if (param == "kd") {
        finite("KD");
        Kd = val;
    } else if (param == "ke") {
        finite("KE");
        Ke = val;
    } else if (param == "e1") {
        finite("E1");
        E1 = val;
    } else if (param == "se1") {
        finite("SE1");
        Se1 = val;
    } else if (param == "e2") {
        finite("E2");
        E2 = val;
    } else if (param == "se2") {
        finite("SE2");
        Se2 = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterEXAC1::get(std::string_view param, units::unit unitType) const
{
    if (param == "ka") {
        return Ka;
    }
    if (param == "ta") {
        return Ta;
    }
    if ((param == "vrmax") || (param == "urmax")) {
        return Vrmax;
    }
    if ((param == "vrmin") || (param == "urmin")) {
        return Vrmin;
    }
    if (param == "tr") {
        return Tr;
    }
    if (param == "tb") {
        return Tb;
    }
    if (param == "tc") {
        return Tc;
    }
    if (param == "te") {
        return Te;
    }
    if (param == "tf") {
        return Tf;
    }
    if (param == "kf") {
        return Kf;
    }
    if (param == "kc") {
        return Kc;
    }
    if (param == "kd") {
        return Kd;
    }
    if (param == "ke") {
        return Ke;
    }
    if (param == "e1") {
        return E1;
    }
    if (param == "se1") {
        return Se1;
    }
    if (param == "e2") {
        return E2;
    }
    if (param == "se2") {
        return Se2;
    }
    return Exciter::get(param, unitType);
}

stringVec ExciterEXAC1::localStateNames() const
{
    return (Tr > 0.0) ? stringVec{"efd", "vmeas", "ll", "va", "ve", "wf"} :
                        stringVec{"efd", "ll", "va", "ve", "wf"};
}

index_t ExciterEXAC1::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const index_t offset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") {
        return (Tr > 0.0) ? offset + voltageMeasurementState : kInvalidLocation;
    }
    if ((field == "ll") || (field == "leadlag")) {
        return offset + stateIndex(leadLagState, Tr > 0.0);
    }
    if ((field == "va") || (field == "regulator")) {
        return offset + stateIndex(regulatorState, Tr > 0.0);
    }
    if ((field == "ve") || (field == "exciter")) {
        return offset + stateIndex(exciterState, Tr > 0.0);
    }
    if ((field == "wf") || (field == "washout")) {
        return offset + stateIndex(washoutState, Tr > 0.0);
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
