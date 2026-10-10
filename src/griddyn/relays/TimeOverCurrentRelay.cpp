/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "TimeOverCurrentRelay.h"

#include "../GridSecondary.h"
#include "../Link.h"
#include "../events/Event.h"
#include "../measurement/Condition.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::relays {
namespace {
    struct CurveParameters {
        double mCoefficient;
        double mExponent;
        double mOffset;
    };

    CurveParameters curveParameters(TimeOverCurrentRelay::Curve curve)
    {
        using Curve = TimeOverCurrentRelay::Curve;
        switch (curve) {
            case Curve::IEC_STANDARD_INVERSE:
                return {.mCoefficient = 0.14, .mExponent = 0.02, .mOffset = 0.0};
            case Curve::IEC_VERY_INVERSE:
                return {.mCoefficient = 13.5, .mExponent = 1.0, .mOffset = 0.0};
            case Curve::IEC_EXTREMELY_INVERSE:
                return {.mCoefficient = 80.0, .mExponent = 2.0, .mOffset = 0.0};
            case Curve::IEC_LONG_TIME_INVERSE:
                return {.mCoefficient = 120.0, .mExponent = 1.0, .mOffset = 0.0};
            case Curve::IEEE_MODERATELY_INVERSE:
                return {.mCoefficient = 0.0515, .mExponent = 0.02, .mOffset = 0.114};
            case Curve::IEEE_VERY_INVERSE:
                return {.mCoefficient = 19.61, .mExponent = 2.0, .mOffset = 0.491};
            case Curve::IEEE_EXTREMELY_INVERSE:
                return {.mCoefficient = 28.2, .mExponent = 2.0, .mOffset = 0.1217};
            default:
                return {.mCoefficient = 0.0, .mExponent = 0.0, .mOffset = 0.0};
        }
    }

    bool isDisabled(double value)
    {
        return value >= (kBigNum / 2.0);
    }

    std::string currentExpression(const CoreObject* source, index_t terminal)
    {
        if (dynamic_cast<const Link*>(source) != nullptr) {
            return "current" + std::to_string(terminal);
        }
        return "sqrt(p^2+q^2)/@bus:v";
    }
}  // namespace

TimeOverCurrentRelay::TimeOverCurrentRelay(const std::string& objName): Relay(objName)
{
    opFlags.set(CONTINUOUS_FLAG);
    opFlags.set(RESETTABLE_FLAG);
}

CoreObject* TimeOverCurrentRelay::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<TimeOverCurrentRelay, Relay>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->mCurve = mCurve;
    nobj->mPickup = mPickup;
    nobj->mInstantaneousPickup = mInstantaneousPickup;
    nobj->mInstantaneousDelay = mInstantaneousDelay;
    nobj->mDefiniteTime = mDefiniteTime;
    nobj->mTimeDial = mTimeDial;
    nobj->mResetMargin = mResetMargin;
    nobj->mTerminal = mTerminal;
    nobj->mVoltageBase = mVoltageBase;
    nobj->mTimeCurrentCurve = mTimeCurrentCurve;
    nobj->mTripped = mTripped;
    nobj->mRetirementPending = mRetirementPending;
    return nobj;
}

void TimeOverCurrentRelay::set(std::string_view param, std::string_view val)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "curve" || key == "characteristic") {
        mCurve = curveFromString(val);
    } else {
        Relay::set(param, val);
    }
}

void TimeOverCurrentRelay::setTimeCurrentCurve(std::span<const TimeCurrentPoint> points,
                                               units::unit currentUnit)
{
    if (points.empty()) {
        throw InvalidParameterValue("time-current curve requires at least one point");
    }

    std::vector<TimeCurrentPoint> converted;
    converted.reserve(points.size());
    for (const auto& point : points) {
        if (!std::isfinite(point.current) || point.current <= 0.0 ||
            !std::isfinite(static_cast<double>(point.time)) || point.time < timeZero) {
            throw InvalidParameterValue("time-current curve point is invalid");
        }
        const auto current =
            units::convert(point.current, currentUnit, units::puA, systemBasePower, mVoltageBase);
        if (!std::isfinite(current) || current <= 0.0) {
            throw InvalidParameterValue("time-current curve current conversion is invalid");
        }
        if (!converted.empty()) {
            const auto& previous = converted.back();
            if (current < previous.current ||
                (current == previous.current && point.time != previous.time)) {
                throw InvalidParameterValue(
                    "time-current curve currents must be nondecreasing and duplicate points must match");
            }
        }
        converted.push_back({.current = current, .time = point.time});
    }

    mTimeCurrentCurve = std::move(converted);
    mPickup = mTimeCurrentCurve.front().current;
    mCurve = Curve::TIME_CURRENT_TABLE;
}

