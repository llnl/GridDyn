/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterEXAC1.h"

#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
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
    index_t
        stateIndex(index_t fullIndex, bool hasVoltageTransducer, bool hasLeadLag, bool hasRegulator)
    {
        index_t index = fullIndex;
        if (!hasVoltageTransducer && (fullIndex > voltageMeasurementState)) {
            --index;
        }
        if (!hasLeadLag && (fullIndex > leadLagState)) {
            --index;
        }
        if (!hasRegulator && (fullIndex > regulatorState)) {
            --index;
        }
        return index;
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
    clone->leadLag = leadLag;
    clone->rootTransitionPending = rootTransitionPending;
    clone->rootTransitionLimited = rootTransitionLimited;
    clone->rootTransitionHigh = rootTransitionHigh;
    clone->rootRearmPending = rootRearmPending;
    clone->rootRearmTime = rootRearmTime;
    return clone;
}

bool ExciterEXAC1::hasLeadLag() const
{
    return Tb > 0.0;
}

bool ExciterEXAC1::hasDynamicRegulator() const
{
    return Ta > 0.0;
}

void ExciterEXAC1::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(Tr) || !std::isfinite(Tb) || !std::isfinite(Tc) || !std::isfinite(Ka) ||
        !std::isfinite(Ta) || !std::isfinite(Vrmax) || !std::isfinite(Vrmin) ||
        !std::isfinite(Te) || !std::isfinite(Kf) || !std::isfinite(Tf) || !std::isfinite(Kc) ||
        !std::isfinite(Kd) || !std::isfinite(Ke) || !std::isfinite(E1) || !std::isfinite(Se1) ||
        !std::isfinite(E2) || !std::isfinite(Se2) || (Tr < 0.0) || (Tb < 0.0) || (Ta < 0.0) ||
        (Te <= 0.0) || (Tf <= 0.0) || (Ka <= 0.0) || (Vrmax < Vrmin) ||
        (regulatorUpperLimit() < regulatorLowerLimit())) {
        throw InvalidParameterValue("EXAC1 gains, time constants, or limits");
    }
    if (!hasLeadLag() && (Tc != 0.0)) {
        throw InvalidParameterValue("EXAC1 requires TC to be zero when TB is zero");
    }
    if (hasLeadLag()) {
        leadLag.setParameters(Tc, Tb);
    }
    rootTransitionPending = false;
    rootTransitionLimited = false;
    rootTransitionHigh = false;
    rootRearmPending = false;
    rootRearmTime = 0.0;
    // ANDES disables ExcQuadSat when SE2 is zero.  Saturation::QUADRATIC
    // intentionally does not infer that convention, because its generic
    // two-point fit permits no zero denominator.
    if (Se2 == 0.0) {
        saturation.setType(utilities::Saturation::SaturationType::NONE);
    } else {
        if ((E1 <= 0.0) || (E2 <= 0.0) || (Se1 < 0.0) || (Se2 <= 0.0)) {
            throw InvalidParameterValue("EXAC1 enabled saturation points");
        }
        if (Se1 == 0.0) {
            if (E2 <= E1) {
                throw InvalidParameterValue(
                    "EXAC1 zero-first saturation requires E2 greater than E1");
            }
            // A zero first saturation point is a cutoff curve: saturation
            // remains zero through E1 and rises to the second specified point.
            saturation.setType(utilities::Saturation::SaturationType::CUTOFF_QUADRATIC);
        } else {
            saturation.setType(utilities::Saturation::SaturationType::QUADRATIC);
        }
        saturation.setParam(E1, E1 * Se1, E2, E2 * Se2);
    }
    const bool hasVoltageTransducer = (Tr > 0.0);
    const bool leadLagEnabled = hasLeadLag();
    const bool dynamicRegulator = hasDynamicRegulator();
    offsets.local().local.algSize = dynamicRegulator ? 1 : 2;
    offsets.local().local.diffSize =
        (hasVoltageTransducer ? 1 : 0) + (leadLagEnabled ? 1 : 0) + (dynamicRegulator ? 3 : 2);
    offsets.local().local.algRoots = dynamicRegulator ? 1 : 0;
    offsets.local().local.jacSize = dynamicRegulator ? 34 : 40;
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
        const auto fex = detail::computeRectifierFactor(current);
        const double mismatch = exciterVoltage * fex.factor - fieldVoltage;
        const double slope = fex.factor - fex.derivative * current;
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
    if (std::abs(exciterVoltage * detail::computeRectifierFactor(finalCurrent).factor -
                 fieldVoltage) > 1e-7) {
        throw InvalidParameterValue(
            "EXAC1 initial field voltage is incompatible with rectifier loading");
    }
    const bool hasVoltageTransducer = (Tr > 0.0);
    const bool leadLagEnabled = hasLeadLag();
    const bool dynamicRegulator = hasDynamicRegulator();
    const index_t algebraicStateCount = dynamicRegulator ? 1 : 2;
    double* state = m_state.data() + algebraicStateCount;
    if (hasVoltageTransducer) {
        state[voltageMeasurementState] = voltage;
    }
    const auto regulatorIndex =
        stateIndex(regulatorState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const auto exciterIndex =
        stateIndex(exciterState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const auto washoutIndex =
        stateIndex(washoutState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    state[exciterIndex] = exciterVoltage;
    state[washoutIndex] = vfe(inputs, state);
    const double initialRegulator = initialRegulatorState(state[washoutIndex]);
    if (initialRegulator < regulatorLowerLimit() - limitTolerance) {
        throw InvalidParameterValue("EXAC1 initial regulator output below lower limit");
    }
    if (!adjustRegulatorInitialUpperLimit(initialRegulator)) {
        throw InvalidParameterValue("EXAC1 initial regulator output outside upper limit");
    }
    if (dynamicRegulator) {
        state[regulatorIndex] = initialRegulator;
    } else {
        m_state[1] = std::clamp(initialRegulator,
                                static_cast<double>(regulatorLowerLimit()),
                                static_cast<double>(regulatorUpperLimit()));
    }
    if (leadLagEnabled) {
        state[stateIndex(leadLagState, hasVoltageTransducer, true, true)] = initialRegulator / Ka;
    }
    m_state[0] = fieldVoltage;
    const double setpointInput = inputs[exciterVsetInLocation] - 1.0;
    vBias = voltage + referenceOffset(state[washoutIndex]) - Vref - setpointInput -
        inputs[exciterVssInLocation];
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state, dynamicRegulator ? initialRegulator : m_state[1]);
}

bool ExciterEXAC1::adjustRegulatorInitialUpperLimit(double initialValue)
{
    return adjustInitialUpperLimit(initialValue, Vrmax, "EXAC1 initial regulator output");
}

double ExciterEXAC1::referenceInput(const IOdata& inputs) const
{
    return Vref + vBias + inputs[exciterVsetInLocation] - 1.0 + inputs[exciterVssInLocation];
}

double ExciterEXAC1::vfe(const IOdata& inputs, const double state[]) const
{
    const auto exciterIndex =
        stateIndex(exciterState, Tr > 0.0, hasLeadLag(), hasDynamicRegulator());
    const double fieldFeedback = Ke * state[exciterIndex] + saturation(state[exciterIndex]);
    return (Kd == 0.0) ? fieldFeedback : fieldFeedback + Kd * inputs[exciterXadIfdInLocation];
}

double ExciterEXAC1::rectifierFactor(const IOdata& inputs, double exciterVoltage) const
{
    if (Kc == 0.0) {
        return 1.0;
    }
    return detail::computeRectifierFactor((exciterVoltage != 0.0) ? Kc *
                                                  inputs[exciterXadIfdInLocation] / exciterVoltage :
                                                                    0.0)
        .factor;
}

double ExciterEXAC1::fieldVoltage(const IOdata& inputs, const double state[]) const
{
    const auto exciterIndex =
        stateIndex(exciterState, Tr > 0.0, hasLeadLag(), hasDynamicRegulator());
    return state[exciterIndex] * rectifierFactor(inputs, state[exciterIndex]);
}

double ExciterEXAC1::regulatorDrive(const IOdata& inputs, const double state[]) const
{
    const bool hasVoltageTransducer = (Tr > 0.0);
    const bool leadLagEnabled = hasLeadLag();
    const auto washoutIndex =
        stateIndex(washoutState, hasVoltageTransducer, leadLagEnabled, hasDynamicRegulator());
    const double measuredVoltage =
        hasVoltageTransducer ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
    const double fieldFeedback = vfe(inputs, state);
    const double input =
        referenceInput(inputs) - measuredVoltage - Kf * (fieldFeedback - state[washoutIndex]) / Tf;
    const double leadOutput = leadLagEnabled ?
        leadLag.output(input, state[stateIndex(leadLagState, hasVoltageTransducer, true, true)]) :
        input;
    return Ka * leadOutput;
}

void ExciterEXAC1::stateWithRegulator(const double state[],
                                      double regulatorValue,
                                      double augmentedState[]) const
{
    const bool hasVoltageTransducer = (Tr > 0.0);
    const bool leadLagEnabled = hasLeadLag();
    const index_t regulatorIndex =
        stateIndex(regulatorState, hasVoltageTransducer, leadLagEnabled, true);
    const index_t stateSize = (hasVoltageTransducer ? 1 : 0) + (leadLagEnabled ? 1 : 0) + 2;
    for (index_t index = 0; index < regulatorIndex; ++index) {
        augmentedState[index] = state[index];
    }
    augmentedState[regulatorIndex] = regulatorValue;
    for (index_t index = regulatorIndex; index < (stateSize - 1); ++index) {
        augmentedState[index + 1] = state[index];
    }
}

double ExciterEXAC1::staticRegulatorTarget(const IOdata& inputs, const double state[]) const
{
    return std::clamp(regulatorDrive(inputs, state),
                      static_cast<double>(regulatorLowerLimit()),
                      static_cast<double>(regulatorUpperLimit()));
}

double ExciterEXAC1::regulatorTargetValue(const IOdata& inputs,
                                          const double state[],
                                          double regulatorValue) const
{
    if (hasDynamicRegulator()) {
        return regulatorTarget(inputs, state);
    }
    std::array<double, 5> augmentedState{};
    stateWithRegulator(state, regulatorValue, augmentedState.data());
    return regulatorTarget(inputs, augmentedState.data());
}

double ExciterEXAC1::regulatorTarget(const IOdata& /*inputs*/, const double state[]) const
{
    return state[stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true)];
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

int ExciterEXAC1::regulatorLimitStatus(const double state[], double regulatorValue) const
{
    const double regulator = hasDynamicRegulator() ?
        state[stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true)] :
        regulatorValue;
    if (regulator >= regulatorUpperLimit()) {
        return 1;
    }
    return (regulator <= regulatorLowerLimit()) ? -1 : 0;
}

