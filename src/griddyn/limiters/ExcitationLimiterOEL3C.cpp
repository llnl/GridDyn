/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterOEL3C.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::limiters {

const ModelDescription& ExcitationLimiterOEL3C::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExcitationLimiterOEL3C>(
        "ExcitationLimiterOEL3C",
        &ExcitationLimiter::getModelDescription(),
        {parameter({"itfpu", "itf_pu"}, &ExcitationLimiterOEL3C::itfpu, units::one, "Itfpu"),
         parameter({"kscale", "k_scale"}, &ExcitationLimiterOEL3C::kscale, units::one, "Kscale"),
         parameter({"tf"}, &ExcitationLimiterOEL3C::tf, units::second, "Tf"),
         parameter({"k1"},
                   &ExcitationLimiterOEL3C::k1,
                   units::one,
                   "K1",
                   [](int value) { return value == 1 || value == 2; }),
         parameter({"koel", "k_oel"}, &ExcitationLimiterOEL3C::koel, units::one, "Koel"),
         parameter({"toel", "t_oel"}, &ExcitationLimiterOEL3C::toel, units::second, "Toel"),
         parameter({"kpoel", "kp_oel"}, &ExcitationLimiterOEL3C::kpoel, units::one, "Kpoel"),
         parameter(
             {"voelmax1", "voel_max1"}, &ExcitationLimiterOEL3C::voelMax1, units::one, "Voel max1"),
         parameter(
             {"voelmin1", "voel_min1"}, &ExcitationLimiterOEL3C::voelMin1, units::one, "Voel min1"),
         parameter(
             {"voelmax2", "voel_max2"}, &ExcitationLimiterOEL3C::voelMax2, units::one, "Voel max2"),
         parameter(
             {"voelmin2", "voel_min2"}, &ExcitationLimiterOEL3C::voelMin2, units::one, "Voel min2"),
         parameter({"oelinput", "oel_input"},
                   &ExcitationLimiterOEL3C::oelInput,
                   units::one,
                   "OEL input selector",
                   [](int value) { return value >= 0 && value <= 2; }),
         state({"voel"}, units::one, 0, "Solver variable voel when active"),
         state({"field_current_filter"},
               units::one,
               1,
               "Solver variable field_current_filter when active"),
         state({"integral"}, units::one, 2, "Solver variable integral when active"),
         localState("prevTime", &ExcitationLimiterOEL3C::prevTime, "Prev time")});
    return description;
}

ExcitationLimiterOEL3C::ExcitationLimiterOEL3C(const std::string& objName):
    detail::LimiterDynamics<2>(objName)
{
    setRole(Role::OVER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 2;
    offsets.local().local.algRoots = 1;
    offsets.local().local.jacSize = 36;
}

CoreObject* ExcitationLimiterOEL3C::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterOEL3C, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->itfpu = itfpu;
        result->kscale = kscale;
        result->tf = tf;
        result->k1 = k1;
        result->oelInput = oelInput;
        result->koel = koel;
        result->toel = toel;
        result->kpoel = kpoel;
        result->voelMax1 = voelMax1;
        result->voelMin1 = voelMin1;
        result->voelMax2 = voelMax2;
        result->voelMin2 = voelMin2;
        result->prevTime = 0.0;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterOEL3C::set(std::string_view param, double val, units::unit unitType)
{
    if (!isSetHandled(getModelDescription().trySet(*this, param, val, unitType))) {
        ExcitationLimiter::set(param, val, unitType);
    }
}

double ExcitationLimiterOEL3C::get(std::string_view param, units::unit unitType) const
{
    if (const auto value = getModelDescription().tryGet(*this, param, unitType)) {
        return *value;
    }
    return ExcitationLimiter::get(param, unitType);
}

void ExcitationLimiterOEL3C::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    for (const double value :
         {itfpu, kscale, tf, koel, toel, kpoel, voelMax1, voelMin1, voelMax2, voelMin2}) {
        if (!std::isfinite(value)) {
            throw InvalidParameterValue("OEL3C nonfinite parameter");
        }
    }
    if (itfpu <= 0.0 || kscale <= 0.0 || tf < 0.0 || koel <= 0.0 || toel <= 0.0 || kpoel < 0.0 ||
        voelMax1 < 0.0 || voelMin1 > 0.0 || voelMax2 < 0.0 || voelMin2 > 0.0) {
        throw InvalidParameterValue("OEL3C gains, time constants, or signed limits");
    }
    offsets.local().local.algRoots = 1;
    proportionalActive = false;
    prevTime = time0;
}

