/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterESAC5A.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::exciters {

const ModelDescription& ExciterESAC5A::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterESAC5A>(
        "ExciterESAC5A",
        &Exciter::getModelDescription(),
        {parameter({"tr"}, &ExciterESAC5A::Tr, units::second, "Voltage measurement time constant"),
         parameter({"te"}, &ExciterESAC5A::Te, units::second, "Exciter time constant"),
         parameter({"kf"}, &ExciterESAC5A::Kf, units::one, "Kf gain or coefficient"),
         parameter({"tf1"}, &ExciterESAC5A::Tf1, units::second, "Tf1 time constant"),
         parameter({"tf2"}, &ExciterESAC5A::Tf2, units::second, "Tf2 time constant"),
         parameter({"tf3"}, &ExciterESAC5A::Tf3, units::second, "Tf3 time constant"),
         parameter({"ke"}, &ExciterESAC5A::Ke, units::one, "Ke gain or coefficient"),
         parameter({"e1"}, &ExciterESAC5A::E1, units::one, "E1 model setting"),
         parameter({"se1"}, &ExciterESAC5A::Se1, units::one, "Se1 model setting"),
         parameter({"e2"}, &ExciterESAC5A::E2, units::one, "E2 model setting"),
         parameter({"se2"}, &ExciterESAC5A::Se2, units::one, "Se2 model setting"),
         state("efd", units::one, "Field voltage"),
         state("vc", units::one, "Sensed terminal voltage"),
         state("vr", units::one, "Regulator state"),
         state("xll", units::one, "Lead-lag state"),
         state("xwf", units::one, "Washout state"),
         state("ve", units::one, "Exciter voltage")});
    return description;
}

ExciterESAC5A::ExciterESAC5A(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 80.0;
    Ta = 0.04;
    Vrmax = 7.3;
    Vrmin = -7.3;
}

CoreObject* ExciterESAC5A::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExciterESAC5A, Exciter>(this, obj);
    if (result == nullptr) {
        return obj;
    }
    Exciter::clone(result);
    result->Tr = Tr;
    result->Te = Te;
    result->Kf = Kf;
    result->Tf1 = Tf1;
    result->Tf2 = Tf2;
    result->Tf3 = Tf3;
    result->Ke = Ke;
    result->E1 = E1;
    result->Se1 = Se1;
    result->E2 = E2;
    result->Se2 = Se2;
    result->saturation = saturation;
    return result;
}

ExciterESAC5A::Layout ExciterESAC5A::layout() const
{
    Layout result;
    if (Tr > 0.0) {
        result.sensed = result.count++;
    }
    result.regulator = result.count++;
    if (Tf2 > 0.0) {
        result.leadLag = result.count++;
    }
    result.washout = result.count++;
    result.field = result.count++;
    return result;
}

void ExciterESAC5A::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    const std::array<double, 15> parameters{
        Tr, Ka, Ta, Vrmax, Vrmin, Te, Kf, Tf1, Tf2, Tf3, Ke, E1, Se1, E2, Se2};
    if (std::any_of(parameters.begin(),
                    parameters.end(),
                    [](double value) { return !std::isfinite(value); }) ||
        (Tr < 0.0) || (Ka <= 0.0) || (Ta <= 0.0) || (Vrmax < Vrmin) || (Te <= 0.0) ||
        (Tf1 <= 0.0) || (Tf2 < 0.0) || (Tf3 < 0.0) || (E1 < 0.0) || (E2 <= E1) || (Se1 < 0.0) ||
        (Se2 < 0.0) || ((Se1 > 0.0) && (Se2 == 0.0))) {
        throw InvalidParameterValue("ESAC5A gains, time constants, limits, or saturation points");
    }
    const bool noSaturation = (Se1 == 0.0) && (Se2 == 0.0);
    saturation.setType(noSaturation ? utilities::Saturation::SaturationType::NONE :
                                      utilities::Saturation::SaturationType::CUTOFF_QUADRATIC);
    if (!noSaturation) {
        // ANDES ExcQuadSat fits S(E1)=E1*SE1 and S(E2)=E2*SE2.
        saturation.setParam(E1, E1 * Se1, E2, E2 * Se2);
        if (!std::isfinite(saturation(E2)) || (std::abs(saturation(E2) - (E2 * Se2)) > 1e-9) ||
            (std::abs(saturation(E1) - (E1 * Se1)) > 1e-9)) {
            throw InvalidParameterValue("ESAC5A saturation fit");
        }
    }
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = layout().count;
    local.algRoots = 1;
    local.jacSize = 64;
    prevTime = time0;
}

