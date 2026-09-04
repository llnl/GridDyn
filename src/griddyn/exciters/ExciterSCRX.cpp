/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterSCRX.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::exciters {
namespace {
    constexpr index_t scrxMaximumStates = 2;

    // Signal is an internal algebraic-differentiation carrier; lowercase field
    // names match the mathematical notation used throughout this implementation.
    // NOLINTBEGIN(readability-identifier-naming)
    struct Signal {
        double value = 0.0;
        std::array<double, scrxMaximumStates> state{};
        std::array<double, exciterInputCount> input{};
    };
    // NOLINTEND(readability-identifier-naming)

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
        for (index_t index = 0; index < scrxMaximumStates; ++index) {
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
        for (index_t index = 0; index < scrxMaximumStates; ++index) {
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
        for (index_t index = 0; index < scrxMaximumStates; ++index) {
            result.state[index] =
                (left.state[index] * right.value) + (left.value * right.state[index]);
        }
        for (index_t index = 0; index < exciterInputCount; ++index) {
            result.input[index] =
                (left.input[index] * right.value) + (left.value * right.input[index]);
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
}  // namespace

ExciterSCRX::ExciterSCRX(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 100.0;
    Vrmin = -10.0;
    Vrmax = 10.0;
}

CoreObject* ExciterSCRX::clone(CoreObject* obj) const
{
    auto* exciterClone = cloneBase<ExciterSCRX, Exciter>(this, obj);
    if (exciterClone == nullptr) {
        return obj;
    }
    exciterClone->TaOverTb = TaOverTb;
    exciterClone->Tb = Tb;
    exciterClone->Te = Te;
    exciterClone->rCrFd = rCrFd;
    exciterClone->solidFed = solidFed;
    return exciterClone;
}

ExciterSCRX::StateLayout ExciterSCRX::stateLayout() const
{
    StateLayout layout;
    if (Tb > 0.0) {
        layout.leadLag = layout.count++;
    }
    if (Te > 0.0) {
        layout.amplifier = layout.count++;
    }
    return layout;
}

void ExciterSCRX::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double, 7> parameters{TaOverTb, Tb, Ka, Te, Vrmin, Vrmax, rCrFd};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (TaOverTb < 0.0) || (Tb < 0.0) || (Te < 0.0) || (Ka == 0.0) || (Vrmin > Vrmax) ||
        ((Tb == 0.0) && (TaOverTb != 0.0))) {
        throw InvalidParameterValue("SCRX gains, time constants, limits, or selector data");
    }
    const auto layout = stateLayout();
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = layout.count;
    local.algRoots = (layout.amplifier == kInvalidLocation) ? 0 : 1;
    local.jacSize = 32;
    prevTime = time0;
}

ExciterSCRX::ModelEvaluation ExciterSCRX::evaluateModel(const IOdata& inputs,
                                                        const double state[]) const
{
    const auto layout = stateLayout();
    ModelEvaluation evaluation;
    std::array<Signal, maximumStates> rates{};

    Signal reference =
        addSignals(constantSignal(Vref + vBias - 1.0), inputSignal(inputs, exciterVsetInLocation));
    reference = addSignals(reference, inputSignal(inputs, exciterVssInLocation));
    const Signal error = subtract(reference, inputSignal(inputs, exciterVoltageInLocation));

    Signal leadOutput = error;
    if (Tb > 0.0) {
        const Signal leadState = stateSignal(state[layout.leadLag], layout.leadLag);
        rates[layout.leadLag] = scale(subtract(error, leadState), 1.0 / Tb);
        leadOutput = addSignals(leadState, scale(subtract(error, leadState), TaOverTb));
    }

    Signal amplifierOutput;
    if (Te > 0.0) {
        const Signal amplifier = stateSignal(state[layout.amplifier], layout.amplifier);
        const Signal rawRate = scale(subtract(scale(leadOutput, Ka), amplifier), 1.0 / Te);
        evaluation.amplifierLimitDrive = rawRate.value;
        const bool blocked = ((amplifier.value >= Vrmax) && (rawRate.value > 0.0)) ||
            ((amplifier.value <= Vrmin) && (rawRate.value < 0.0));
        rates[layout.amplifier] = blocked ? constantSignal(0.0) : rawRate;
        amplifierOutput = clamp(amplifier, Vrmin, Vrmax);
    } else {
        amplifierOutput = clamp(scale(leadOutput, Ka), Vrmin, Vrmax);
    }

    const Signal sourceMultiplier =
        solidFed ? constantSignal(1.0) : inputSignal(inputs, exciterVoltageInLocation);
    Signal fieldOutput = multiply(sourceMultiplier, amplifierOutput);
    if (inputs[exciterXadIfdInLocation] < 0.0) {
        fieldOutput = scale(inputSignal(inputs, exciterXadIfdInLocation), -rCrFd);
    }
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

void ExciterSCRX::dynObjectInitializeB(const IOdata& inputs,
                                       const IOdata& desiredOutput,
                                       IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) || !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("SCRX initial voltage signals");
    }
    if (!std::isfinite(inputs[exciterXadIfdInLocation]) ||
        (std::abs(inputs[exciterXadIfdInLocation]) > 1e20)) {
        throw InvalidParameterValue("SCRX requires a compatible field-current signal");
    }
    if (inputs[exciterXadIfdInLocation] < 0.0) {
        throw InvalidParameterValue("SCRX negative-field-current initialization is unsupported");
    }

    const double source = solidFed ? 1.0 : inputs[exciterVoltageInLocation];
    if (!std::isfinite(source) || (std::abs(source) <= 1e-12)) {
        throw InvalidParameterValue("SCRX initial source multiplier");
    }
    const double amplifierOutput = desiredOutput[0] / source;
    if ((amplifierOutput < Vrmin - 1e-7) || (amplifierOutput > Vrmax + 1e-7)) {
        throw InvalidParameterValue("SCRX initial amplifier output outside limits");
    }
    const double error = amplifierOutput / Ka;
    const auto layout = stateLayout();
    double* state = m_state.data() + 1;
    if (Tb > 0.0) {
        state[layout.leadLag] = error;
    }
    if (Te > 0.0) {
        state[layout.amplifier] = amplifierOutput;
    }
    m_state[0] = desiredOutput[0];
    vBias = error + inputs[exciterVoltageInLocation] - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation];
    fieldSet.resize(2);
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);

    const auto initialized = evaluateModel(inputs, state);
    if (!std::isfinite(initialized.fieldOutput) ||
        (std::abs(initialized.fieldOutput - desiredOutput[0]) > 1e-7) ||
        std::any_of(initialized.rates.begin(),
                    initialized.rates.begin() + layout.count,
                    [](double value) { return std::abs(value) > 1e-7; })) {
        throw InvalidParameterValue("SCRX initial equations are inconsistent");
    }
    updateLimitFlags(inputs, state);
}

