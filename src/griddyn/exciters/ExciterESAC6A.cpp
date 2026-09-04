/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterESAC6A.h"

#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::exciters {
// The expressions intentionally retain the grouping in the GridKit AC6A diagram.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t esac6aMaximumStates = 5;

    struct Signal {
        double value = 0.0;
        std::array<double, esac6aMaximumStates> state{};
        std::array<double, exciterInputCount> input{};
    };

    Signal constantSignal(double value)
    {
        Signal result;
        result.value = value;
        return result;
    }

    Signal stateSignal(double value, index_t index)
    {
        auto result = constantSignal(value);
        result.state[index] = 1.0;
        return result;
    }

    Signal inputSignal(const IOdata& inputs, index_t index)
    {
        auto result = constantSignal(inputs[index]);
        result.input[index] = 1.0;
        return result;
    }

    Signal addSignals(const Signal& left, const Signal& right)
    {
        Signal result;
        result.value = left.value + right.value;
        for (index_t index = 0; index < esac6aMaximumStates; ++index) {
            result.state[index] = left.state[index] + right.state[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.input[index] = left.input[index] + right.input[index];
        }
        return result;
    }

    Signal scale(const Signal& signal, double factor)
    {
        Signal result;
        result.value = factor * signal.value;
        for (index_t index = 0; index < esac6aMaximumStates; ++index) {
            result.state[index] = factor * signal.state[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.input[index] = factor * signal.input[index];
        }
        return result;
    }

    Signal subtract(const Signal& left, const Signal& right)
    {
        return addSignals(left, scale(right, -1.0));
    }

    Signal multiply(const Signal& left, const Signal& right)
    {
        Signal result;
        result.value = left.value * right.value;
        for (index_t index = 0; index < esac6aMaximumStates; ++index) {
            result.state[index] = left.state[index] * right.value + left.value * right.state[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.input[index] = left.input[index] * right.value + left.value * right.input[index];
        }
        return result;
    }

    Signal divide(const Signal& numerator, const Signal& denominator)
    {
        if (std::abs(denominator.value) <= 1e-12) {
            return constantSignal(0.0);
        }
        Signal result;
        result.value = numerator.value / denominator.value;
        const double denominatorSquared = denominator.value * denominator.value;
        for (index_t index = 0; index < esac6aMaximumStates; ++index) {
            result.state[index] = (numerator.state[index] * denominator.value -
                                   numerator.value * denominator.state[index]) /
                denominatorSquared;
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.input[index] = (numerator.input[index] * denominator.value -
                                   numerator.value * denominator.input[index]) /
                denominatorSquared;
        }
        return result;
    }

    Signal clamp(const Signal& signal, double lower, double upper)
    {
        if (signal.value <= lower) {
            return constantSignal(lower);
        }
        if (signal.value >= upper) {
            return constantSignal(upper);
        }
        return signal;
    }

    Signal clamp(const Signal& signal, const Signal& lower, const Signal& upper)
    {
        if (signal.value <= lower.value) {
            return lower;
        }
        if (signal.value >= upper.value) {
            return upper;
        }
        return signal;
    }

    Signal applyFunction(const Signal& signal, double value, double derivative)
    {
        Signal result;
        result.value = value;
        for (index_t index = 0; index < esac6aMaximumStates; ++index) {
            result.state[index] = derivative * signal.state[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.input[index] = derivative * signal.input[index];
        }
        return result;
    }

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

ExciterESAC6A::ExciterESAC6A(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 40.0;
    Ta = 0.1;
    Vrmax = 1.0;
    Vrmin = -1.0;
}

CoreObject* ExciterESAC6A::clone(CoreObject* obj) const
{
    auto* exciterClone = cloneBase<ExciterESAC6A, Exciter>(this, obj);
    if (exciterClone == nullptr) {
        return obj;
    }
    exciterClone->Tr = Tr;
    exciterClone->Tk = Tk;
    exciterClone->Tb = Tb;
    exciterClone->Tc = Tc;
    exciterClone->Vamax = Vamax;
    exciterClone->Vamin = Vamin;
    exciterClone->Te = Te;
    exciterClone->Vfelim = Vfelim;
    exciterClone->Kh = Kh;
    exciterClone->Vhmax = Vhmax;
    exciterClone->Th = Th;
    exciterClone->Tj = Tj;
    exciterClone->Kc = Kc;
    exciterClone->Kd = Kd;
    exciterClone->Ke = Ke;
    exciterClone->E1 = E1;
    exciterClone->Se1 = Se1;
    exciterClone->E2 = E2;
    exciterClone->Se2 = Se2;
    exciterClone->speedMultiplier = speedMultiplier;
    exciterClone->saturation = saturation;
    return exciterClone;
}

ExciterESAC6A::StateLayout ExciterESAC6A::stateLayout() const
{
    StateLayout layout;
    if (Tr > 0.0) {
        layout.sensedVoltage = layout.count++;
    }
    layout.regulator = layout.count++;
    if (Tb > 0.0) {
        layout.leadLag = layout.count++;
    }
    layout.exciter = layout.count++;
    if (Th > 0.0) {
        layout.feedback = layout.count++;
    }
    return layout;
}

void ExciterESAC6A::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 23> parameters{Tr,    Ka,    Ta, Tk,     Tb,  Tc,    Vamax, Vamin,
                                            Vrmax, Vrmin, Te, Vfelim, Kh,  Vhmax, Th,    Tj,
                                            Kc,    Kd,    Ke, E1,     Se1, E2,    Se2};
    const bool disabledSaturation = (Se1 == 0.0) && (Se2 == 0.0);
    const bool invalidSaturation =
        !disabledSaturation && ((E1 <= 0.0) || (E2 <= E1) || (Se1 <= 0.0) || (Se2 <= 0.0));
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Ka <= 0.0) || (Tr < 0.0) || (Ta <= 0.0) || (Tk < 0.0) || (Tb < 0.0) || (Tc < 0.0) ||
        (Te <= 0.0) || (Th < 0.0) || (Tj < 0.0) || (Vamin > Vamax) || (Vrmin > Vrmax) ||
        (Vhmax < 0.0) || ((Tb == 0.0) && (Tc != 0.0)) || ((Th == 0.0) && (Tj != 0.0)) ||
        invalidSaturation) {
        throw InvalidParameterValue("ESAC6A gains, time constants, limits, or saturation points");
    }

    saturation.setType(disabledSaturation ?
                           utilities::Saturation::SaturationType::NONE :
                           utilities::Saturation::SaturationType::CUTOFF_QUADRATIC);
    if (!disabledSaturation) {
        saturation.setParam(E1, Se1, E2, Se2);
        if (!std::isfinite(saturation(E1)) || !std::isfinite(saturation(E2)) ||
            (std::abs(saturation(E1) - Se1) > 1e-10) || (std::abs(saturation(E2) - Se2) > 1e-10)) {
            throw InvalidParameterValue("ESAC6A saturation fit");
        }
    }

    const auto layout = stateLayout();
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = layout.count;
    local.algRoots = 1;
    local.jacSize = 96;
    prevTime = time0;
}

ExciterESAC6A::ModelEvaluation ExciterESAC6A::evaluateModel(const IOdata& inputs,
                                                            const double state[]) const
{
    const auto layout = stateLayout();
    ModelEvaluation evaluation;
    std::array<Signal, maximumStates> rates{};

    const Signal exciterVoltage = stateSignal(state[layout.exciter], layout.exciter);
    const auto saturationData = saturation.evaluate(exciterVoltage.value);
    const Signal saturationSignal =
        applyFunction(exciterVoltage, saturationData.value, saturationData.derivative);
    Signal fieldFeedback =
        multiply(addSignals(constantSignal(Ke), saturationSignal), exciterVoltage);
    fieldFeedback =
        addSignals(fieldFeedback, scale(inputSignal(inputs, exciterXadIfdInLocation), Kd));

    const Signal measuredVoltage = (Tr > 0.0) ?
        stateSignal(state[layout.sensedVoltage], layout.sensedVoltage) :
        inputSignal(inputs, exciterVoltageInLocation);
    Signal reference =
        addSignals(constantSignal(Vref + vBias - 1.0), inputSignal(inputs, exciterVsetInLocation));
    reference = addSignals(reference, inputSignal(inputs, exciterVssInLocation));
    const Signal error = subtract(reference, measuredVoltage);

    if (Tr > 0.0) {
        rates[layout.sensedVoltage] =
            scale(subtract(inputSignal(inputs, exciterVoltageInLocation), measuredVoltage),
                  1.0 / Tr);
    }

    const Signal regulatorState = stateSignal(state[layout.regulator], layout.regulator);
    rates[layout.regulator] = scale(subtract(scale(error, Ka), regulatorState), 1.0 / Ta);
    const Signal regulatorOutput =
        clamp(addSignals(regulatorState, scale(rates[layout.regulator], Tk)), Vamin, Vamax);

    Signal leadLagOutput = regulatorOutput;
    if (Tb > 0.0) {
        const Signal leadLagState = stateSignal(state[layout.leadLag], layout.leadLag);
        rates[layout.leadLag] = scale(subtract(regulatorOutput, leadLagState), 1.0 / Tb);
        leadLagOutput = addSignals(leadLagState, scale(rates[layout.leadLag], Tc));
    }

    const Signal feedbackLimiterInput = scale(subtract(fieldFeedback, constantSignal(Vfelim)), Kh);
    const Signal limitedFeedback = clamp(feedbackLimiterInput, 0.0, Vhmax);
    Signal feedbackOutput = limitedFeedback;
    if (Th > 0.0) {
        const Signal feedbackState = stateSignal(state[layout.feedback], layout.feedback);
        rates[layout.feedback] = scale(subtract(limitedFeedback, feedbackState), 1.0 / Th);
        feedbackOutput = addSignals(feedbackState, scale(rates[layout.feedback], Tj));
    }
    const Signal terminalVoltage = inputSignal(inputs, exciterVoltageInLocation);
    const Signal voltageRegulator = clamp(subtract(leadLagOutput, feedbackOutput),
                                          scale(terminalVoltage, Vrmin),
                                          scale(terminalVoltage, Vrmax));
    const Signal rawExciterRate = scale(subtract(voltageRegulator, fieldFeedback), 1.0 / Te);
    evaluation.exciterLimitDrive = rawExciterRate.value;
    rates[layout.exciter] = ((exciterVoltage.value <= 0.0) && (rawExciterRate.value < 0.0)) ?
        constantSignal(0.0) :
        rawExciterRate;

    const Signal normalizedCurrent =
        divide(scale(inputSignal(inputs, exciterXadIfdInLocation), Kc), exciterVoltage);
    const auto rectifierData = detail::computeRectifierFactor(normalizedCurrent.value);
    const Signal rectifier =
        applyFunction(normalizedCurrent, rectifierData.factor, rectifierData.derivative);
    const Signal speed =
        speedMultiplier ? inputSignal(inputs, exciterOmegaInLocation) : constantSignal(1.0);
    const Signal fieldOutput = multiply(multiply(speed, rectifier), exciterVoltage);

    evaluation.fieldOutput = fieldOutput.value;
    evaluation.fieldStateDerivatives = fieldOutput.state;
    evaluation.fieldInputDerivatives = fieldOutput.input;
    for (index_t row = 0; row < layout.count; ++row) {
        evaluation.rates[row] = rates[row].value;
        evaluation.rateStateDerivatives[row] = rates[row].state;
        evaluation.rateInputDerivatives[row] = rates[row].input;
    }
    return evaluation;
}

void ExciterESAC6A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("ESAC6A initial voltage signals");
    }
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    if (((Kc != 0.0) || (Kd != 0.0)) && !finiteMachineSignal(fieldCurrent)) {
        throw InvalidParameterValue("ESAC6A requires a compatible field-current signal");
    }
    const double speed = speedMultiplier ? inputs[exciterOmegaInLocation] : 1.0;
    if (!std::isfinite(speed) || (std::abs(speed) <= 1e-12)) {
        throw InvalidParameterValue("ESAC6A initial speed multiplier");
    }

    const double fieldVoltage = desiredOutput[0];
    double exciterVoltage = std::max(0.01, std::abs(fieldVoltage / speed));
    for (int count = 0; count < 20; ++count) {
        const double normalizedCurrent = (Kc != 0.0) ? Kc * fieldCurrent / exciterVoltage : 0.0;
        const auto rectifier = detail::computeRectifierFactor(normalizedCurrent);
        const double mismatch = speed * rectifier.factor * exciterVoltage - fieldVoltage;
        const double slope = speed * (rectifier.factor - rectifier.derivative * normalizedCurrent);
        if (std::abs(slope) < 1e-12) {
            break;
        }
        exciterVoltage = std::max(1e-8, exciterVoltage - mismatch / slope);
        if (std::abs(mismatch) < 1e-12) {
            break;
        }
    }
    const double normalizedCurrent = (Kc != 0.0) ? Kc * fieldCurrent / exciterVoltage : 0.0;
    if (std::abs(speed * detail::computeRectifierFactor(normalizedCurrent).factor * exciterVoltage -
                 fieldVoltage) > 1e-7) {
        throw InvalidParameterValue(
            "ESAC6A initial field voltage is incompatible with rectifier loading");
    }

    if (exciterVoltage <= 0.0) {
        throw InvalidParameterValue("ESAC6A initial exciter voltage must be positive");
    }
    const double fieldFeedback =
        (Ke + saturation(exciterVoltage)) * exciterVoltage + Kd * fieldCurrent;
    const double feedbackLimiter =
        std::clamp(Kh * (fieldFeedback - Vfelim), 0.0, static_cast<double>(Vhmax));
    const double voltageRegulator = fieldFeedback;
    const double leadLagOutput = voltageRegulator + feedbackLimiter;
    const double regulatorLower = inputs[exciterVoltageInLocation] * Vrmin;
    const double regulatorUpper = inputs[exciterVoltageInLocation] * Vrmax;
    if ((leadLagOutput < Vamin - 1e-7) || (leadLagOutput > Vamax + 1e-7) ||
        (voltageRegulator < regulatorLower - 1e-7) || (voltageRegulator > regulatorUpper + 1e-7)) {
        throw InvalidParameterValue("ESAC6A initial regulator output outside limits");
    }

    const auto layout = stateLayout();
    double* state = m_state.data() + 1;
    if (Tr > 0.0) {
        state[layout.sensedVoltage] = inputs[exciterVoltageInLocation];
    }
    state[layout.regulator] = leadLagOutput;
    if (Tb > 0.0) {
        state[layout.leadLag] = leadLagOutput;
    }
    state[layout.exciter] = exciterVoltage;
    if (Th > 0.0) {
        state[layout.feedback] = feedbackLimiter;
    }
    m_state[0] = fieldVoltage;

    const double error = leadLagOutput / Ka;
    vBias = error + inputs[exciterVoltageInLocation] - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation];
    fieldSet.resize(2);
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);

    const auto initialized = evaluateModel(inputs, state);
    if (!std::isfinite(initialized.fieldOutput) ||
        (std::abs(initialized.fieldOutput - fieldVoltage) > 1e-7) ||
        std::any_of(initialized.rates.begin(),
                    initialized.rates.begin() + layout.count,
                    [](double value) {
                        return !std::isfinite(value) || (std::abs(value) > 1e-7);
                    })) {
        throw InvalidParameterValue("ESAC6A initial equations are inconsistent");
    }
    updateExciterLimitFlag(inputs, state);
}

