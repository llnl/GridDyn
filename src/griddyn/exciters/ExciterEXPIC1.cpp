/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterEXPIC1.h"

#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace griddyn::exciters {
// The expressions below intentionally retain the grouping of the published
// transfer functions.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr double limitTolerance = 1e-7;
    constexpr index_t maximumStates = 7;

    struct Signal {
        double mValue = 0.0;
        std::array<double, maximumStates> mState{};
        std::array<double, exciterInputCount> mInput{};
    };

    Signal constantSignal(double value)
    {
        Signal signal;
        signal.mValue = value;
        return signal;
    }

    Signal stateSignal(double value, index_t index)
    {
        auto signal = constantSignal(value);
        signal.mState[index] = 1.0;
        return signal;
    }

    Signal inputSignal(const IOdata& inputs, index_t index)
    {
        auto signal = constantSignal(inputs[index]);
        signal.mInput[index] = 1.0;
        return signal;
    }

    Signal addSignals(const Signal& left, const Signal& right)
    {
        Signal result;
        result.mValue = left.mValue + right.mValue;
        for (index_t index = 0; index < maximumStates; ++index) {
            result.mState[index] = left.mState[index] + right.mState[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.mInput[index] = left.mInput[index] + right.mInput[index];
        }
        return result;
    }

    Signal scale(const Signal& signal, double factor)
    {
        Signal result;
        result.mValue = factor * signal.mValue;
        for (index_t index = 0; index < maximumStates; ++index) {
            result.mState[index] = factor * signal.mState[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.mInput[index] = factor * signal.mInput[index];
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
        result.mValue = left.mValue * right.mValue;
        for (index_t index = 0; index < maximumStates; ++index) {
            result.mState[index] =
                left.mState[index] * right.mValue + left.mValue * right.mState[index];
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.mInput[index] =
                left.mInput[index] * right.mValue + left.mValue * right.mInput[index];
        }
        return result;
    }

    Signal clampSignal(const Signal& signal, double lower, double upper)
    {
        if (signal.mValue <= lower) {
            return constantSignal(lower);
        }
        if (signal.mValue >= upper) {
            return constantSignal(upper);
        }
        return signal;
    }

    bool finiteMachineSignal(double value)
    {
        return std::isfinite(value) && (std::abs(value) < 1e20);
    }
}  // namespace

ExciterEXPIC1::ExciterEXPIC1(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 1.0;
    Vrmax = 1.0;
    Vrmin = -1.0;
}

CoreObject* ExciterEXPIC1::clone(CoreObject* obj) const
{
    auto* exciterClone = cloneBase<ExciterEXPIC1, Exciter>(this, obj);
    if (exciterClone == nullptr) {
        return obj;
    }
    exciterClone->Tr = Tr;
    exciterClone->Ta1 = Ta1;
    exciterClone->Vr1 = Vr1;
    exciterClone->Vr2 = Vr2;
    exciterClone->Ta2 = Ta2;
    exciterClone->Ta3 = Ta3;
    exciterClone->Ta4 = Ta4;
    exciterClone->Kf = Kf;
    exciterClone->Tf1 = Tf1;
    exciterClone->Tf2 = Tf2;
    exciterClone->Efdmax = Efdmax;
    exciterClone->Efdmin = Efdmin;
    exciterClone->Ke = Ke;
    exciterClone->Te = Te;
    exciterClone->E1 = E1;
    exciterClone->Se1 = Se1;
    exciterClone->E2 = E2;
    exciterClone->Se2 = Se2;
    exciterClone->Kp = Kp;
    exciterClone->Ki = Ki;
    exciterClone->Kc = Kc;
    exciterClone->saturation = saturation;
    return exciterClone;
}

bool ExciterEXPIC1::hasRegulatorFilter() const
{
    return (Ta2 != 0.0) || (Ta3 != 0.0) || (Ta4 != 0.0);
}

bool ExciterEXPIC1::hasFeedbackFilter() const
{
    return Kf != 0.0;
}

ExciterEXPIC1::StateLayout ExciterEXPIC1::stateLayout() const
{
    StateLayout layout;
    if (Te > 0.0) {
        layout.efd = layout.count++;
    }
    if (Tr > 0.0) {
        layout.sensedVoltage = layout.count++;
    }
    layout.piIntegrator = layout.count++;
    if (hasRegulatorFilter()) {
        layout.regulatorOne = layout.count++;
        layout.regulator = layout.count++;
    }
    if (hasFeedbackFilter()) {
        layout.feedbackOne = layout.count++;
        layout.feedback = layout.count++;
    }
    return layout;
}

void ExciterEXPIC1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 24> parameters{Tr,    Ka,    Ta1, Vr1, Vr2, Ta2,    Ta3,    Ta4,
                                            Vrmax, Vrmin, Kf,  Tf1, Tf2, Efdmax, Efdmin, Ke,
                                            Te,    E1,    Se1, E2,  Se2, Kp,     Ki,     Kc};
    const bool partialRegulatorBypass = hasRegulatorFilter() && ((Ta2 <= 0.0) || (Ta4 <= 0.0));
    const bool invalidFeedback = hasFeedbackFilter() && ((Tf1 <= 0.0) || (Tf2 <= 0.0));
    const bool disabledSaturation = (Se1 == 0.0) && (Se2 == 0.0);
    const bool invalidSaturation =
        !disabledSaturation && ((E1 <= 0.0) || (E2 <= E1) || (Se1 <= 0.0) || (Se2 <= 0.0));
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Ka <= 0.0) || (Tr < 0.0) || (Ta1 < 0.0) || (Ta2 < 0.0) || (Ta3 < 0.0) || (Ta4 < 0.0) ||
        (Tf1 < 0.0) || (Tf2 < 0.0) || (Te < 0.0) || (Vr1 < Vr2) || (Vrmax < Vrmin) ||
        (Efdmax < Efdmin) || partialRegulatorBypass || invalidFeedback || invalidSaturation) {
        throw InvalidParameterValue("EXPIC1 gains, time constants, limits, or saturation points");
    }

    saturation.setType(disabledSaturation ?
                           utilities::Saturation::SaturationType::NONE :
                           utilities::Saturation::SaturationType::CUTOFF_QUADRATIC);
    if (!disabledSaturation) {
        // EXPIC1 supplies S_E(E), not the already multiplied E*S_E(E) used by
        // several other exciter models.
        saturation.setParam(E1, Se1, E2, Se2);
        if (!std::isfinite(saturation(E1)) || !std::isfinite(saturation(E2)) ||
            (std::abs(saturation(E1) - Se1) > 1e-10) || (std::abs(saturation(E2) - Se2) > 1e-10)) {
            throw InvalidParameterValue("EXPIC1 saturation fit");
        }
    }

    const auto layout = stateLayout();
    auto& local = offsets.local().local;
    local.algSize = (Te == 0.0) ? 1 : 0;
    local.diffSize = layout.count;
    local.jacSize = 96;
    prevTime = time0;
}

