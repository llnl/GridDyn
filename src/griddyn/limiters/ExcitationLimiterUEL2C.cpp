/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExcitationLimiterUEL2C.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::limiters {
namespace {
    constexpr index_t integralState = 0;
    constexpr index_t powerState = 1;
    constexpr index_t reactiveState = 2;
    constexpr index_t voltageState = 3;
    constexpr index_t referenceState = 4;
    constexpr index_t gainState = 5;
    constexpr index_t feedbackState = 6;
    constexpr index_t firstLagState = 7;
    constexpr index_t secondLagState = 8;

    detail::Signal<9> applyVoltageBias(const detail::Signal<9>& voltage, double bias)
    {
        if (voltage.mValue > 1.0) {
            return voltage;
        }
        if (voltage.mValue > bias) {
            return detail::constant<9>(1.0);
        }
        return detail::scale(voltage, 1.0 / bias);
    }
}  // namespace

const ModelDescription& ExcitationLimiterUEL2C::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExcitationLimiterUEL2C>(
        "ExcitationLimiterUEL2C",
        &ExcitationLimiter::getModelDescription(),
        {parameter({"tup"}, &ExcitationLimiterUEL2C::tup, units::second, "tup"),
         parameter({"tuq"}, &ExcitationLimiterUEL2C::tuq, units::second, "tuq"),
         parameter({"tuv"}, &ExcitationLimiterUEL2C::tuv, units::second, "tuv"),
         parameter({"vbias"}, &ExcitationLimiterUEL2C::vBias, units::one, "vbias"),
         parameter({"kuf"}, &ExcitationLimiterUEL2C::kuf, units::one, "kuf"),
         parameter({"tqref"}, &ExcitationLimiterUEL2C::tqref, units::second, "tqref"),
         parameter({"kfix"}, &ExcitationLimiterUEL2C::kfix, units::one, "kfix"),
         parameter({"tadj"}, &ExcitationLimiterUEL2C::tadj, units::second, "tadj"),
         parameter({"kui"}, &ExcitationLimiterUEL2C::kuI, units::one, "kui"),
         parameter({"kul"}, &ExcitationLimiterUEL2C::kuL, units::one, "kul"),
         parameter({"vuimax"}, &ExcitationLimiterUEL2C::vuiMax, units::one, "vuimax"),
         parameter({"vuimin"}, &ExcitationLimiterUEL2C::vuiMin, units::one, "vuimin"),
         parameter({"tu1"}, &ExcitationLimiterUEL2C::tu1, units::second, "tu1"),
         parameter({"tu2"}, &ExcitationLimiterUEL2C::tu2, units::second, "tu2"),
         parameter({"tu3"}, &ExcitationLimiterUEL2C::tu3, units::second, "tu3"),
         parameter({"tu4"}, &ExcitationLimiterUEL2C::tu4, units::second, "tu4"),
         parameter({"kfb"}, &ExcitationLimiterUEL2C::kfb, units::one, "kfb"),
         parameter({"tul"}, &ExcitationLimiterUEL2C::tul, units::second, "tul"),
         parameter({"xq"}, &ExcitationLimiterUEL2C::xq, units::one, "xq"),
         parameter({"vulmax1", "vuelmax1", "vuelmax"},
                   &ExcitationLimiterUEL2C::vulMax1,
                   units::one,
                   "vulmax1"),
         parameter({"vulmin1", "vuelmin1", "vuelmin"},
                   &ExcitationLimiterUEL2C::vulMin1,
                   units::one,
                   "vulmin1"),
         parameter(
             {"vulmax2", "vuelmax2"}, &ExcitationLimiterUEL2C::vulMax2, units::one, "vulmax2"),
         parameter(
             {"vulmin2", "vuelmin2"}, &ExcitationLimiterUEL2C::vulMin2, units::one, "vulmin2"),
         parameter({"k1"},
                   &ExcitationLimiterUEL2C::k1,
                   units::one,
                   "k1",
                   [](int value) { return value >= 0 && value <= 2; }),
         parameter({"k2"},
                   &ExcitationLimiterUEL2C::k2,
                   units::one,
                   "k2",
                   [](int value) { return value >= 0 && value <= 2; }),
         parameter({"sw1"},
                   &ExcitationLimiterUEL2C::sw1,
                   units::one,
                   "Gain switch",
                   [](int value) { return value == 1 || value == 2; }),
         parameter({"p0"}, &ExcitationLimiterUEL2C::pPoints, 0, units::one, "Curve p0"),
         parameter({"p1"}, &ExcitationLimiterUEL2C::pPoints, 1, units::one, "Curve p1"),
         parameter({"p2"}, &ExcitationLimiterUEL2C::pPoints, 2, units::one, "Curve p2"),
         parameter({"p3"}, &ExcitationLimiterUEL2C::pPoints, 3, units::one, "Curve p3"),
         parameter({"p4"}, &ExcitationLimiterUEL2C::pPoints, 4, units::one, "Curve p4"),
         parameter({"p5"}, &ExcitationLimiterUEL2C::pPoints, 5, units::one, "Curve p5"),
         parameter({"p6"}, &ExcitationLimiterUEL2C::pPoints, 6, units::one, "Curve p6"),
         parameter({"p7"}, &ExcitationLimiterUEL2C::pPoints, 7, units::one, "Curve p7"),
         parameter({"p8"}, &ExcitationLimiterUEL2C::pPoints, 8, units::one, "Curve p8"),
         parameter({"p9"}, &ExcitationLimiterUEL2C::pPoints, 9, units::one, "Curve p9"),
         parameter({"q0"}, &ExcitationLimiterUEL2C::qPoints, 0, units::one, "Curve q0"),
         parameter({"q1"}, &ExcitationLimiterUEL2C::qPoints, 1, units::one, "Curve q1"),
         parameter({"q2"}, &ExcitationLimiterUEL2C::qPoints, 2, units::one, "Curve q2"),
         parameter({"q3"}, &ExcitationLimiterUEL2C::qPoints, 3, units::one, "Curve q3"),
         parameter({"q4"}, &ExcitationLimiterUEL2C::qPoints, 4, units::one, "Curve q4"),
         parameter({"q5"}, &ExcitationLimiterUEL2C::qPoints, 5, units::one, "Curve q5"),
         parameter({"q6"}, &ExcitationLimiterUEL2C::qPoints, 6, units::one, "Curve q6"),
         parameter({"q7"}, &ExcitationLimiterUEL2C::qPoints, 7, units::one, "Curve q7"),
         parameter({"q8"}, &ExcitationLimiterUEL2C::qPoints, 8, units::one, "Curve q8"),
         parameter({"q9"}, &ExcitationLimiterUEL2C::qPoints, 9, units::one, "Curve q9"),
         localState("pointCount", &ExcitationLimiterUEL2C::pointCount, "Point count"),
         localState("prevTime", &ExcitationLimiterUEL2C::prevTime, "Previous time"),
         state({"vuel"}, units::one, 0, "vuel"),
         state({"integral"}, units::one, 1, "integral"),
         state({"power_filter"}, units::one, 2, "power_filter"),
         state({"reactive_filter"}, units::one, 3, "reactive_filter"),
         state({"voltage_filter"}, units::one, 4, "voltage_filter"),
         state({"reference_filter"}, units::one, 5, "reference_filter"),
         state({"gain_filter"}, units::one, 6, "gain_filter"),
         state({"feedback_filter"}, units::one, 7, "feedback_filter"),
         state({"lead_lag_1"}, units::one, 8, "lead_lag_1"),
         state({"lead_lag_2"}, units::one, 9, "lead_lag_2")});
    return description;
}