ExciterESAC5A::Evaluation ExciterESAC5A::evaluate(const IOdata& inputs, const double state[]) const
{
    const auto indices = layout();
    Evaluation result;
    const double measured = (Tr > 0.0) ? state[indices.sensed] : inputs[exciterVoltageInLocation];
    const double regulator = state[indices.regulator];
    const double field = state[indices.field];
    const double washoutState = state[indices.washout];
    const double leadFraction = (Tf2 > 0.0) ? Tf3 / Tf2 : 1.0;
    const double leadLag = (Tf2 > 0.0) ?
        state[indices.leadLag] + (leadFraction * (regulator - state[indices.leadLag])) :
        regulator;
    const double feedback = Kf / Tf1 * (leadLag - washoutState);
    const double reference =
        Vref + vBias - 1.0 + inputs[exciterVsetInLocation] + inputs[exciterVssInLocation];
    result.regulatorDrive = ((Ka * (reference - measured - feedback)) - regulator) / Ta;

    if (Tr > 0.0) {
        result.rates[indices.sensed] = (inputs[exciterVoltageInLocation] - measured) / Tr;
        result.stateJac[indices.sensed][indices.sensed] = -1.0 / Tr;
        result.inputJac[indices.sensed][exciterVoltageInLocation] = 1.0 / Tr;
    }
    if (!opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        result.rates[indices.regulator] = result.regulatorDrive;
        auto& row = result.stateJac[indices.regulator];
        row[indices.regulator] = (-1.0 - ((Ka * Kf * leadFraction) / Tf1)) / Ta;
        if (Tr > 0.0) {
            row[indices.sensed] = -Ka / Ta;
        } else {
            result.inputJac[indices.regulator][exciterVoltageInLocation] = -Ka / Ta;
        }
        if (Tf2 > 0.0) {
            row[indices.leadLag] = -Ka * Kf * (1.0 - leadFraction) / (Tf1 * Ta);
        }
        row[indices.washout] = Ka * Kf / (Tf1 * Ta);
        result.inputJac[indices.regulator][exciterVsetInLocation] = Ka / Ta;
        result.inputJac[indices.regulator][exciterVssInLocation] = Ka / Ta;
    }
    if (Tf2 > 0.0) {
        result.rates[indices.leadLag] = (regulator - state[indices.leadLag]) / Tf2;
        result.stateJac[indices.leadLag][indices.regulator] = 1.0 / Tf2;
        result.stateJac[indices.leadLag][indices.leadLag] = -1.0 / Tf2;
    }
    result.rates[indices.washout] = (leadLag - washoutState) / Tf1;
    result.stateJac[indices.washout][indices.regulator] = leadFraction / Tf1;
    if (Tf2 > 0.0) {
        result.stateJac[indices.washout][indices.leadLag] = (1.0 - leadFraction) / Tf1;
    }
    result.stateJac[indices.washout][indices.washout] = -1.0 / Tf1;
    const auto saturationData = saturation.evaluate(field);
    result.rates[indices.field] = (regulator - (Ke * field) - saturationData.value) / Te;
    result.stateJac[indices.field][indices.regulator] = 1.0 / Te;
    result.stateJac[indices.field][indices.field] = (-Ke - saturationData.derivative) / Te;
    return result;
}

void ExciterESAC5A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    const double originalUpperLimit = Vrmax;
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(desiredOutput[0]) || !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation])) {
        throw InvalidParameterValue("ESAC5A initial voltage signals");
    }
    const double field = desiredOutput[0];
    const double regulator = (Ke * field) + saturation(field);
    if ((regulator < Vrmin - 1e-7) ||
        !adjustInitialUpperLimit(regulator, Vrmax, "ESAC5A initial regulator output")) {
        throw InvalidParameterValue("ESAC5A initial regulator output outside limits");
    }
    const auto indices = layout();
    double* state = m_state.data() + 1;
    if (Tr > 0.0) {
        state[indices.sensed] = inputs[exciterVoltageInLocation];
    }
    state[indices.regulator] = regulator;
    if (Tf2 > 0.0) {
        state[indices.leadLag] = regulator;
    }
    state[indices.washout] = regulator;
    state[indices.field] = field;
    m_state[0] = field;
    vBias = inputs[exciterVoltageInLocation] + (regulator / Ka) - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation];
    fieldSet.resize(2);
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    const double correction = std::max(0.0, Vrmax - originalUpperLimit);
    const double guard = 10.0 * limiterRootTolerance * Ta + std::abs(Ka) * 1e-5;
    if ((Vrmax != 0.0) && (regulator >= Vrmax)) {
        vBias += std::max(0.2 * correction, guard) / Ka;
        opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        opFlags.set(TRIGGER_HIGH);
    } else if (regulator <= Vrmin) {
        vBias -= guard / Ka;
        opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
        opFlags.reset(TRIGGER_HIGH);
    } else {
        opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
        opFlags.reset(TRIGGER_HIGH);
    }
    const auto initialized = evaluate(inputs, state);
    if (std::any_of(initialized.rates.begin(),
                    initialized.rates.begin() + indices.count,
                    [](double value) { return !std::isfinite(value) || std::abs(value) > 1e-7; })) {
        throw InvalidParameterValue("ESAC5A initial equations are inconsistent");
    }
}

void ExciterESAC5A::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const auto evaluation = evaluate(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] = locations.diffStateLoc[layout().field] - locations.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < locations.diffSize; ++index) {
            locations.destDiffLoc[index] = evaluation.rates[index] - locations.dstateLoc[index];
        }
    }
}