bool ExciterEXAC1::updateLimitFlags(const IOdata& inputs, double state[], double regulatorValue)
{
    const int status = regulatorLimitStatus(state, regulatorValue);
    const bool wasLimited = opFlags[REGULATOR_LIMITED];
    bool limited = status != 0;
    bool high = status > 0;
    const bool dynamicRegulator = hasDynamicRegulator();
    const auto regulatorIndex = stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true);
    if (wasLimited) {
        // A held regulator state can remain exactly on its bound after IDA
        // returns from a root. Release from the unconstrained drive, rather
        // than from state distance, so an equilibrium at the bound is not
        // reported as a new root forever.
        const double drive = regulatorDrive(inputs, state) - regulatorValue;
        const bool wasHigh = opFlags[REGULATOR_LIMIT_HIGH];
        const bool release = wasHigh ? (drive <= -limitTolerance) : (drive >= limitTolerance);
        if (release) {
            limited = false;
            high = false;
            if (dynamicRegulator) {
                // Move just inside the released side of the limit.  The root
                // is intentionally offset by limitTolerance, so leaving the
                // state at the boundary would let the next root check
                // immediately re-engage the limiter.
                state[regulatorIndex] = wasHigh ? regulatorUpperLimit() - limitTolerance :
                                                  regulatorLowerLimit() + limitTolerance;
            }
        } else {
            limited = true;
            high = wasHigh;
        }
    } else if (status != 0) {
        // If an unbounded state is exactly on (or just beyond) a limit and
        // its unconstrained drive points inward, this is the release side of
        // the boundary, not a new limit entry.  This also keeps rootCheck()
        // from re-latching a limiter before IDA has advanced the state.
        const double drive = regulatorDrive(inputs, state) - regulatorValue;
        const bool inward = (status > 0) ? (drive <= -limitTolerance) : (drive >= limitTolerance);
        if (inward) {
            limited = false;
            high = false;
            if (dynamicRegulator) {
                state[regulatorIndex] = (status > 0) ? regulatorUpperLimit() - limitTolerance :
                                                       regulatorLowerLimit() + limitTolerance;
            }
        } else if (dynamicRegulator) {
            // A root return can leave the state a tolerance outside the
            // geometric limit. Project it to the boundary before holding it.
            state[regulatorIndex] = std::clamp(state[regulatorIndex],
                                               static_cast<double>(regulatorLowerLimit()),
                                               static_cast<double>(regulatorUpperLimit()));
        }
    } else if (dynamicRegulator) {
        // The solver state can restart exactly on a geometric bound while the
        // object-side state has already been nudged just inside it.  If the
        // unconstrained drive still points outward, this is a real re-entry,
        // not a release.  Recognize it before rootCheck() preserves a pending
        // release branch, otherwise IDA immediately rediscovers the entry
        // root at the same state.
        const double drive = regulatorDrive(inputs, state) - regulatorValue;
        if ((state[regulatorIndex] >= regulatorUpperLimit() - limitTolerance) &&
            (drive > limitTolerance)) {
            limited = true;
            high = true;
        } else if ((state[regulatorIndex] <= regulatorLowerLimit() + limitTolerance) &&
                   (drive < -limitTolerance)) {
            limited = true;
            high = false;
        }
    }
    const bool changed =
        (opFlags[REGULATOR_LIMITED] != limited) || (opFlags[REGULATOR_LIMIT_HIGH] != high);
    opFlags.set(REGULATOR_LIMITED, limited);
    opFlags.set(REGULATOR_LIMIT_HIGH, high);
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
        if (!hasDynamicRegulator()) {
            locations.destLoc[1] =
                staticRegulatorTarget(inputs, locations.diffStateLoc) - locations.algStateLoc[1];
        }
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
    const bool leadLagEnabled = hasLeadLag();
    const bool dynamicRegulator = hasDynamicRegulator();
    const auto regulatorIndex =
        stateIndex(regulatorState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const auto exciterIndex =
        stateIndex(exciterState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const auto washoutIndex =
        stateIndex(washoutState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const double measuredVoltage =
        hasVoltageTransducer ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
    const double fieldFeedback = vfe(inputs, state);
    const double input =
        referenceInput(inputs) - measuredVoltage - Kf * (fieldFeedback - state[washoutIndex]) / Tf;
    const double leadOutput = leadLagEnabled ?
        leadLag.output(input, state[stateIndex(leadLagState, hasVoltageTransducer, true, true)]) :
        input;
    const double regulatorValue =
        dynamicRegulator ? state[regulatorIndex] : locations.algStateLoc[1];
    const double regulatorDerivative =
        dynamicRegulator ? (Ka * leadOutput - regulatorValue) / Ta : 0.0;
    const int status = regulatorLimitStatus(state, regulatorValue);
    if (hasVoltageTransducer) {
        derivativeValues[voltageMeasurementState] =
            (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    }
    if (leadLagEnabled) {
        const auto leadLagIndex = stateIndex(leadLagState, hasVoltageTransducer, true, true);
        derivativeValues[leadLagIndex] = leadLag.derivative(input, state[leadLagIndex]);
    }
    if (dynamicRegulator) {
        derivativeValues[regulatorIndex] = ((status > 0) && (regulatorDerivative > 0.0)) ||
                ((status < 0) && (regulatorDerivative < 0.0)) ?
            0.0 :
            regulatorDerivative;
    }
    derivativeValues[exciterIndex] =
        (regulatorTargetValue(inputs, state, regulatorValue) - fieldFeedback) / Te;
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
    const bool leadLagEnabled = hasLeadLag();
    const bool dynamicRegulator = hasDynamicRegulator();
    const auto leadLagIndex = stateIndex(leadLagState, hasVoltageTransducer, true, true);
    const auto regulatorIndex =
        stateIndex(regulatorState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const auto exciterIndex =
        stateIndex(exciterState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const auto washoutIndex =
        stateIndex(washoutState, hasVoltageTransducer, leadLagEnabled, dynamicRegulator);
    const double exciterVoltage = state[exciterIndex];
    const double normalizedCurrent =
        (exciterVoltage != 0.0) ? Kc * inputs[exciterXadIfdInLocation] / exciterVoltage : 0.0;
    const auto fex = detail::computeRectifierFactor(normalizedCurrent);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(algOffset, algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(algOffset,
                              diffOffset + exciterIndex,
                              fex.factor - fex.derivative * normalizedCurrent);
        }
        matrixData.assignCheckCol(algOffset,
                                  inputLocs[exciterXadIfdInLocation],
                                  fex.derivative * Kc);
        if (!dynamicRegulator) {
            matrixData.assign(algOffset + 1, algOffset + 1, -1.0);
            const double saturationSlope = Ke + saturation.deriv(exciterVoltage);
            const double feedbackGain = Kf / Tf;
            const double measuredVoltage = hasVoltageTransducer ? state[voltageMeasurementState] :
                                                                  inputs[exciterVoltageInLocation];
            const double input = referenceInput(inputs) - measuredVoltage -
                feedbackGain * (vfe(inputs, state) - state[washoutIndex]);
            const double leadInputGain = leadLagEnabled ? leadLag.outputInputJacobian() : 1.0;
            const double leadStateGain = leadLagEnabled ? leadLag.outputStateJacobian() : 0.0;
            const double leadOutput =
                leadLagEnabled ? leadLag.output(input, state[leadLagIndex]) : input;
            const double unlimitedRegulator = Ka * leadOutput;
            const bool regulatorLimitActive = (unlimitedRegulator <= regulatorLowerLimit()) ||
                (unlimitedRegulator >= regulatorUpperLimit());
            if (!regulatorLimitActive) {
                if (!isAlgebraicOnly(sMode)) {
                    if (leadLagEnabled) {
                        matrixData.assign(algOffset + 1,
                                          diffOffset + leadLagIndex,
                                          Ka * leadStateGain);
                    }
                    if (hasVoltageTransducer) {
                        matrixData.assign(algOffset + 1,
                                          diffOffset + voltageMeasurementState,
                                          -Ka * leadInputGain);
                    }
                    matrixData.assign(algOffset + 1,
                                      diffOffset + exciterIndex,
                                      -Ka * leadInputGain * feedbackGain * saturationSlope);
                    matrixData.assign(algOffset + 1,
                                      diffOffset + washoutIndex,
                                      Ka * leadInputGain * feedbackGain);
                }
                if (!hasVoltageTransducer) {
                    matrixData.assignCheckCol(algOffset + 1,
                                              inputLocs[exciterVoltageInLocation],
                                              -Ka * leadInputGain);
                }
                matrixData.assignCheckCol(algOffset + 1,
                                          inputLocs[exciterVsetInLocation],
                                          Ka * leadInputGain);
                matrixData.assignCheckCol(algOffset + 1,
                                          inputLocs[exciterVssInLocation],
                                          Ka * leadInputGain);
                matrixData.assignCheckCol(algOffset + 1,
                                          inputLocs[exciterXadIfdInLocation],
                                          -Ka * leadInputGain * feedbackGain * Kd);
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double saturationSlope = Ke + saturation.deriv(exciterVoltage);
    const double feedbackGain = Kf / Tf;
    const double measuredVoltage =
        hasVoltageTransducer ? state[voltageMeasurementState] : inputs[exciterVoltageInLocation];
    const double input = referenceInput(inputs) - measuredVoltage -
        feedbackGain * (vfe(inputs, state) - state[washoutIndex]);
    const double leadInputGain = leadLagEnabled ? leadLag.outputInputJacobian() : 1.0;
    const double leadStateGain = leadLagEnabled ? leadLag.outputStateJacobian() : 0.0;
    const double leadOutput = leadLagEnabled ? leadLag.output(input, state[leadLagIndex]) : input;
    const double regulatorValue =
        dynamicRegulator ? state[regulatorIndex] : locations.algStateLoc[1];
    const double regulatorDerivative =
        dynamicRegulator ? (Ka * leadOutput - regulatorValue) / Ta : 0.0;
    const int status = regulatorLimitStatus(state, regulatorValue);
    const bool frozen = ((status > 0) && (regulatorDerivative > 0.0)) ||
        ((status < 0) && (regulatorDerivative < 0.0));

    if (hasVoltageTransducer) {
        matrixData.assign(diffOffset + voltageMeasurementState,
                          diffOffset + voltageMeasurementState,
                          -1.0 / Tr - stateData.cj);
        matrixData.assignCheckCol(diffOffset + voltageMeasurementState,
                                  inputLocs[exciterVoltageInLocation],
                                  1.0 / Tr);
    }
    if (leadLagEnabled) {
        const double leadLagInputGain = leadLag.derivativeInputJacobian();
        if (hasVoltageTransducer) {
            matrixData.assign(diffOffset + leadLagIndex,
                              diffOffset + voltageMeasurementState,
                              -leadLagInputGain);
        } else {
            matrixData.assignCheckCol(diffOffset + leadLagIndex,
                                      inputLocs[exciterVoltageInLocation],
                                      -leadLagInputGain);
        }
        matrixData.assign(diffOffset + leadLagIndex,
                          diffOffset + leadLagIndex,
                          leadLag.derivativeStateJacobian() - stateData.cj);
        matrixData.assign(diffOffset + leadLagIndex,
                          diffOffset + exciterIndex,
                          -feedbackGain * saturationSlope * leadLagInputGain);
        matrixData.assign(diffOffset + leadLagIndex,
                          diffOffset + washoutIndex,
                          feedbackGain * leadLagInputGain);
        matrixData.assignCheckCol(diffOffset + leadLagIndex,
                                  inputLocs[exciterVsetInLocation],
                                  leadLagInputGain);
        matrixData.assignCheckCol(diffOffset + leadLagIndex,
                                  inputLocs[exciterVssInLocation],
                                  leadLagInputGain);
        matrixData.assignCheckCol(diffOffset + leadLagIndex,
                                  inputLocs[exciterXadIfdInLocation],
                                  -feedbackGain * Kd * leadLagInputGain);
    }

    if (dynamicRegulator && frozen) {
        matrixData.assign(diffOffset + regulatorIndex, diffOffset + regulatorIndex, -stateData.cj);
    } else if (dynamicRegulator) {
        if (hasVoltageTransducer) {
            matrixData.assign(diffOffset + regulatorIndex,
                              diffOffset + voltageMeasurementState,
                              -Ka * leadInputGain / Ta);
        } else {
            matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                      inputLocs[exciterVoltageInLocation],
                                      -Ka * leadInputGain / Ta);
        }
        if (leadLagEnabled) {
            matrixData.assign(diffOffset + regulatorIndex,
                              diffOffset + leadLagIndex,
                              Ka * leadStateGain / Ta);
        }
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + regulatorIndex,
                          -1.0 / Ta - stateData.cj);
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + exciterIndex,
                          -Ka * leadInputGain * feedbackGain * saturationSlope / Ta);
        matrixData.assign(diffOffset + regulatorIndex,
                          diffOffset + washoutIndex,
                          Ka * leadInputGain * feedbackGain / Ta);
        matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                  inputLocs[exciterVsetInLocation],
                                  Ka * leadInputGain / Ta);
        matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                  inputLocs[exciterVssInLocation],
                                  Ka * leadInputGain / Ta);
        matrixData.assignCheckCol(diffOffset + regulatorIndex,
                                  inputLocs[exciterXadIfdInLocation],
                                  -Ka * leadInputGain * feedbackGain * Kd / Ta);
    }

    double regulatorGain = 0.0;
    double targetExciterGain = 0.0;
    double targetCurrentGain = 0.0;
    std::array<double, 5> augmentedState{};
    const double* targetState = state;
    if (!dynamicRegulator) {
        stateWithRegulator(state, regulatorValue, augmentedState.data());
        targetState = augmentedState.data();
    }
    regulatorTargetDerivatives(
        inputs, targetState, regulatorGain, targetExciterGain, targetCurrentGain);
    const index_t regulatorLocation =
        dynamicRegulator ? diffOffset + regulatorIndex : algOffset + 1;
    matrixData.assign(diffOffset + exciterIndex, regulatorLocation, regulatorGain / Te);
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
    const bool dynamicRegulator = hasDynamicRegulator();
    const index_t algebraicStateCount = dynamicRegulator ? 1 : 2;
    double* state = m_state.data() + algebraicStateCount;
    const double* derivatives = m_dstate_dt.data() + algebraicStateCount;
    for (index_t index = 0; index < offsets.local().local.diffSize; ++index) {
        state[index] += timeStep * derivatives[index];
    }
    if (!dynamicRegulator) {
        m_state[1] = staticRegulatorTarget(inputs, state);
    }
    m_state[0] = fieldVoltage(inputs, state);
    updateLimitFlags(inputs,
                     state,
                     dynamicRegulator ?
                         state[stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true)] :
                         m_state[1]);
    prevTime = time;
}

void ExciterEXAC1::rootTest(const IOdata& inputs,
                            const StateData& stateData,
                            double roots[],
                            const SolverMode& sMode)
{
    if (!hasDynamicRegulator()) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double* state = locations.diffStateLoc;
    const double regulator = state[stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true)];
    if (rootRearmPending) {
        if (stateData.time <= rootRearmTime) {
            // IDA retains the terminal root in its root history when it
            // restarts after a Jacobian/branch update.  A no-op limiter root
            // is not a new physical transition, so give IDA a positive
            // one-sided value until it has taken a forward-time step.
            roots[rootOffset] = limitTolerance;
            return;
        }
        rootRearmPending = false;
    }
    if (opFlags[REGULATOR_LIMITED]) {
        const double drive = regulatorDrive(inputs, state) - regulator;
        // Once limited, the root is the release condition. Offset it into
        // the held region so a zero drive at the limit is not rediscovered on
        // every solver restart.
        roots[rootOffset] =
            opFlags[REGULATOR_LIMIT_HIGH] ? drive + limitTolerance : drive - limitTolerance;
        return;
    }
    // An initialized regulator exactly at a limit is a valid equilibrium. Keep
    // the root off zero so IDA does not rediscover the same non-event on every
    // restart; rootCheck() still changes the limiter branch when the state
    // moves through the limit.
    roots[rootOffset] =
        std::min(regulatorUpperLimit() - regulator, regulator - regulatorLowerLimit()) +
        limitTolerance;
}