index_t ExcitationLimiterOEL3C::measurementLocation() const
{
    if (oelInput == 0) {
        return limiterFieldCurrentInLocation;
    }
    if (oelInput == 1) {
        return limiterFieldVoltageInLocation;
    }
    return limiterExciterFieldCurrentInLocation;
}

double ExcitationLimiterOEL3C::measurement(const IOdata& inputs) const
{
    const auto index = measurementLocation();
    if (inputs.size() <= index || !std::isfinite(inputs[index]) || std::abs(inputs[index]) > 1e20) {
        throw InvalidParameterValue("OEL3C selected Ifd/Efd/VFE measurement is unavailable");
    }
    return inputs[index];
}

detail::Evaluation<2> ExcitationLimiterOEL3C::evaluate(const IOdata& inputs,
                                                       const double states[]) const
{
    measurement(inputs);
    detail::Evaluation<2> result;
    const auto index = measurementLocation();
    const auto measured = tf > 0.0 ? detail::state<2>(states[0], 0) :
                                     detail::scale(detail::input<2>(inputs, index), kscale);
    if (tf > 0.0) {
        result.mRates[0] =
            detail::scale(detail::subtract(detail::scale(detail::input<2>(inputs, index), kscale),
                                           measured),
                          1.0 / tf);
    }
    // IEEE applies K1 to both the rated pickup and the scaled measurement.
    // Retaining negative error lets the integrator recover after pickup clears.
    const auto error = detail::scale(detail::subtract(detail::constant<2>(std::pow(itfpu, k1)),
                                                      detail::power(measured, k1)),
                                     koel);
    const auto integral = detail::state<2>(states[1], 1);
    result.mRates[1] =
        detail::limitedRate(detail::scale(error, 1.0 / toel), integral.mValue, voelMin1, voelMax1);
    // Keep the selected SW1 branch fixed during a solver step. The root
    // handler changes it when VIOEL crosses zero, then the solver can
    // reinitialize the discontinuous proportional output consistently.
    const auto proportional =
        proportionalActive ? detail::scale(error, kpoel) : detail::constant<2>(0.0);
    result.mOutput =
        detail::scale(detail::clamp(detail::add(integral, proportional), voelMin2, voelMax2), -1.0);
    return result;
}

void ExcitationLimiterOEL3C::dynObjectInitializeB(const IOdata& inputs,
                                                  const IOdata& /*desiredOutput*/,
                                                  IOdata& fieldSet)
{
    m_state[1] = kscale * measurement(inputs);
    m_state[2] = std::pow(m_state[1], k1) <= std::pow(itfpu, k1) ? voelMax1 : 0.0;
    proportionalActive = m_state[2] < 0.0;
    m_state[0] = evaluate(inputs, m_state.data() + 1).mOutput.mValue;
    fieldSet = {m_state[0]};
}

void ExcitationLimiterOEL3C::timestep(CoreTime time,
                                      const IOdata& inputs,
                                      const SolverMode& /*mode*/)
{
    const auto result = evaluate(inputs, m_state.data() + 1);
    const double step = time - prevTime;
    m_state[1] += step * result.mRates[0].mValue;
    m_state[2] = std::clamp(m_state[2] + (step * result.mRates[1].mValue), voelMin1, voelMax1);
    proportionalActive = m_state[2] < 0.0;
    m_state[0] = evaluate(inputs, m_state.data() + 1).mOutput.mValue;
    prevTime = time;
}

void ExcitationLimiterOEL3C::rootTest(const IOdata& /*inputs*/,
                                      const StateData& data,
                                      double roots[],
                                      const SolverMode& mode)
{
    const auto root = offsets.getRootOffset(mode);
    if (root != kNullLocation) {
        const auto loc = offsets.getLocations(data, mode, this);
        // Rearm on the far side of zero to avoid repeated roots at an inactive
        // initialized switch.
        roots[root] = loc.diffStateLoc[1] + (proportionalActive ? -1e-8 : 1e-8);
    }
}

void ExcitationLimiterOEL3C::rootTrigger(CoreTime /*time*/,
                                         const IOdata& /*inputs*/,
                                         const std::vector<int>& rootMask,
                                         const SolverMode& mode)
{
    const auto root = offsets.getRootOffset(mode);
    if (root != kNullLocation && rootMask[root] != 0) {
        proportionalActive = rootMask[root] < 0;
    }
}

ChangeCode ExcitationLimiterOEL3C::rootCheck(const IOdata& /*inputs*/,
                                             const StateData& data,
                                             const SolverMode& mode,
                                             CheckLevel /*level*/)
{
    const auto loc = offsets.getLocations(data, mode, this);
    const bool active = loc.diffStateLoc[1] < 0.0;
    if (active != proportionalActive) {
        proportionalActive = active;
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

}  // namespace griddyn::limiters
