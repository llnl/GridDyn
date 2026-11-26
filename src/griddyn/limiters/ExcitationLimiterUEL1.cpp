/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterUEL1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {
const ModelDescription& ExcitationLimiterUEL1::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExcitationLimiterUEL1>(
        "ExcitationLimiterUEL1",
        &ExcitationLimiter::getModelDescription(),
        {parameter({"kur", "ku_r"}, &ExcitationLimiterUEL1::kur, units::one, "Kur"),
         parameter({"kuc", "ku_c"}, &ExcitationLimiterUEL1::kuc, units::one, "Kuc"),
         parameter({"vurmax", "vur_max"}, &ExcitationLimiterUEL1::vurMax, units::one, "Vur max"),
         parameter({"vucmax", "vuc_max"}, &ExcitationLimiterUEL1::vucMax, units::one, "Vuc max"),
         parameter({"kui", "ku_i"}, &ExcitationLimiterUEL1::kuI, units::one, "Ku i"),
         parameter({"kul", "ku_l"}, &ExcitationLimiterUEL1::kuL, units::one, "Ku l"),
         parameter({"vuimax", "vui_max"}, &ExcitationLimiterUEL1::vuiMax, units::one, "Vui max"),
         parameter({"vuimin", "vui_min"}, &ExcitationLimiterUEL1::vuiMin, units::one, "Vui min"),
         parameter({"tu1"}, &ExcitationLimiterUEL1::tu1, units::second, "Tu1"),
         parameter({"tu2"}, &ExcitationLimiterUEL1::tu2, units::second, "Tu2"),
         parameter({"tu3"}, &ExcitationLimiterUEL1::tu3, units::second, "Tu3"),
         parameter({"tu4"}, &ExcitationLimiterUEL1::tu4, units::second, "Tu4"),
         parameter({"vulmax", "vuelmax", "vul_max"},
                   &ExcitationLimiterUEL1::vulMax,
                   units::one,
                   "Vul max"),
         parameter({"vulmin", "vuelmin", "vul_min"},
                   &ExcitationLimiterUEL1::vulMin,
                   units::one,
                   "Vul min"),
         parameter({"kuf", "ku_f"}, &ExcitationLimiterUEL1::kuf, units::one, "Kuf"),
         state({"vuel"}, units::one, 0, "Solver variable vuel when active"),
         state({"integral"}, units::one, 1, "Solver variable integral when active"),
         state({"lead_lag_1"}, units::one, 2, "Solver variable lead_lag_1 when active"),
         state({"lead_lag_2"}, units::one, 3, "Solver variable lead_lag_2 when active"),
         localState("prevTime", &ExcitationLimiterUEL1::prevTime, "Prev time")});
    return description;
}

ExcitationLimiterUEL1::ExcitationLimiterUEL1(const std::string& objName):
    detail::LimiterDynamics<3>(objName)
{
    setRole(Role::UNDER);
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = 3;
    offsets.local().local.jacSize = 52;
}

CoreObject* ExcitationLimiterUEL1::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterUEL1, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->kur = kur;
        result->kuc = kuc;
        result->vurMax = vurMax;
        result->vucMax = vucMax;
        result->kuI = kuI;
        result->kuL = kuL;
        result->vuiMax = vuiMax;
        result->vuiMin = vuiMin;
        result->tu1 = tu1;
        result->tu2 = tu2;
        result->tu3 = tu3;
        result->tu4 = tu4;
        result->vulMax = vulMax;
        result->vulMin = vulMin;
        result->kuf = kuf;
        result->prevTime = 0.0;
    }
    return (result != nullptr) ? result : obj;
}

void ExcitationLimiterUEL1::set(std::string_view param, double val, units::unit unitType)
{
    if (!isSetHandled(getModelDescription().trySet(*this, param, val, unitType))) {
        ExcitationLimiter::set(param, val, unitType);
    }
}

void ExcitationLimiterUEL1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    for (const double value : {kur,
                               kuc,
                               vurMax,
                               vucMax,
                               kuI,
                               kuL,
                               vuiMax,
                               vuiMin,
                               tu1,
                               tu2,
                               tu3,
                               tu4,
                               vulMax,
                               vulMin,
                               kuf}) {
        if (!std::isfinite(value)) {
            throw InvalidParameterValue("UEL1 nonfinite parameter");
        }
    }
    if (kur <= 0.0 || kuc < 0.0 || vurMax <= 0.0 || vucMax <= 0.0 || kuI < 0.0 || kuL < 0.0 ||
        (kuI == 0.0 && kuL == 0.0) || vuiMin > 0.0 || vuiMax < 0.0 || vulMin > 0.0 ||
        vulMax < 0.0 || tu1 < 0.0 || tu2 < 0.0 || tu3 < 0.0 || tu4 < 0.0 ||
        (tu2 == 0.0 && tu1 != 0.0) || (tu4 == 0.0 && tu3 != 0.0)) {
        throw InvalidParameterValue("UEL1 gains, time constants, or signed limits");
    }
    prevTime = time0;
}