ExcitationLimiterUEL2C::ExcitationLimiterUEL2C(const std::string& name):
    detail::LimiterDynamics<9>(name)
{
    setRole(Role::UNDER);
}

CoreObject* ExcitationLimiterUEL2C::clone(CoreObject* obj) const
{
    auto* result = cloneBase<ExcitationLimiterUEL2C, ExcitationLimiter>(this, obj);
    if (result != nullptr) {
        result->tup = tup;
        result->tuq = tuq;
        result->tuv = tuv;
        result->vBias = vBias;
        result->kuf = kuf;
        result->tqref = tqref;
        result->kfix = kfix;
        result->tadj = tadj;
        result->kuI = kuI;
        result->kuL = kuL;
        result->vuiMax = vuiMax;
        result->vuiMin = vuiMin;
        result->tu1 = tu1;
        result->tu2 = tu2;
        result->tu3 = tu3;
        result->tu4 = tu4;
        result->kfb = kfb;
        result->tul = tul;
        result->xq = xq;
        result->vulMax1 = vulMax1;
        result->vulMin1 = vulMin1;
        result->vulMax2 = vulMax2;
        result->vulMin2 = vulMin2;
        result->k1 = k1;
        result->k2 = k2;
        result->sw1 = sw1;
        result->pPoints = pPoints;
        result->qPoints = qPoints;
        result->pointCount = pointCount;
        result->pConfigured = pConfigured;
        result->qConfigured = qConfigured;
        result->prevTime = 0.0;
    }
    return result != nullptr ? result : obj;
}