double ExciterEXPIC1::sourceMultiplier(const IOdata& inputs) const
{
    if ((Kp == 0.0) && (Ki == 0.0)) {
        return 1.0;
    }
    return detail::computeRectifierData(inputs, Kp, Ki, Kc, 0.0, 0.0, kBigNum).voltage;
}

ExciterEXPIC1::ModelEvaluation ExciterEXPIC1::evaluateModel(const IOdata& inputs,
                                                            const double state[]) const
{
    const auto layout = stateLayout();
    ModelEvaluation evaluation;
    std::array<Signal, maximumStates> rates{};

    const Signal feedback = hasFeedbackFilter() ?
        stateSignal(state[layout.feedback], layout.feedback) :
        constantSignal(0.0);
    const Signal measuredVoltage = (Tr > 0.0) ?
        stateSignal(state[layout.sensedVoltage], layout.sensedVoltage) :
        inputSignal(inputs, exciterVoltageInLocation);
    Signal reference =
        addSignals(constantSignal(Vref + vBias - 1.0), inputSignal(inputs, exciterVsetInLocation));
    reference = addSignals(reference, inputSignal(inputs, exciterVssInLocation));
    const Signal error = subtract(subtract(reference, measuredVoltage), feedback);
    const Signal piIntegrator = stateSignal(state[layout.piIntegrator], layout.piIntegrator);
    const Signal vaUnlimited = addSignals(piIntegrator, scale(error, Ka * Ta1));
    const Signal regulatorOutput = clampSignal(vaUnlimited, Vr2, Vr1);
    const Signal piRateUnlimited = scale(error, Ka);
    const bool piBlocked = ((vaUnlimited.mValue >= Vr1) && (piRateUnlimited.mValue > 0.0)) ||
        ((vaUnlimited.mValue <= Vr2) && (piRateUnlimited.mValue < 0.0));
    rates[layout.piIntegrator] = piBlocked ? constantSignal(0.0) : piRateUnlimited;

    Signal rawRegulator = regulatorOutput;
    if (hasRegulatorFilter()) {
        const Signal regulatorOne = stateSignal(state[layout.regulatorOne], layout.regulatorOne);
        rates[layout.regulatorOne] = scale(subtract(regulatorOutput, regulatorOne), 1.0 / Ta2);
        const Signal regulator = stateSignal(state[layout.regulator], layout.regulator);
        const Signal regulatorTarget =
            addSignals(regulatorOne, scale(rates[layout.regulatorOne], Ta3));
        rates[layout.regulator] = scale(subtract(regulatorTarget, regulator), 1.0 / Ta4);
        rawRegulator = regulator;
    }
    const Signal limitedRegulator = clampSignal(rawRegulator, Vrmin, Vrmax);

    if (hasFeedbackFilter()) {
        const Signal feedbackOne = stateSignal(state[layout.feedbackOne], layout.feedbackOne);
        rates[layout.feedbackOne] = scale(subtract(limitedRegulator, feedbackOne), 1.0 / Tf1);
        rates[layout.feedback] =
            scale(subtract(scale(rates[layout.feedbackOne], Kf), feedback), 1.0 / Tf2);
    }

    if (Tr > 0.0) {
        rates[layout.sensedVoltage] =
            scale(subtract(inputSignal(inputs, exciterVoltageInLocation), measuredVoltage),
                  1.0 / Tr);
    }

    Signal source = constantSignal(1.0);
    if ((Kp != 0.0) || (Ki != 0.0)) {
        const auto sourceData = detail::computeRectifierData(inputs, Kp, Ki, Kc, 0.0, 0.0, kBigNum);
        source.mValue = sourceData.voltage;
        const std::array<index_t, 5> sourceInputs{exciterIdInLocation,
                                                  exciterIqInLocation,
                                                  exciterVdInLocation,
                                                  exciterVqInLocation,
                                                  exciterXadIfdInLocation};
        for (std::size_t index = 0; index < sourceInputs.size(); ++index) {
            source.mInput[sourceInputs[index]] = sourceData.derivatives[index];
        }
    }
    const Signal exciterInput = clampSignal(multiply(source, limitedRegulator), Efdmin, Efdmax);
    if (Te > 0.0) {
        const Signal efd = stateSignal(state[layout.efd], layout.efd);
        Signal fieldFeedback = scale(efd, Ke + saturation(efd.mValue));
        fieldFeedback.mState[layout.efd] =
            Ke + saturation(efd.mValue) + efd.mValue * saturation.deriv(efd.mValue);
        rates[layout.efd] = scale(subtract(exciterInput, fieldFeedback), 1.0 / Te);
        evaluation.fieldOutput = efd.mValue;
        evaluation.fieldStateDerivatives[layout.efd] = 1.0;
    } else {
        evaluation.fieldOutput = exciterInput.mValue;
        evaluation.fieldStateDerivatives = exciterInput.mState;
        evaluation.fieldInputDerivatives = exciterInput.mInput;
    }

    for (index_t row = 0; row < layout.count; ++row) {
        evaluation.rates[row] = rates[row].mValue;
        evaluation.rateStateDerivatives[row] = rates[row].mState;
        evaluation.rateInputDerivatives[row] = rates[row].mInput;
    }
    return evaluation;
}