void TimeOverCurrentRelay::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "pickup" || key == "pickuplevel" || key == "ipickup" || key == "limit") {
        mPickup = units::convert(val, unitType, units::puA, systemBasePower, mVoltageBase);
    } else if (key == "instantaneous" || key == "instantaneouspickup" || key == "instpickup" ||
               key == "highset") {
        mInstantaneousPickup =
            units::convert(val, unitType, units::puA, systemBasePower, mVoltageBase);
    } else if (key == "instantaneousdelay" || key == "instdelay" || key == "highsetdelay") {
        mInstantaneousDelay = units::convert(val,
                                             unitType == units::defunit ? units::second : unitType,
                                             units::second);
    } else if (key == "delay" || key == "definitetime" || key == "minimumtime" ||
               key == "tripdelay") {
        mDefiniteTime = units::convert(val,
                                       unitType == units::defunit ? units::second : unitType,
                                       units::second);
    } else if (key == "timedial" || key == "td") {
        mTimeDial = val;
    } else if (key == "resetmargin" || key == "margin") {
        mResetMargin = units::convert(val, unitType, units::puA, systemBasePower, mVoltageBase);
    } else if (key == "terminal" || key == "side") {
        mTerminal = static_cast<index_t>(val);
    } else if (key == "voltagebase") {
        mVoltageBase = val;
    } else {
        Relay::set(param, val, unitType);
    }
}

double TimeOverCurrentRelay::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "pickup" || key == "pickuplevel" || key == "ipickup" || key == "limit") {
        return units::convert(mPickup, units::puA, unitType, systemBasePower, mVoltageBase);
    }
    if (key == "instantaneous" || key == "instantaneouspickup" || key == "instpickup" ||
        key == "highset") {
        return units::convert(
            mInstantaneousPickup, units::puA, unitType, systemBasePower, mVoltageBase);
    }
    if (key == "instantaneousdelay" || key == "instdelay" || key == "highsetdelay") {
        return units::convert(mInstantaneousDelay, units::second, unitType);
    }
    if (key == "delay" || key == "definitetime" || key == "minimumtime" || key == "tripdelay") {
        return units::convert(mDefiniteTime, units::second, unitType);
    }
    if (key == "timedial" || key == "td") {
        return mTimeDial;
    }
    if (key == "resetmargin" || key == "margin") {
        return units::convert(mResetMargin, units::puA, unitType, systemBasePower, mVoltageBase);
    }
    if (key == "terminal" || key == "side") {
        return static_cast<double>(mTerminal);
    }
    if (key == "voltagebase") {
        return mVoltageBase;
    }
    if (key == "tripped") {
        return mTripped ? 1.0 : 0.0;
    }
    return Relay::get(param, unitType);
}

std::string TimeOverCurrentRelay::getString(std::string_view param) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "curve" || key == "characteristic") {
        return curveToString(mCurve);
    }
    return Relay::getString(param);
}

void TimeOverCurrentRelay::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    static constexpr auto numericParameterStrings =
        std::array<std::string_view, 9>{"pickup",
                                        "instantaneous",
                                        "instantaneousdelay",
                                        "delay",
                                        "timedial",
                                        "resetmargin",
                                        "terminal",
                                        "voltagebase",
                                        "tripped"};
    static constexpr auto stringParameterStrings = std::array<std::string_view, 1>{"curve"};
    static constexpr std::array<std::string_view, 0> flagStrings{};
    getParamString<TimeOverCurrentRelay, Relay>(
        this, pstr, numericParameterStrings, stringParameterStrings, flagStrings, pstype);
}