void ExciterEXAC1::rootTrigger(CoreTime time,
                               const IOdata& inputs,
                               const std::vector<int>& rootMask,
                               const SolverMode& sMode)
{
    if (!hasDynamicRegulator()) {
        return;
    }
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if (rootMask[rootOffset] != 0) {
        double* state = m_state.data() + 1;
        const auto regulatorIndex = stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true);
        const bool wasLimited = opFlags[REGULATOR_LIMITED];
        const bool wasHigh = opFlags[REGULATOR_LIMIT_HIGH];
        const double stateBefore = state[regulatorIndex];
        const double driveBefore = regulatorDrive(inputs, state) - stateBefore;
        const double midpoint = (regulatorUpperLimit() + regulatorLowerLimit()) / 2.0;
        bool limited;
        bool high;
        if (wasLimited) {
            // A release root is valid only when the current unconstrained
            // drive points back into the feasible region.  A stale/interpolated
            // solver input can otherwise make an entry root look like a
            // release, after which the state immediately crosses VAMAX/VAMIN
            // again and produces an endless root pair.
            const bool release =
                wasHigh ? (driveBefore <= -limitTolerance) : (driveBefore >= limitTolerance);
            limited = !release;
            high = limited ? wasHigh : false;
        } else {
            limited = true;
            high = state[regulatorIndex] >= midpoint;
        }
        rootTransitionPending = true;
        rootTransitionLimited = limited;
        rootTransitionHigh = high;
        opFlags.set(REGULATOR_LIMITED, limited);
        opFlags.set(REGULATOR_LIMIT_HIGH, high);
        if (limited) {
            state[regulatorIndex] = std::clamp(state[regulatorIndex],
                                               static_cast<double>(regulatorLowerLimit()),
                                               static_cast<double>(regulatorUpperLimit()));
        } else {
            state[regulatorIndex] = wasHigh ? regulatorUpperLimit() - limitTolerance :
                                              regulatorLowerLimit() + limitTolerance;
        }
        const bool changed = (wasLimited != limited) || (wasHigh != high);
        rootRearmPending = !changed;
        if (rootRearmPending) {
            rootRearmTime = time;
        }
        alert(this, JAC_COUNT_CHANGE);
        if (changed) {
            const StateData stateData(time, m_state.data(), m_dstate_dt.data());
            derivative(inputs, stateData, m_dstate_dt.data(), cLocalSolverMode);
        }
    }
}