detail::Signal<3> ExcitationLimiterUEL1::characteristic(const IOdata& inputs) const
{
    if (inputs.size() < limiterMachineInputCount) {
        throw InvalidParameterValue("UEL1 input vector");
    }
    for (auto index : {limiterIdInLocation,
                       limiterIqInLocation,
                       limiterVdInLocation,
                       limiterVqInLocation,
                       limiterStabilizingFeedbackInLocation}) {
        if (index == limiterStabilizingFeedbackInLocation && kuf == 0.0) {
            continue;
        }
        const auto inputIndex = static_cast<IOdata::size_type>(index);
        if (inputs.size() <= inputIndex || !std::isfinite(inputs[inputIndex]) ||
            std::abs(inputs[inputIndex]) > 1e20) {
            throw InvalidParameterValue(
                "UEL1 requires compatible machine and excitation feedback signals");
        }
    }
    const auto directCurrent = detail::input<3>(inputs, limiterIdInLocation);
    const auto quadratureCurrent = detail::input<3>(inputs, limiterIqInLocation);
    const auto directVoltage = detail::input<3>(inputs, limiterVdInLocation);
    const auto quadratureVoltage = detail::input<3>(inputs, limiterVqInLocation);
    // VUC = |KUC*VT-j*IT| in GridDyn's machine dq convention.
    const auto characteristicVoltage = detail::clamp(
        detail::magnitude(detail::add(detail::scale(directVoltage, kuc), quadratureCurrent),
                          detail::subtract(detail::scale(quadratureVoltage, kuc), directCurrent)),
        0.0,
        vucMax);
    const auto referenceVoltage =
        detail::clamp(detail::scale(detail::magnitude(directVoltage, quadratureVoltage), kur),
                      0.0,
                      vurMax);
    auto error = detail::subtract(characteristicVoltage, referenceVoltage);
    if (kuf != 0.0) {
        error = detail::subtract(
            error,
            detail::scale(detail::input<3>(inputs, limiterStabilizingFeedbackInLocation), kuf));
    }
    return error;
}

detail::Evaluation<3> ExcitationLimiterUEL1::evaluate(const IOdata& inputs,
                                                      const double states[]) const
{
    detail::Evaluation<3> result;
    const auto error = characteristic(inputs);
    const auto integral = detail::state<3>(states[0], 0);
    const auto rawPI = detail::add(detail::scale(error, kuL), integral);
    const auto limitedPI = detail::clamp(rawPI, vuiMin, vuiMax);
    // The limits apply to the PI output, not just to its integral state.
    result.mRates[0] = detail::limitedRate(detail::scale(error, kuI), rawPI.mValue, vuiMin, vuiMax);
    const auto lag1 = detail::state<3>(states[1], 1);
    const double alpha1 = tu2 > 0.0 ? tu1 / tu2 : 1.0;
    const auto first =
        detail::add(detail::scale(limitedPI, alpha1), detail::scale(lag1, 1.0 - alpha1));
    if (tu2 > 0.0) {
        result.mRates[1] = detail::scale(detail::subtract(limitedPI, lag1), 1.0 / tu2);
    }
    const auto lag2 = detail::state<3>(states[2], 2);
    const double alpha2 = tu4 > 0.0 ? tu3 / tu4 : 1.0;
    if (tu4 > 0.0) {
        result.mRates[2] = detail::scale(detail::subtract(first, lag2), 1.0 / tu4);
    }
    result.mOutput =
        detail::clamp(detail::add(detail::scale(first, alpha2), detail::scale(lag2, 1.0 - alpha2)),
                      vulMin,
                      vulMax);
    return result;
}

void ExcitationLimiterUEL1::dynObjectInitializeB(const IOdata& inputs,
                                                 const IOdata& /*desiredOutput*/,
                                                 IOdata& fieldSet)
{
    m_state[1] = 0.0;
    m_state[2] = std::clamp(kuL * characteristic(inputs).mValue, vuiMin, vuiMax);
    m_state[3] = m_state[2];
    m_state[0] = evaluate(inputs, m_state.data() + 1).mOutput.mValue;
    fieldSet = {m_state[0]};
}

void ExcitationLimiterUEL1::timestep(CoreTime time,
                                     const IOdata& inputs,
                                     const SolverMode& /*mode*/)
{
    const auto result = evaluate(inputs, m_state.data() + 1);
    const double step = time - prevTime;
    for (index_t index = 0; index < 3; ++index) {
        m_state[index + 1] += step * result.mRates[index].mValue;
    }
    m_state[0] = evaluate(inputs, m_state.data() + 1).mOutput.mValue;
    prevTime = time;
}
}  // namespace griddyn::limiters