void ExciterEXPIC1::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || desiredOutput.empty() ||
        !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("EXPIC1 initial voltage signals");
    }
    if ((Kp != 0.0) || (Ki != 0.0)) {
        for (const index_t index : {exciterIdInLocation,
                                    exciterIqInLocation,
                                    exciterVdInLocation,
                                    exciterVqInLocation,
                                    exciterXadIfdInLocation}) {
            if (!finiteMachineSignal(inputs[index])) {
                throw InvalidParameterValue(
                    "EXPIC1 requires compatible synchronous-machine signals");
            }
        }
    }

    const double fieldVoltage = desiredOutput[0];
    const double source = sourceMultiplier(inputs);
    if (!std::isfinite(source) || (source <= 1e-12)) {
        throw InvalidParameterValue("EXPIC1 initial source multiplier");
    }
    const double requiredExciterInput =
        (Te > 0.0) ? (Ke + saturation(fieldVoltage)) * fieldVoltage : fieldVoltage;
    if ((requiredExciterInput < Efdmin - limitTolerance) ||
        (requiredExciterInput > Efdmax + limitTolerance)) {
        throw InvalidParameterValue("EXPIC1 initial exciter input outside limits");
    }
    const double initialRegulator = requiredExciterInput / source;
    if ((initialRegulator < Vrmin - limitTolerance) ||
        (initialRegulator > Vrmax + limitTolerance) || (initialRegulator < Vr2 - limitTolerance) ||
        (initialRegulator > Vr1 + limitTolerance)) {
        throw InvalidParameterValue("EXPIC1 initial regulator output outside limits");
    }

    const auto layout = stateLayout();
    const index_t algebraicCount = (Te == 0.0) ? 1 : 0;
    double* differentialState = m_state.data() + algebraicCount;
    if (Te > 0.0) {
        differentialState[layout.efd] = fieldVoltage;
    } else {
        m_state[0] = fieldVoltage;
    }
    if (Tr > 0.0) {
        differentialState[layout.sensedVoltage] = inputs[exciterVoltageInLocation];
    }
    differentialState[layout.piIntegrator] = initialRegulator;
    if (hasRegulatorFilter()) {
        differentialState[layout.regulatorOne] = initialRegulator;
        differentialState[layout.regulator] = initialRegulator;
    }
    if (hasFeedbackFilter()) {
        differentialState[layout.feedbackOne] = initialRegulator;
        differentialState[layout.feedback] = 0.0;
    }

    vBias = inputs[exciterVoltageInLocation] - Vref - (inputs[exciterVsetInLocation] - 1.0) -
        inputs[exciterVssInLocation];
    fieldSet.resize(2);
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);

    const auto initialized = evaluateModel(inputs, differentialState);
    if (!std::isfinite(initialized.fieldOutput) ||
        (std::abs(initialized.fieldOutput - fieldVoltage) > 1e-7) ||
        std::any_of(initialized.rates.begin(),
                    initialized.rates.begin() + layout.count,
                    [](double rate) { return !std::isfinite(rate) || (std::abs(rate) > 1e-7); })) {
        throw InvalidParameterValue("EXPIC1 initial equations are inconsistent");
    }
}