TimeOverCurrentRelay::Curve TimeOverCurrentRelay::curveFromString(std::string_view curveName)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{curveName});
    if (key == "iec_standard_inverse" || key == "iec_inverse" || key == "standard_inverse" ||
        key == "standard") {
        return Curve::IEC_STANDARD_INVERSE;
    }
    if (key == "iec_very_inverse" || key == "very_inverse" || key == "very") {
        return Curve::IEC_VERY_INVERSE;
    }
    if (key == "iec_extremely_inverse" || key == "extremely_inverse" || key == "extremely") {
        return Curve::IEC_EXTREMELY_INVERSE;
    }
    if (key == "iec_long_time_inverse" || key == "long_time_inverse" || key == "long") {
        return Curve::IEC_LONG_TIME_INVERSE;
    }
    if (key == "ieee_moderately_inverse" || key == "moderately_inverse" || key == "moderately") {
        return Curve::IEEE_MODERATELY_INVERSE;
    }
    if (key == "ieee_very_inverse") {
        return Curve::IEEE_VERY_INVERSE;
    }
    if (key == "ieee_extremely_inverse") {
        return Curve::IEEE_EXTREMELY_INVERSE;
    }
    if (key == "definite" || key == "definite_time" || key == "definite-time") {
        return Curve::DEFINITE_TIME;
    }
    if (key == "table" || key == "piecewise" || key == "time_current_table" ||
        key == "time-current-table") {
        return Curve::TIME_CURRENT_TABLE;
    }
    throw InvalidParameterValue("curve");
}

std::string TimeOverCurrentRelay::curveToString(Curve curve)
{
    switch (curve) {
        case Curve::IEC_STANDARD_INVERSE:
            return "iec_standard_inverse";
        case Curve::IEC_VERY_INVERSE:
            return "iec_very_inverse";
        case Curve::IEC_EXTREMELY_INVERSE:
            return "iec_extremely_inverse";
        case Curve::IEC_LONG_TIME_INVERSE:
            return "iec_long_time_inverse";
        case Curve::IEEE_MODERATELY_INVERSE:
            return "ieee_moderately_inverse";
        case Curve::IEEE_VERY_INVERSE:
            return "ieee_very_inverse";
        case Curve::IEEE_EXTREMELY_INVERSE:
            return "ieee_extremely_inverse";
        case Curve::DEFINITE_TIME:
            return "definite_time";
        case Curve::TIME_CURRENT_TABLE:
            return "time_current_table";
        default:
            return "unknown";
    }
}

CoreTime TimeOverCurrentRelay::operatingTime(double current) const
{
    if (!std::isfinite(current) || !std::isfinite(mPickup) || mPickup <= 0.0 ||
        current <= mPickup) {
        return maxTime;
    }
    if (mCurve == Curve::DEFINITE_TIME) {
        return mDefiniteTime;
    }
    if (mCurve == Curve::TIME_CURRENT_TABLE) {
        if (mTimeCurrentCurve.empty()) {
            return maxTime;
        }
        const auto right = std::upper_bound(mTimeCurrentCurve.begin(),
                                            mTimeCurrentCurve.end(),
                                            current,
                                            [](double value, const TimeCurrentPoint& point) {
                                                return value < point.current;
                                            });
        if (right == mTimeCurrentCurve.end()) {
            return mDefiniteTime + mTimeCurrentCurve.back().time;
        }
        if (right == mTimeCurrentCurve.begin()) {
            return maxTime;
        }
        const auto& leftPoint = *(right - 1);
        const auto& rightPoint = *right;
        if (rightPoint.current == leftPoint.current) {
            return mDefiniteTime + leftPoint.time;
        }
        const double fraction =
            (current - leftPoint.current) / (rightPoint.current - leftPoint.current);
        const auto time = leftPoint.time + (fraction * (rightPoint.time - leftPoint.time));
        return std::isfinite(static_cast<double>(time)) ?
            std::max(timeZero, mDefiniteTime + CoreTime(time)) :
            maxTime;
    }
    const auto parameters = curveParameters(mCurve);
    const double multiple = current / mPickup;
    const double denominator = std::pow(multiple, parameters.mExponent) - 1.0;
    if (!(denominator > 0.0)) {
        return maxTime;
    }
    const double time =
        mDefiniteTime + mTimeDial * ((parameters.mCoefficient / denominator) + parameters.mOffset);
    if (!std::isfinite(time)) {
        return maxTime;
    }
    return (time < timeZero) ? timeZero : CoreTime(time);
}