void ExcitationLimiterUEL2C::set(std::string_view param, double val, units::unit unitType)
{
    if (!isSetHandled(getModelDescription().trySet(*this, param, val, unitType))) {
        ExcitationLimiter::set(param, val, unitType);
        return;
    }
    if (param.size() == 2 && (param.front() == 'p' || param.front() == 'q') && param[1] >= '0' &&
        param[1] <= '9') {
        const auto index = static_cast<std::size_t>(param[1] - '0');
        (param.front() == 'p' ? pConfigured : qConfigured).set(index);
        pointCount = std::max(pointCount, index + 1);
    }
}

void ExcitationLimiterUEL2C::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    for (const double value :
         {tup, tuq, tuv, vBias, kuf,     tqref,   kfix,    tadj,    kuI, kuL, vuiMax, vuiMin,
          tu1, tu2, tu3, tu4,   vulMax1, vulMin1, vulMax2, vulMin2, kfb, tul, xq}) {
        if (!std::isfinite(value)) {
            throw InvalidParameterValue("UEL2C nonfinite parameter");
        }
    }
    if (tup < 0.0 || tuq < 0.0 || tuv < 0.0 || vBias <= 0.0 || tqref < 0.0 || kfix <= 0.0 ||
        kfix > 1.0 || tadj < 0.0 || kuI < 0.0 || kuL < 0.0 || (kuI == 0.0 && kuL == 0.0) ||
        vuiMin > 0.0 || vuiMax < 0.0 || tu1 < 0.0 || tu2 < 0.0 || tu3 < 0.0 || tu4 < 0.0 ||
        tul < 0.0 || xq <= 0.0 || (tu2 == 0.0 && tu1 != 0.0) || (tu4 == 0.0 && tu3 != 0.0) ||
        vulMin1 > 0.0 || vulMax1 < 0.0 || vulMin2 > 0.0 || vulMax2 < 0.0 || pointCount < 2 ||
        pointCount > pPoints.size()) {
        throw InvalidParameterValue("UEL2C gains, time constants, signed limits, or curve");
    }
    for (std::size_t index = 0; index < pointCount; ++index) {
        if (!pConfigured[index] || !qConfigured[index] || !std::isfinite(pPoints[index]) ||
            !std::isfinite(qPoints[index]) || (index > 0 && pPoints[index] <= pPoints[index - 1])) {
            throw InvalidParameterValue("UEL2C requires complete, finite curve pairs ordered by P");
        }
    }
    prevTime = time0;
}

ExcitationLimiterUEL2C::CurveEvaluation ExcitationLimiterUEL2C::curve(double normalizedP) const
{
    if (normalizedP <= pPoints[0]) {
        return {.qLimit = qPoints[0]};
    }
    if (normalizedP >= pPoints[pointCount - 1]) {
        return {.qLimit = qPoints[pointCount - 1]};
    }
    for (std::size_t index = 1; index < pointCount; ++index) {
        if (normalizedP <= pPoints[index]) {
            const double slope =
                (qPoints[index] - qPoints[index - 1]) / (pPoints[index] - pPoints[index - 1]);
            return {.qLimit = qPoints[index - 1] + (slope * (normalizedP - pPoints[index - 1])),
                    .slope = slope};
        }
    }
    return {.qLimit = qPoints[pointCount - 1]};
}