void ExciterSCRX::residual(const IOdata& inputs,
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

void ExciterSCRX::derivative(const IOdata& inputs,
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

void ExciterSCRX::algebraicUpdate(const IOdata& inputs,
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

void ExciterSCRX::jacobianElements(const IOdata& inputs,
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

void ExciterSCRX::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const auto layout = stateLayout();
    for (index_t index = 0; index < layout.count; ++index) {
        m_state[index + 1] += timeStep * m_dstate_dt[index + 1];
    }
    if (layout.amplifier != kInvalidLocation) {
        m_state[layout.amplifier + 1] = std::clamp(m_state[layout.amplifier + 1],
                                                   static_cast<double>(Vrmin),
                                                   static_cast<double>(Vrmax));
    }
    m_state[0] = evaluateModel(inputs, m_state.data() + 1).fieldOutput;
    updateLimitFlags(inputs, m_state.data() + 1);
    prevTime = time;
}

bool ExciterSCRX::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const auto layout = stateLayout();
    if (layout.amplifier == kInvalidLocation) {
        return false;
    }
    const double amplifier = state[layout.amplifier];
    const double drive = evaluateModel(inputs, state).amplifierLimitDrive;
    int status = 0;
    if ((amplifier >= Vrmax) && (drive >= 0.0)) {
        status = 1;
    } else if ((amplifier <= Vrmin) && (drive <= 0.0)) {
        status = -1;
    }
    const bool changed = (opFlags[AMPLIFIER_LIMITED] != (status != 0)) ||
        (opFlags[AMPLIFIER_LIMIT_HIGH] != (status > 0));
    opFlags.set(AMPLIFIER_LIMITED, status != 0);
    opFlags.set(AMPLIFIER_LIMIT_HIGH, status > 0);
    return changed;
}

void ExciterSCRX::rootTest(const IOdata& inputs,
                           const StateData& stateData,
                           double roots[],
                           const SolverMode& sMode)
{
    const auto layout = stateLayout();
    if (layout.amplifier == kInvalidLocation) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double amplifier = locations.diffStateLoc[layout.amplifier];
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if (opFlags[AMPLIFIER_LIMITED]) {
        const double drive = evaluateModel(inputs, locations.diffStateLoc).amplifierLimitDrive;
        roots[rootOffset] = opFlags[AMPLIFIER_LIMIT_HIGH] ? drive : -drive;
    } else {
        roots[rootOffset] = std::min(Vrmax - amplifier, amplifier - Vrmin);
    }
}

void ExciterSCRX::rootTrigger(CoreTime /*time*/,
                              const IOdata& inputs,
                              const std::vector<int>& rootMask,
                              const SolverMode& sMode)
{
    const auto layout = stateLayout();
    if (layout.amplifier == kInvalidLocation) {
        return;
    }
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if (rootMask[rootOffset] != 0) {
        if (opFlags[AMPLIFIER_LIMITED]) {
            opFlags.reset(AMPLIFIER_LIMITED);
            opFlags.reset(AMPLIFIER_LIMIT_HIGH);
            alert(this, JAC_COUNT_CHANGE);
        } else {
            double* state = m_state.data() + 1;
            state[layout.amplifier] = std::clamp(state[layout.amplifier],
                                                 static_cast<double>(Vrmin),
                                                 static_cast<double>(Vrmax));
            if (updateLimitFlags(inputs, state)) {
                alert(this, JAC_COUNT_CHANGE);
            }
        }
    }
}

ChangeCode ExciterSCRX::rootCheck(const IOdata& inputs,
                                  const StateData& /*stateData*/,
                                  const SolverMode& /*sMode*/,
                                  CheckLevel /*level*/)
{
    const auto layout = stateLayout();
    if (layout.amplifier == kInvalidLocation) {
        return ChangeCode::NO_CHANGE;
    }
    double* state = m_state.data() + 1;
    state[layout.amplifier] =
        std::clamp(state[layout.amplifier], static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterSCRX::localStateNames() const
{
    const auto layout = stateLayout();
    stringVec names{"efd"};
    if (layout.leadLag != kInvalidLocation) {
        names.emplace_back("xll");
    }
    if (layout.amplifier != kInvalidLocation) {
        names.emplace_back("efdpre");
    }
    return names;
}

index_t ExciterSCRX::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto layout = stateLayout();
    if ((field == "xll") || (field == "leadlag")) {
        return (layout.leadLag == kInvalidLocation) ? kInvalidLocation :
                                                      offsets.getDiffOffset(sMode) + layout.leadLag;
    }
    if ((field == "efdpre") || (field == "amplifier")) {
        return (layout.amplifier == kInvalidLocation) ?
            kInvalidLocation :
            offsets.getDiffOffset(sMode) + layout.amplifier;
    }
    return kInvalidLocation;
}

void ExciterSCRX::set(std::string_view param, std::string_view val)
{
    if (param == "cswitch") {
        const auto lower = gmlc::utilities::convertToLowerCase(std::string{val});
        if ((lower == "solid") || (lower == "solidfed") || (lower == "true")) {
            solidFed = true;
            return;
        }
        if ((lower == "bus") || (lower == "busfed") || (lower == "false")) {
            solidFed = false;
            return;
        }
    }
    Exciter::set(param, val);
}

void ExciterSCRX::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tatb") {
        TaOverTb = val;
    } else if (param == "tb") {
        Tb = val;
    } else if ((param == "k") || (param == "ka")) {
        Ka = val;
    } else if (param == "te") {
        Te = val;
    } else if ((param == "emin") || (param == "efdmin") || (param == "vrmin")) {
        Vrmin = val;
    } else if ((param == "emax") || (param == "efdmax") || (param == "vrmax")) {
        Vrmax = val;
    } else if (param == "cswitch") {
        if ((val != 0.0) && (val != 1.0)) {
            throw InvalidParameterValue("SCRX CSWITCH must be zero or one");
        }
        solidFed = (val != 0.0);
    } else if ((param == "rcrfd") || (param == "rc_rfd")) {
        rCrFd = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterSCRX::get(std::string_view param, units::unit unitType) const
{
    if (param == "tatb") {
        return TaOverTb;
    }
    if (param == "tb") {
        return Tb;
    }
    if ((param == "k") || (param == "ka")) {
        return Ka;
    }
    if (param == "te") {
        return Te;
    }
    if ((param == "emin") || (param == "efdmin") || (param == "vrmin")) {
        return Vrmin;
    }
    if ((param == "emax") || (param == "efdmax") || (param == "vrmax")) {
        return Vrmax;
    }
    if (param == "cswitch") {
        return solidFed ? 1.0 : 0.0;
    }
    if ((param == "rcrfd") || (param == "rc_rfd")) {
        return rCrFd;
    }
    return Exciter::get(param, unitType);
}
}  // namespace griddyn::exciters