void TimeOverCurrentRelay::validateParameters() const
{
    if (m_sourceObject == nullptr || m_sinkObject == nullptr || mPickup <= 0.0 ||
        !std::isfinite(mPickup) || mTimeDial < 0.0 || !std::isfinite(mTimeDial) ||
        mDefiniteTime < timeZero || !std::isfinite(static_cast<double>(mDefiniteTime)) ||
        mInstantaneousDelay < timeZero ||
        !std::isfinite(static_cast<double>(mInstantaneousDelay)) || mResetMargin < 0.0 ||
        !std::isfinite(mResetMargin) || mTerminal < 1 || mTerminal > 2 || mVoltageBase <= 0.0 ||
        !std::isfinite(mVoltageBase)) {
        throw InvalidParameterValue("time-over-current relay parameters or source/sink");
    }
    if (!isDisabled(mInstantaneousPickup) &&
        (!std::isfinite(mInstantaneousPickup) || mInstantaneousPickup <= mPickup)) {
        throw InvalidParameterValue("instantaneous pickup must exceed pickup");
    }
    if (mCurve == Curve::TIME_CURRENT_TABLE && mTimeCurrentCurve.empty()) {
        throw InvalidParameterValue("time-current curve has no points");
    }
}

void TimeOverCurrentRelay::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateParameters();

    auto tripEvent = std::make_shared<Event>();
    const auto measurement = currentExpression(m_sourceObject, mTerminal);
    if (dynamic_cast<Link*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(makeCondition(measurement, ">", mPickup, m_sourceObject)));
        setResetMargin(0, mResetMargin);
        tripEvent->setTarget(m_sinkObject, "switch" + std::to_string(mTerminal));
        tripEvent->setValue(1.0);
    } else if (dynamic_cast<GridSecondary*>(m_sourceObject) != nullptr) {
        add(std::shared_ptr<Condition>(makeCondition(measurement, ">", mPickup, m_sourceObject)));
        setResetMargin(0, mResetMargin);
        tripEvent->setTarget(m_sinkObject, "status");
        tripEvent->setValue(0.0);
    } else {
        throw InvalidParameterValue("time-over-current source must be a link or secondary object");
    }
    add(std::move(tripEvent));
    setActionTrigger(0, 0, mDefiniteTime);

    if (!isDisabled(mInstantaneousPickup)) {
        add(std::shared_ptr<Condition>(
            makeCondition(measurement, ">", mInstantaneousPickup, m_sourceObject)));
        setResetMargin(1, mResetMargin);
        setActionTrigger(0, 1, mInstantaneousDelay);
    }

    Relay::dynObjectInitializeA(time0, flags);
}

void TimeOverCurrentRelay::updateA(CoreTime time)
{
    if (mRetirementPending) {
        // Do this on the next relay update, after the topology-changing event
        // has returned to the simulation.  Changing the root layout from the
        // action callback itself can leave the integrator's root vector stale.
        mRetirementPending = false;
        for (index_t condition = 0; getCondition(condition) != nullptr; ++condition) {
            if (getConditionStatus(condition) != ConditionStatus::DISABLED) {
                setConditionStatus(condition, ConditionStatus::DISABLED);
            }
        }
    }
    if (mTripped) {
        nextUpdateTime = maxTime;
        lastUpdateTime = time;
        return;
    }
    Relay::updateA(time);
}

void TimeOverCurrentRelay::conditionTriggered(index_t conditionNum, CoreTime /*triggerTime*/)
{
    if (conditionNum == 0) {
        setActionTrigger(0, 0, operatingTime(getConditionValue(0)));
    } else if (conditionNum == 1) {
        setActionTrigger(0, 1, mInstantaneousDelay);
    }
}

void TimeOverCurrentRelay::actionTaken(index_t actionNum,
                                       index_t conditionNum,
                                       ChangeCode actionReturn,
                                       CoreTime actionTime)
{
    Relay::actionTaken(actionNum, conditionNum, actionReturn, actionTime);
    if (actionNum != 0 || mTripped) {
        return;
    }
    mTripped = true;
    mRetirementPending = true;
    logging::normal(this, "time-over-current relay tripped on {}", m_sourceObject->getName());
    alert(this, BREAKER_TRIP_CURRENT);
    // Keep the triggered root registered until the solver has completed the
    // topology-changing action.  The next update retires all conditions.
}

void TimeOverCurrentRelay::conditionCleared(index_t conditionNum, CoreTime clearTime)
{
    Relay::conditionCleared(conditionNum, clearTime);
    if (!mTripped) {
        return;
    }
    // This also clears any other delayed checks for the condition.  The
    // callback is reached from Relay's normal condition-clearing path, where
    // root-count changes are already supported.
    setConditionStatus(conditionNum, ConditionStatus::DISABLED);
}

}  // namespace griddyn::relays