void ExciterEXPIC1::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const auto evaluation = evaluateModel(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode) && (Te == 0.0)) {
        locations.destLoc[0] = evaluation.fieldOutput - locations.algStateLoc[0];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t index = 0; index < locations.diffSize; ++index) {
        locations.destDiffLoc[index] = evaluation.rates[index] - locations.dstateLoc[index];
    }
}

void ExciterEXPIC1::derivative(const IOdata& inputs,
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

void ExciterEXPIC1::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode) && (Te == 0.0)) {
        const auto locations = offsets.getLocations(stateData, update, sMode, this);
        locations.destLoc[0] = evaluateModel(inputs, locations.diffStateLoc).fieldOutput;
    }
}

void ExciterEXPIC1::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluateModel(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode) && (Te == 0.0)) {
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
            const double value = evaluation.rateInputDerivatives[row][input];
            if (value != 0.0) {
                matrixData.assignCheckCol(locations.diffOffset + row, inputLocs[input], value);
            }
        }
    }
}

void ExciterEXPIC1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const index_t algebraicCount = (Te == 0.0) ? 1 : 0;
    const auto layout = stateLayout();
    for (index_t index = 0; index < layout.count; ++index) {
        m_state[algebraicCount + index] += timeStep * m_dstate_dt[algebraicCount + index];
    }
    if (Te == 0.0) {
        m_state[0] = evaluateModel(inputs, m_state.data() + 1).fieldOutput;
    }
    prevTime = time;
}