detail::Evaluation<9> ExcitationLimiterUEL2C::evaluate(const IOdata& inputs,
                                                       const double states[]) const
{
    if (inputs.size() < limiterMachineInputCount) {
        throw InvalidParameterValue("UEL2C input vector");
    }
    for (auto index : {limiterIdInLocation,
                       limiterIqInLocation,
                       limiterVdInLocation,
                       limiterVqInLocation,
                       limiterStabilizingFeedbackInLocation,
                       limiterExciterFeedbackInLocation}) {
        if ((index == limiterStabilizingFeedbackInLocation && kuf == 0.0) ||
            (index == limiterExciterFeedbackInLocation && kfb == 0.0)) {
            continue;
        }
        if (inputs.size() <= index || !std::isfinite(inputs[index]) ||
            std::abs(inputs[index]) > 1e20) {
            throw InvalidParameterValue(
                "UEL2C requires compatible machine and excitation feedback signals");
        }
    }
    detail::Evaluation<9> result;
    const auto directCurrent = detail::input<9>(inputs, limiterIdInLocation);
    const auto quadratureCurrent = detail::input<9>(inputs, limiterIqInLocation);
    const auto directVoltage = detail::input<9>(inputs, limiterVdInLocation);
    const auto quadratureVoltage = detail::input<9>(inputs, limiterVqInLocation);
    const auto realPower = detail::add(detail::multiply(directCurrent, directVoltage),
                                       detail::multiply(quadratureCurrent, quadratureVoltage));
    const auto reactivePower = detail::subtract(detail::multiply(directCurrent, quadratureVoltage),
                                                detail::multiply(quadratureCurrent, directVoltage));
    const auto voltage = detail::magnitude(directVoltage, quadratureVoltage);
    auto lag = [&](const detail::Signal<9>& drive, index_t index, double timeConstant) {
        if (timeConstant == 0.0) {
            return drive;
        }
        const auto filtered = detail::state<9>(states[index], index);
        result.mRates[index] = detail::scale(detail::subtract(drive, filtered), 1.0 / timeConstant);
        return filtered;
    };
    const auto filteredVoltage = lag(voltage, voltageState, tuv);
    const auto biasVoltage = applyVoltageBias(filteredVoltage, vBias);
    // Regularize only the inverse-voltage divisor at a collapsed terminal.
    const auto divisor = biasVoltage.mValue > 1e-6 ? biasVoltage : detail::constant<9>(1e-6);
    const auto normalizedP =
        lag(detail::multiply(realPower, detail::reciprocal(detail::power(divisor, k1))),
            powerState,
            tup);
    const auto lookup = curve(normalizedP.mValue);
    const auto tableQ = detail::function(normalizedP, lookup.qLimit, lookup.slope);
    const auto reference =
        lag(detail::multiply(tableQ, detail::power(biasVoltage, k2)), referenceState, tqref);
    auto error = detail::subtract(reference, lag(reactivePower, reactiveState, tuq));
    if (kuf != 0.0) {
        error = detail::subtract(
            error,
            detail::scale(detail::input<9>(inputs, limiterStabilizingFeedbackInLocation), kuf));
    }
    auto gainReduction = detail::constant<9>(kfix);
    if (sw1 == 2) {
        const auto axis =
            detail::add(detail::scale(detail::power(voltage, 2), 1.0 / xq), reactivePower);
        const auto norm = detail::magnitude(axis, realPower);
        const auto target = norm.mValue > 1e-14 ? detail::multiply(axis, detail::reciprocal(norm)) :
                                                  detail::constant<9>(0.0);
        if (tadj > 0.0) {
            const auto filtered = detail::state<9>(states[gainState], gainState);
            result.mRates[gainState] =
                detail::limitedRate(detail::scale(detail::subtract(target, filtered), 1.0 / tadj),
                                    filtered.mValue,
                                    0.1,
                                    1.0);
            gainReduction = detail::clamp(filtered, 0.1, 1.0);
        } else {
            gainReduction = detail::clamp(target, 0.1, 1.0);
        }
    }
    const auto piError = detail::multiply(error, gainReduction);
    const auto rawPI = detail::add(detail::scale(piError, kuL),
                                   detail::state<9>(states[integralState], integralState));
    result.mRates[integralState] =
        detail::limitedRate(detail::scale(piError, kuI), rawPI.mValue, vuiMin, vuiMax);
    auto limitedPI = detail::clamp(rawPI, vuiMin, vuiMax);
    if (kfb != 0.0) {
        limitedPI = detail::subtract(
            limitedPI,
            lag(detail::scale(detail::input<9>(inputs, limiterExciterFeedbackInLocation), kfb),
                feedbackState,
                tul));
    }
    const auto firstLag = lag(limitedPI, firstLagState, tu2);
    const double alpha1 = tu2 > 0.0 ? tu1 / tu2 : 1.0;
    const auto first = detail::clamp(detail::add(detail::scale(limitedPI, alpha1),
                                                 detail::scale(firstLag, 1.0 - alpha1)),
                                     vulMin2,
                                     vulMax2);
    const auto secondLag = lag(first, secondLagState, tu4);
    const double alpha2 = tu4 > 0.0 ? tu3 / tu4 : 1.0;
    result.mOutput = detail::clamp(detail::add(detail::scale(first, alpha2),
                                               detail::scale(secondLag, 1.0 - alpha2)),
                                   vulMin1,
                                   vulMax1);
    return result;
}