void ExciterESAC6A::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const auto evaluation = evaluateModel(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] = evaluation.fieldOutput - locations.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < locations.diffSize; ++index) {
            locations.destDiffLoc[index] = evaluation.rates[index] - locations.dstateLoc[index];
        }
    }
}

void ExciterESAC6A::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const auto evaluation = evaluateModel(inputs, locations.diffStateLoc);
    std::copy_n(evaluation.rates.begin(), locations.diffSize, locations.destDiffLoc);
}

void ExciterESAC6A::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto locations = offsets.getLocations(stateData, update, sMode, this);
        locations.destLoc[0] = evaluateModel(inputs, locations.diffStateLoc).fieldOutput;
    }
}

void ExciterESAC6A::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluateModel(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(locations.algOffset, locations.algOffset, -1.0);
        for (index_t column = 0; column < locations.diffSize; ++column) {
            if (evaluation.fieldStateDerivatives[column] != 0.0) {
                matrixData.assign(locations.algOffset,
                                  locations.diffOffset + column,
                                  evaluation.fieldStateDerivatives[column]);
            }
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            if (evaluation.fieldInputDerivatives[input] != 0.0) {
                matrixData.assignCheckCol(locations.algOffset,
                                          inputLocs[input],
                                          evaluation.fieldInputDerivatives[input]);
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t row = 0; row < locations.diffSize; ++row) {
        for (index_t column = 0; column < locations.diffSize; ++column) {
            double value = evaluation.rateStateDerivatives[row][column];
            if (row == column) {
                value -= stateData.cj;
            }
            if (value != 0.0) {
                matrixData.assign(locations.diffOffset + row, locations.diffOffset + column, value);
            }
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            if (evaluation.rateInputDerivatives[row][input] != 0.0) {
                matrixData.assignCheckCol(locations.diffOffset + row,
                                          inputLocs[input],
                                          evaluation.rateInputDerivatives[row][input]);
            }
        }
    }
}

void ExciterESAC6A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const auto layout = stateLayout();
    for (index_t index = 0; index < layout.count; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    m_state[layout.exciter + 1] = std::max(0.0, m_state[layout.exciter + 1]);
    m_state[0] = evaluateModel(inputs, m_state.data() + 1).fieldOutput;
    updateExciterLimitFlag(inputs, m_state.data() + 1);
    prevTime = time;
}

bool ExciterESAC6A::updateExciterLimitFlag(const IOdata& inputs, const double state[])
{
    const auto layout = stateLayout();
    const bool limited =
        (state[layout.exciter] <= 0.0) && (evaluateModel(inputs, state).exciterLimitDrive <= 0.0);
    const bool changed = opFlags[EXCITER_AT_LOWER_LIMIT] != limited;
    opFlags.set(EXCITER_AT_LOWER_LIMIT, limited);
    return changed;
}

void ExciterESAC6A::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluateModel(inputs, locations.diffStateLoc);
    roots[offsets.getRootOffset(sMode)] = opFlags[EXCITER_AT_LOWER_LIMIT] ?
        -evaluation.exciterLimitDrive :
        locations.diffStateLoc[stateLayout().exciter];
}

