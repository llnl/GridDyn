/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "StabilizerPss2b.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::stabilizers {
StabilizerPss2b::StabilizerPss2b(const std::string& objName): StabilizerPss2a(objName)
{
    mode2 = 3;
    T10 = 1e-9;
    T11 = 0.033;
}

const ModelDescription& StabilizerPss2b::getModelDescription() const
{
    static const auto description = ModelDescription::define<StabilizerPss2b>(
        "StabilizerPss2b",
        &StabilizerPss2a::getModelDescription(),
        {parameter({"m"},
                   &StabilizerPss2b::rampM,
                   units::one,
                   "rampM gain",
                   [](int value) { return value >= 0 && value <= 8; }),
         parameter({"n"},
                   &StabilizerPss2b::rampN,
                   units::one,
                   "rampN gain",
                   [](int value) { return value >= 0 && value <= 8; }),
         parameter({"t10"},
                   &StabilizerPss2b::T10,
                   units::second,
                   "T10 time constant",
                   parameterValidators::nonnegative),
         parameter({"t11"},
                   &StabilizerPss2b::T11,
                   units::second,
                   "T11 time constant",
                   parameterValidators::nonnegative),
         parameter({"vs1max"}, &StabilizerPss2b::Vs1max, units::one, "Vs1max signal limit"),
         parameter({"vs1min"}, &StabilizerPss2b::Vs1min, units::one, "Vs1min signal limit"),
         parameter({"vs2max"}, &StabilizerPss2b::Vs2max, units::one, "Vs2max signal limit"),
         parameter({"vs2min"}, &StabilizerPss2b::Vs2min, units::one, "Vs2min signal limit"),
         parameter({"t9"},
                   &StabilizerPss2b::T9,
                   units::second,
                   "T9 time constant",
                   parameterValidators::nonnegative)});
    return description;
}

CoreObject* StabilizerPss2b::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<StabilizerPss2b, StabilizerPss2a>(this, obj);
    if (copy == nullptr) {
        return obj;
    }
    copy->Vs1max = Vs1max;
    copy->Vs1min = Vs1min;
    copy->Vs2max = Vs2max;
    copy->Vs2min = Vs2min;
    return copy;
}

void StabilizerPss2b::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(Vs1max) || !std::isfinite(Vs1min) || !std::isfinite(Vs2max) ||
        !std::isfinite(Vs2min) || Vs1max < Vs1min || Vs2max < Vs2min) {
        throw InvalidParameterValue("PSS2B input limits");
    }
    StabilizerPss2a::dynObjectInitializeA(time0, flags);
    offsets.local().local.algRoots += 4;
}

StabilizerPss2a::LinearValue
    StabilizerPss2b::selectedInput(const IOdata& inputs, int mode, bool first) const
{
    auto signal = StabilizerPss2a::selectedInput(inputs, mode, first);
    const double lower = first ? Vs1min : Vs2min;
    const double upper = first ? Vs1max : Vs2max;
    if (signal.value <= lower || signal.value >= upper) {
        signal.value = std::clamp(signal.value, lower, upper);
        signal.input1Gain = 0.0;
        signal.input2Gain = 0.0;
    }
    return signal;
}

void StabilizerPss2b::dynObjectInitializeB(const IOdata& inputs,
                                           const IOdata& desiredOutput,
                                           IOdata& fieldSet)
{
    StabilizerPss2a::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    auto* state = m_state.data() + 1;
    const double firstInput = selectedInput(inputs, mode1, true).value;
    const double secondInput = selectedInput(inputs, mode2, false).value;
    // Positive washout times reject DC. Fully bypassed washout branches pass
    // the clipped input, so the remaining lags must start at that DC output.
    const double first = (Tw1 > 0.0 || Tw2 > 0.0) ? 0.0 : firstInput;
    const double second = Ks2 * ((Tw3 > 0.0 || Tw4 > 0.0) ? 0.0 : secondInput);
    if (branch1LagState != kNullLocation) {
        state[branch1LagState] = first;
    }
    if (branch2LagState != kNullLocation) {
        state[branch2LagState] = second;
    }
    const double summed = first + (Ks3 * second);
    for (index_t stage = 0; stage < rampStateCount(); ++stage) {
        state[rampFilterState[stage]] = summed;
    }
    const double compensated = Ks1 * (summed - second);
    for (const auto index : {leadLag1State, leadLag2State, leadLag3State}) {
        if (index != kNullLocation) {
            state[index] = compensated;
        }
    }
    m_state[0] = std::clamp(compensated, Vstmin, Vstmax);
    updateLimitFlags(inputs, state);
}

void StabilizerPss2b::algebraicUpdate(const IOdata& inputs,
                                      const StateData& stateData,
                                      double update[],
                                      const SolverMode& sMode,
                                      double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[0] = output(inputs, loc.diffStateLoc);
    }
}

void StabilizerPss2b::rootTest(const IOdata& inputs,
                               const StateData& stateData,
                               double roots[],
                               const SolverMode& sMode)
{
    StabilizerPss2a::rootTest(inputs, stateData, roots, sMode);
    const auto offset = offsets.getRootOffset(sMode) + 2;
    const double first = StabilizerPss2a::selectedInput(inputs, mode1, true).value;
    const double second = StabilizerPss2a::selectedInput(inputs, mode2, false).value;
    roots[offset] = Vs1max - first + 1e-4;
    roots[offset + 1] = first - Vs1min + 1e-4;
    roots[offset + 2] = Vs2max - second + 1e-4;
    roots[offset + 3] = second - Vs2min + 1e-4;
}

void StabilizerPss2b::rootTrigger(CoreTime time,
                                  const IOdata& inputs,
                                  const std::vector<int>& rootMask,
                                  const SolverMode& sMode)
{
    StabilizerPss2a::rootTrigger(time, inputs, rootMask, sMode);
    const auto offset = offsets.getRootOffset(sMode) + 2;
    for (index_t rootIndex = 0; rootIndex < 4; ++rootIndex) {
        if (rootMask[offset + rootIndex] != 0) {
            alert(this, JAC_COUNT_CHANGE);
            break;
        }
    }
}

void StabilizerPss2b::set(std::string_view param, double val, units::unit unitType)
{
    if (!StabilizerPss2b::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        StabilizerPss2a::set(param, val, unitType);
    }
}

double StabilizerPss2b::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            StabilizerPss2b::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return StabilizerPss2a::get(param, unitType);
}
}  // namespace griddyn::stabilizers