void ExciterESAC5A::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const auto evaluation = evaluate(inputs, locations.diffStateLoc);
    std::copy_n(evaluation.rates.begin(), locations.diffSize, locations.destDiffLoc);
}

void ExciterESAC5A::algebraicUpdate(const IOdata& /*inputs*/,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto locations = offsets.getLocations(stateData, update, sMode, this);
        locations.destLoc[0] = locations.diffStateLoc[layout().field];
    }
}

void ExciterESAC5A::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto evaluation = evaluate(inputs, locations.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(locations.algOffset, locations.algOffset, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(locations.algOffset, locations.diffOffset + layout().field, 1.0);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    for (index_t row = 0; row < locations.diffSize; ++row) {
        for (index_t column = 0; column < locations.diffSize; ++column) {
            double value = evaluation.stateJac[row][column];
            if (row == column) {
                value -= stateData.cj;
            }
            if (value != 0.0) {
                matrixData.assign(locations.diffOffset + row, locations.diffOffset + column, value);
            }
        }
        for (index_t input = 0; input < exciterInputCount; ++input) {
            if (evaluation.inputJac[row][input] != 0.0) {
                matrixData.assignCheckCol(locations.diffOffset + row,
                                          inputLocs[input],
                                          evaluation.inputJac[row][input]);
            }
        }
    }
}

void ExciterESAC5A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double step = time - prevTime;
    const auto indices = layout();
    for (index_t index = 0; index < indices.count; ++index) {
        m_state[index + 1] += step * m_dstate_dt[index + 1];
    }
    m_state[0] = m_state[indices.field + 1];
    updateRegulatorLimit(inputs, m_state.data() + 1);
    prevTime = time;
}

bool ExciterESAC5A::updateRegulatorLimit(const IOdata& inputs, const double state[])
{
    const auto indices = layout();
    const double drive = evaluate(inputs, state).regulatorDrive;
    const bool high = (state[indices.regulator] >= Vrmax) && (drive > 0.0);
    const bool low = (state[indices.regulator] <= Vrmin) && (drive < 0.0);
    const bool changed =
        opFlags[OUTSIDE_VOLTAGE_LIMITS] != (high || low) || (high && !opFlags[TRIGGER_HIGH]);
    opFlags.set(OUTSIDE_VOLTAGE_LIMITS, high || low);
    opFlags.set(TRIGGER_HIGH, high);
    return changed;
}

void ExciterESAC5A::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const auto indices = layout();
    const double regulator = locations.diffStateLoc[indices.regulator];
    const double drive = evaluate(inputs, locations.diffStateLoc).regulatorDrive;
    double rootValue = std::min(Vrmax - regulator, regulator - Vrmin) + limiterRootTolerance;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        rootValue = (opFlags[TRIGGER_HIGH] ? -drive : drive) + limiterRootTolerance;
    }
    roots[offsets.getRootOffset(sMode)] = rootValue;
}

void ExciterESAC5A::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    if (rootMask[offsets.getRootOffset(sMode)] == 0) {
        return;
    }
    auto* state = m_state.data() + 1;
    state[layout().regulator] = std::clamp(state[layout().regulator],
                                           static_cast<double>(Vrmin),
                                           static_cast<double>(Vrmax));
    if (updateRegulatorLimit(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode ExciterESAC5A::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    auto* state = m_state.data() + 1;
    state[layout().regulator] = std::clamp(state[layout().regulator],
                                           static_cast<double>(Vrmin),
                                           static_cast<double>(Vrmax));
    if (updateRegulatorLimit(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterESAC5A::localStateNames() const
{
    const auto indices = layout();
    stringVec names{"efd"};
    for (index_t index = 0; index < indices.count; ++index) {
        if (index == indices.sensed) {
            names.emplace_back("vc");
        } else if (index == indices.regulator) {
            names.emplace_back("vr");
        } else if (index == indices.leadLag) {
            names.emplace_back("xll");
        } else if (index == indices.washout) {
            names.emplace_back("xwf");
        } else {
            names.emplace_back("ve");
        }
    }
    return names;
}

index_t ExciterESAC5A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const auto indices = layout();
    index_t location = kInvalidLocation;
    if ((field == "vc") || (field == "vmeas")) {
        location = indices.sensed;
    } else if ((field == "vr") || (field == "regulator")) {
        location = indices.regulator;
    } else if ((field == "xll") || (field == "leadlag")) {
        location = indices.leadLag;
    } else if ((field == "xwf") || (field == "washout")) {
        location = indices.washout;
    } else if ((field == "ve") || (field == "exciter")) {
        location = indices.field;
    }
    return (location == kInvalidLocation) ? kInvalidLocation :
                                            offsets.getDiffOffset(sMode) + location;
}

void ExciterESAC5A::set(std::string_view param, std::string_view val)
{
    if (!isSetHandled(ExciterESAC5A::getModelDescription().trySetLocal(*this, param, val))) {
        Exciter::set(param, val);
    }
}

}  // namespace griddyn::exciters