ChangeCode ExciterEXAC1::rootCheck(const IOdata& inputs,
                                   const StateData& /*stateData*/,
                                   const SolverMode& /*sMode*/,
                                   CheckLevel /*level*/)
{
    double* state = m_state.data() + (hasDynamicRegulator() ? 1 : 2);
    const auto regulatorIndex = stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true);
    const bool wasLimited = opFlags[REGULATOR_LIMITED];
    const bool wasHigh = opFlags[REGULATOR_LIMIT_HIGH];
    const bool transitionPending = rootTransitionPending;
    const bool transitionLimited = rootTransitionLimited;
    const bool transitionHigh = rootTransitionHigh;
    rootTransitionPending = false;
    updateLimitFlags(inputs,
                     state,
                     hasDynamicRegulator() ? m_state[1 + regulatorIndex] : m_state[1]);
    if (transitionPending) {
        if (transitionLimited || !opFlags[REGULATOR_LIMITED]) {
            opFlags.set(REGULATOR_LIMITED, transitionLimited);
            opFlags.set(REGULATOR_LIMIT_HIGH, transitionHigh);
        }
    } else if (wasLimited) {
        opFlags.set(REGULATOR_LIMITED, true);
        opFlags.set(REGULATOR_LIMIT_HIGH, wasHigh);
    }
    const bool changed =
        (wasLimited != opFlags[REGULATOR_LIMITED]) || (wasHigh != opFlags[REGULATOR_LIMIT_HIGH]);
    if (changed) {
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
        nonnegative("TB");
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
    stringVec names{"efd"};
    if (!hasDynamicRegulator()) {
        names.emplace_back("va");
    }
    if (Tr > 0.0) {
        names.emplace_back("vmeas");
    }
    if (hasLeadLag()) {
        names.emplace_back("ll");
    }
    if (hasDynamicRegulator()) {
        names.emplace_back("va");
    }
    names.emplace_back("ve");
    names.emplace_back("wf");
    return names;
}

index_t ExciterEXAC1::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    if ((field == "va") || (field == "regulator")) {
        if (!hasDynamicRegulator()) {
            return offsets.getAlgOffset(sMode) + 1;
        }
    }
    const index_t offset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") {
        return (Tr > 0.0) ? offset + voltageMeasurementState : kInvalidLocation;
    }
    if ((field == "ll") || (field == "leadlag")) {
        return hasLeadLag() ? offset + stateIndex(leadLagState, Tr > 0.0, true, true) :
                              kInvalidLocation;
    }
    if ((field == "va") || (field == "regulator")) {
        return offset + stateIndex(regulatorState, Tr > 0.0, hasLeadLag(), true);
    }
    if ((field == "ve") || (field == "exciter")) {
        return offset + stateIndex(exciterState, Tr > 0.0, hasLeadLag(), hasDynamicRegulator());
    }
    if ((field == "wf") || (field == "washout")) {
        return offset + stateIndex(washoutState, Tr > 0.0, hasLeadLag(), hasDynamicRegulator());
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
