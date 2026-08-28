/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterEXAC4.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::exciters {
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t leadLagState = 1;
    constexpr index_t regulatorState = 2;
}  // namespace

ExciterEXAC4::ExciterEXAC4(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 80.0;
    Ta = 0.04;
    Vrmax = 8.0;
    Vrmin = 0.0;
}

CoreObject* ExciterEXAC4::clone(CoreObject* obj) const
{
    auto* clone = cloneBase<ExciterEXAC4, Exciter>(this, obj);
    if (clone == nullptr) return obj;
    clone->Tr = Tr;
    clone->Vimax = Vimax;
    clone->Vimin = Vimin;
    clone->Tc = Tc;
    clone->Tb = Tb;
    clone->Kc = Kc;
    clone->vref0 = vref0;
    return clone;
}

void ExciterEXAC4::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Tr) || !std::isfinite(Vimax) || !std::isfinite(Vimin) ||
        !std::isfinite(Tc) || !std::isfinite(Tb) || !std::isfinite(Ka) || !std::isfinite(Ta) ||
        !std::isfinite(Vrmax) || !std::isfinite(Vrmin) || !std::isfinite(Kc) || (Tr <= 0.0) ||
        (Tb <= 0.0) || (Ta <= 0.0) || (Ka <= 0.0) || (Vimax < Vimin) || (Vrmax < Vrmin)) {
        throw InvalidParameterValue("EXAC4 gains, time constants, or limits");
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 3;
    offsets.local().local.algRoots = 2;
    offsets.local().local.jacSize = 19;
}

void ExciterEXAC4::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    if (!std::isfinite(inputs[exciterVoltageInLocation]) ||
        ((Kc != 0.0) && !std::isfinite(inputs[exciterXadIfdInLocation])) || desiredOutput.empty() ||
        !std::isfinite(desiredOutput[0])) {
        throw InvalidParameterValue("EXAC4 initial generator signals or field voltage");
    }
    const double fieldVoltage = desiredOutput[0];
    const double fieldCurrent = inputs[exciterXadIfdInLocation];
    const double regulator = fieldVoltage;
    const double lower = Vrmin - Kc * fieldCurrent;
    const double upper = Vrmax - Kc * fieldCurrent;
    if ((regulator < lower - 1e-7) || (regulator > upper + 1e-7)) {
        throw InvalidParameterValue("EXAC4 initial field voltage outside limits");
    }
    double* state = m_state.data() + 1;
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    state[regulatorState] = regulator;
    state[leadLagState] = regulator / Ka;
    m_state[0] = fieldVoltage;
    vref0 = inputs[exciterVoltageInLocation] + fieldVoltage / Ka;
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    updateLimitFlags(inputs, state);
}

double ExciterEXAC4::unlimitedInput(const double state[]) const
{
    return vref0 - state[voltageMeasurementState];
}
double ExciterEXAC4::limitedInput(const double state[]) const
{
    return std::clamp(unlimitedInput(state),
                      static_cast<double>(Vimin),
                      static_cast<double>(Vimax));
}
int ExciterEXAC4::inputLimitStatus(const double state[]) const
{
    const double value = unlimitedInput(state);
    return (value >= Vimax) ? 1 : ((value <= Vimin) ? -1 : 0);
}
int ExciterEXAC4::outputLimitStatus(const IOdata& inputs, const double state[]) const
{
    const double value = state[regulatorState];
    const double upper = Vrmax - Kc * inputs[exciterXadIfdInLocation];
    const double lower = Vrmin - Kc * inputs[exciterXadIfdInLocation];
    return (value >= upper) ? 1 : ((value <= lower) ? -1 : 0);
}
double ExciterEXAC4::fieldVoltage(const IOdata& inputs, const double state[]) const
{
    const int status = outputLimitStatus(inputs, state);
    return (status > 0) ?
        Vrmax - Kc * inputs[exciterXadIfdInLocation] :
        ((status < 0) ? Vrmin - Kc * inputs[exciterXadIfdInLocation] : state[regulatorState]);
}
bool ExciterEXAC4::updateLimitFlags(const IOdata& inputs, const double state[])
{
    const int input = inputLimitStatus(state);
    const int output = outputLimitStatus(inputs, state);
    const bool changed = (opFlags[INPUT_LIMITED] != (input != 0)) ||
        (opFlags[INPUT_LIMIT_HIGH] != (input > 0)) || (opFlags[OUTPUT_LIMITED] != (output != 0)) ||
        (opFlags[OUTPUT_LIMIT_HIGH] != (output > 0));
    opFlags.set(INPUT_LIMITED, input != 0);
    opFlags.set(INPUT_LIMIT_HIGH, input > 0);
    opFlags.set(OUTPUT_LIMITED, output != 0);
    opFlags.set(OUTPUT_LIMIT_HIGH, output > 0);
    return changed;
}

void ExciterEXAC4::residual(const IOdata& inputs,
                            const StateData& stateData,
                            double resid[],
                            const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode))
        locations.destLoc[0] =
            fieldVoltage(inputs, locations.diffStateLoc) - locations.algStateLoc[0];
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t ii = 0; ii < locations.diffSize; ++ii)
            locations.destDiffLoc[ii] -= locations.dstateLoc[ii];
    }
}