void ExciterESAC6A::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if (rootMask[rootOffset] != 0) {
        if (opFlags[EXCITER_AT_LOWER_LIMIT]) {
            opFlags.reset(EXCITER_AT_LOWER_LIMIT);
            alert(this, JAC_COUNT_CHANGE);
        } else {
            double* state = m_state.data() + 1;
            const auto layout = stateLayout();
            state[layout.exciter] = std::max(0.0, state[layout.exciter]);
            if (updateExciterLimitFlag(inputs, state)) {
                alert(this, JAC_COUNT_CHANGE);
            }
        }
    }
}

ChangeCode ExciterESAC6A::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    double* state = m_state.data() + 1;
    const auto layout = stateLayout();
    state[layout.exciter] = std::max(0.0, state[layout.exciter]);
    if (updateExciterLimitFlag(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterESAC6A::localStateNames() const
{
    const auto layout = stateLayout();
    stringVec names{"efd"};
    for (index_t index = 0; index < layout.count; ++index) {
        if (index == layout.sensedVoltage) {
            names.emplace_back("vc");
        } else if (index == layout.regulator) {
            names.emplace_back("xa");
        } else if (index == layout.leadLag) {
            names.emplace_back("xll");
        } else if (index == layout.exciter) {
            names.emplace_back("ve");
        } else if (index == layout.feedback) {
            names.emplace_back("vf");
        }
    }
    return names;
}

index_t ExciterESAC6A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto layout = stateLayout();
    index_t localIndex = kInvalidLocation;
    if ((field == "vc") || (field == "vmeas")) {
        localIndex = layout.sensedVoltage;
    } else if ((field == "xa") || (field == "regulator")) {
        localIndex = layout.regulator;
    } else if ((field == "xll") || (field == "leadlag")) {
        localIndex = layout.leadLag;
    } else if ((field == "ve") || (field == "exciter")) {
        localIndex = layout.exciter;
    } else if ((field == "vf") || (field == "feedback")) {
        localIndex = layout.feedback;
    }
    return (localIndex == kInvalidLocation) ? kInvalidLocation :
                                              offsets.getDiffOffset(sMode) + localIndex;
}

void ExciterESAC6A::set(std::string_view param, std::string_view val)
{
    if (param == "spdmlt") {
        const auto lower = gmlc::utilities::convertToLowerCase(std::string{val});
        if ((lower == "true") || (lower == "on")) {
            speedMultiplier = true;
            return;
        }
        if ((lower == "false") || (lower == "off")) {
            speedMultiplier = false;
            return;
        }
    }
    Exciter::set(param, val);
}

void ExciterESAC6A::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tr") {
        Tr = val;
    } else if (param == "ka") {
        Ka = val;
    } else if (param == "ta") {
        Ta = val;
    } else if (param == "tk") {
        Tk = val;
    } else if (param == "tb") {
        Tb = val;
    } else if (param == "tc") {
        Tc = val;
    } else if (param == "vamax") {
        Vamax = val;
    } else if (param == "vamin") {
        Vamin = val;
    } else if (param == "vrmax") {
        Vrmax = val;
    } else if (param == "vrmin") {
        Vrmin = val;
    } else if (param == "te") {
        Te = val;
    } else if (param == "vfelim") {
        Vfelim = val;
    } else if (param == "kh") {
        Kh = val;
    } else if (param == "vhmax") {
        Vhmax = val;
    } else if (param == "th") {
        Th = val;
    } else if (param == "tj") {
        Tj = val;
    } else if (param == "kc") {
        Kc = val;
    } else if (param == "kd") {
        Kd = val;
    } else if (param == "ke") {
        Ke = val;
    } else if (param == "e1") {
        E1 = val;
    } else if (param == "se1") {
        Se1 = val;
    } else if (param == "e2") {
        E2 = val;
    } else if (param == "se2") {
        Se2 = val;
    } else if (param == "spdmlt") {
        if ((val != 0.0) && (val != 1.0)) {
            throw InvalidParameterValue("ESAC6A SPDMLT must be zero or one");
        }
        speedMultiplier = (val != 0.0);
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterESAC6A::get(std::string_view param, units::unit unitType) const
{
    if (param == "tr") {
        return Tr;
    }
    if (param == "ka") {
        return Ka;
    }
    if (param == "ta") {
        return Ta;
    }
    if (param == "tk") {
        return Tk;
    }
    if (param == "tb") {
        return Tb;
    }
    if (param == "tc") {
        return Tc;
    }
    if (param == "vamax") {
        return Vamax;
    }
    if (param == "vamin") {
        return Vamin;
    }
    if (param == "vrmax") {
        return Vrmax;
    }
    if (param == "vrmin") {
        return Vrmin;
    }
    if (param == "te") {
        return Te;
    }
    if (param == "vfelim") {
        return Vfelim;
    }
    if (param == "kh") {
        return Kh;
    }
    if (param == "vhmax") {
        return Vhmax;
    }
    if (param == "th") {
        return Th;
    }
    if (param == "tj") {
        return Tj;
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
    if (param == "spdmlt") {
        return speedMultiplier ? 1.0 : 0.0;
    }
    return Exciter::get(param, unitType);
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