void ExcitationLimiterUEL2C::dynObjectInitializeB(const IOdata& inputs,
                                                  const IOdata& /*desiredOutput*/,
                                                  IOdata& fieldSet)
{
    // Seed each filter at its input in diagram order. Bypassed filters retain
    // a harmless state but always evaluate their current input directly.
    std::fill(m_state.begin(), m_state.end(), 0.0);
    const double directVoltage = inputs.at(limiterVdInLocation);
    const double quadratureVoltage = inputs.at(limiterVqInLocation);
    const double directCurrent = inputs.at(limiterIdInLocation);
    const double quadratureCurrent = inputs.at(limiterIqInLocation);
    const double voltage = std::hypot(directVoltage, quadratureVoltage);
    const double realPower =
        (directCurrent * directVoltage) + (quadratureCurrent * quadratureVoltage);
    const double reactivePower =
        (directCurrent * quadratureVoltage) - (quadratureCurrent * directVoltage);
    const double bias = applyVoltageBias(detail::constant<9>(voltage), vBias).mValue;
    const double normalizedP = realPower / std::pow(std::max(bias, 1e-6), k1);
    m_state[voltageState + 1] = voltage;
    m_state[powerState + 1] = normalizedP;
    m_state[reactiveState + 1] = reactivePower;
    m_state[referenceState + 1] = curve(normalizedP).qLimit * std::pow(bias, k2);
    const double axis = (voltage * voltage / xq) + reactivePower;
    const double norm = std::hypot(axis, realPower);
    m_state[gainState + 1] = std::clamp(norm > 1e-14 ? axis / norm : 0.0, 0.1, 1.0);
    // Evaluate once before accessing optional feedback inputs to validate them.
    evaluate(inputs, m_state.data() + 1);
    const double gainReduction = sw1 == 1 ? kfix : m_state[gainState + 1];
    const double error = m_state[referenceState + 1] - reactivePower -
        (kuf != 0.0 ? kuf * inputs[limiterStabilizingFeedbackInLocation] : 0.0);
    const double feedback = kfb != 0.0 ? kfb * inputs[limiterExciterFeedbackInLocation] : 0.0;
    m_state[feedbackState + 1] = feedback;
    m_state[firstLagState + 1] = std::clamp(kuL * gainReduction * error, vuiMin, vuiMax) - feedback;
    m_state[secondLagState + 1] = std::clamp(m_state[firstLagState + 1], vulMin2, vulMax2);
    m_state[0] = evaluate(inputs, m_state.data() + 1).mOutput.mValue;
    fieldSet = {m_state[0]};
}

void ExcitationLimiterUEL2C::timestep(CoreTime time,
                                      const IOdata& inputs,
                                      const SolverMode& /*mode*/)
{
    const auto result = evaluate(inputs, m_state.data() + 1);
    const double step = time - prevTime;
    for (index_t index = 0; index < 9; ++index) {
        m_state[index + 1] += step * result.mRates[index].mValue;
    }
    m_state[gainState + 1] = std::clamp(m_state[gainState + 1], 0.1, 1.0);
    m_state[0] = evaluate(inputs, m_state.data() + 1).mOutput.mValue;
    prevTime = time;
}
}  // namespace griddyn::limiters