void ExciterEXAC4::derivative(const IOdata& inputs,
                              const StateData& stateData,
                              double deriv[],
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* derivativeValues = locations.destDiffLoc;
    const double input = limitedInput(state);
    const double lead = state[leadLagState] + Tc * (input - state[leadLagState]) / Tb;
    derivativeValues[voltageMeasurementState] =
        (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    derivativeValues[leadLagState] = (input - state[leadLagState]) / Tb;
    derivativeValues[regulatorState] = (Ka * lead - state[regulatorState]) / Ta;
}

void ExciterEXAC4::jacobianElements(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t alg = locations.algOffset;
    const index_t diff = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg, alg, -1.0);
        if (outputLimitStatus(inputs, state) == 0)
            matrixData.assign(alg, diff + regulatorState, 1.0);
        else
            matrixData.assignCheckCol(alg, inputLocs[exciterXadIfdInLocation], -Kc);
    }
    if (!hasDifferential(sMode)) return;
    matrixData.assign(diff + voltageMeasurementState,
                      diff + voltageMeasurementState,
                      -1.0 / Tr - stateData.cj);
    matrixData.assignCheckCol(diff + voltageMeasurementState,
                              inputLocs[exciterVoltageInLocation],
                              1.0 / Tr);
    const bool limited = inputLimitStatus(state) != 0;
    const double ratio = Tc / Tb;
    matrixData.assign(diff + leadLagState, diff + leadLagState, -1.0 / Tb - stateData.cj);
    if (!limited) matrixData.assign(diff + leadLagState, diff + voltageMeasurementState, -1.0 / Tb);
    matrixData.assign(diff + regulatorState, diff + leadLagState, Ka * (1.0 - ratio) / Ta);
    matrixData.assign(diff + regulatorState, diff + regulatorState, -1.0 / Ta - stateData.cj);
    if (!limited)
        matrixData.assign(diff + regulatorState, diff + voltageMeasurementState, -Ka * ratio / Ta);
}

void ExciterEXAC4::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double dt = time - prevTime;
    double* state = m_state.data() + 1;
    const double* derivativeValues = m_dstate_dt.data() + 1;
    for (index_t ii = 0; ii < 3; ++ii)
        state[ii] += dt * derivativeValues[ii];
    m_state[0] = fieldVoltage(inputs, state);
    updateLimitFlags(inputs, state);
    prevTime = time;
}

void ExciterEXAC4::rootTest(const IOdata& inputs,
                            const StateData& stateData,
                            double roots[],
                            const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const index_t root = offsets.getRootOffset(sMode);
    const double input = unlimitedInput(state);
    roots[root] = std::min(Vimax - input, input - Vimin);
    const double output = state[regulatorState];
    roots[root + 1] = std::min(Vrmax - Kc * inputs[exciterXadIfdInLocation] - output,
                               output - (Vrmin - Kc * inputs[exciterXadIfdInLocation]));
}
void ExciterEXAC4::rootTrigger(CoreTime /*time*/,
                               const IOdata& inputs,
                               const std::vector<int>& rootMask,
                               const SolverMode& sMode)
{
    const index_t root = offsets.getRootOffset(sMode);
    if (((rootMask[root] != 0) || (rootMask[root + 1] != 0)) &&
        updateLimitFlags(inputs, m_state.data() + 1))
        alert(this, JAC_COUNT_CHANGE);
}
ChangeCode ExciterEXAC4::rootCheck(const IOdata& inputs,
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
void ExciterEXAC4::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}
void ExciterEXAC4::set(std::string_view param, double val, units::unit unitType)
{
    const auto finite = [val](const char* label) {
        if (!std::isfinite(val))
            throw InvalidParameterValue(std::string("EXAC4 ") + label + " must be finite");
    };
    if (param == "tr") {
        if (!std::isfinite(val) || (val <= 0.0))
            throw InvalidParameterValue("EXAC4 TR must be positive and finite");
        Tr = val;
    } else if (param == "tb") {
        if (!std::isfinite(val) || (val <= 0.0))
            throw InvalidParameterValue("EXAC4 TB must be positive and finite");
        Tb = val;
    } else if (param == "tc") {
        finite("TC");
        Tc = val;
    } else if (param == "vimax") {
        finite("VIMAX");
        Vimax = val;
    } else if (param == "vimin") {
        finite("VIMIN");
        Vimin = val;
    } else if (param == "kc") {
        finite("KC");
        Kc = val;
    } else
        Exciter::set(param, val, unitType);
}
double ExciterEXAC4::get(std::string_view param, units::unit unitType) const
{
    if (param == "ka") return Ka;
    if (param == "ta") return Ta;
    if ((param == "vrmax") || (param == "urmax")) return Vrmax;
    if ((param == "vrmin") || (param == "urmin")) return Vrmin;
    if (param == "tr") return Tr;
    if (param == "tb") return Tb;
    if (param == "tc") return Tc;
    if (param == "vimax") return Vimax;
    if (param == "vimin") return Vimin;
    if (param == "kc") return Kc;
    return Exciter::get(param, unitType);
}
stringVec ExciterEXAC4::localStateNames() const
{
    return {"efd", "vmeas", "ll", "vr"};
}
index_t ExciterEXAC4::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) return offsets.getAlgOffset(sMode);
    const index_t offset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") return offset + voltageMeasurementState;
    if ((field == "ll") || (field == "leadlag")) return offset + leadLagState;
    if ((field == "vr") || (field == "regulator")) return offset + regulatorState;
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