stringVec ExciterEXPIC1::localStateNames() const
{
    const auto layout = stateLayout();
    stringVec names;
    names.reserve(static_cast<std::size_t>(layout.count) + ((Te == 0.0) ? 1U : 0U));
    if (Te == 0.0) {
        names.emplace_back("efd");
    }
    for (index_t index = 0; index < layout.count; ++index) {
        if (index == layout.efd) {
            names.emplace_back("efd");
        } else if (index == layout.sensedVoltage) {
            names.emplace_back("et");
        } else if (index == layout.piIntegrator) {
            names.emplace_back("xa");
        } else if (index == layout.regulatorOne) {
            names.emplace_back("vr1");
        } else if (index == layout.regulator) {
            names.emplace_back("vr");
        } else if (index == layout.feedbackOne) {
            names.emplace_back("vf1");
        } else if (index == layout.feedback) {
            names.emplace_back("vf");
        }
    }
    return names;
}

index_t ExciterEXPIC1::findIndex(std::string_view field, const SolverMode& sMode) const
{
    const auto layout = stateLayout();
    if ((field == "efd") || (field == "field")) {
        return (Te == 0.0) ? offsets.getAlgOffset(sMode) :
                             offsets.getDiffOffset(sMode) + layout.efd;
    }
    index_t localIndex = kInvalidLocation;
    if ((field == "et") || (field == "vmeas")) {
        localIndex = layout.sensedVoltage;
    } else if ((field == "xa") || (field == "va") || (field == "pi")) {
        localIndex = layout.piIntegrator;
    } else if (field == "vr1") {
        localIndex = layout.regulatorOne;
    } else if ((field == "vr") || (field == "regulator")) {
        localIndex = layout.regulator;
    } else if (field == "vf1") {
        localIndex = layout.feedbackOne;
    } else if ((field == "vf") || (field == "feedback")) {
        localIndex = layout.feedback;
    }
    return (localIndex == kInvalidLocation) ? localIndex :
                                              offsets.getDiffOffset(sMode) + localIndex;
}

void ExciterEXPIC1::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterEXPIC1::set(std::string_view param, double val, units::unit unitType)
{
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("EXPIC1 parameters must be finite");
    }
    if (param == "tr") {
        Tr = val;
    } else if (param == "ta1") {
        Ta1 = val;
    } else if (param == "vr1") {
        Vr1 = val;
    } else if (param == "vr2") {
        Vr2 = val;
    } else if (param == "ta2") {
        Ta2 = val;
    } else if (param == "ta3") {
        Ta3 = val;
    } else if (param == "ta4") {
        Ta4 = val;
    } else if (param == "kf") {
        Kf = val;
    } else if (param == "tf1") {
        Tf1 = val;
    } else if (param == "tf2") {
        Tf2 = val;
    } else if (param == "efdmax") {
        Efdmax = val;
    } else if (param == "efdmin") {
        Efdmin = val;
    } else if (param == "ke") {
        Ke = val;
    } else if (param == "te") {
        Te = val;
    } else if (param == "e1") {
        E1 = val;
    } else if (param == "se1") {
        Se1 = val;
    } else if (param == "e2") {
        E2 = val;
    } else if (param == "se2") {
        Se2 = val;
    } else if (param == "kp") {
        Kp = val;
    } else if (param == "ki") {
        Ki = val;
    } else if (param == "kc") {
        Kc = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterEXPIC1::get(std::string_view param, units::unit unitType) const
{
    if (param == "ka") {
        return Ka;
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
    if (param == "ta1") {
        return Ta1;
    }
    if (param == "vr1") {
        return Vr1;
    }
    if (param == "vr2") {
        return Vr2;
    }
    if (param == "ta2") {
        return Ta2;
    }
    if (param == "ta3") {
        return Ta3;
    }
    if (param == "ta4") {
        return Ta4;
    }
    if (param == "kf") {
        return Kf;
    }
    if (param == "tf1") {
        return Tf1;
    }
    if (param == "tf2") {
        return Tf2;
    }
    if (param == "efdmax") {
        return Efdmax;
    }
    if (param == "efdmin") {
        return Efdmin;
    }
    if (param == "ke") {
        return Ke;
    }
    if (param == "te") {
        return Te;
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
    if (param == "kp") {
        return Kp;
    }
    if (param == "ki") {
        return Ki;
    }
    if (param == "kc") {
        return Kc;
    }
    return Exciter::get(param, unitType);
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
