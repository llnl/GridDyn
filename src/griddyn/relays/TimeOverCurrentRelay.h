/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::relays {

/**
 * @brief Non-directional time-over-current protection for a link or secondary object.
 *
 * The relay uses the current magnitude at the selected link terminal.  A pickup
 * crossing starts a delayed action whose delay is calculated from the selected
 * IEC or IEEE inverse-time characteristic.  An optional instantaneous pickup
 * provides a high-set trip path.  Clearing the pickup condition before the
 * delay expires resets the pending action through Relay's resettable-condition
 * handling.
 *
 * This is deliberately a protection relay, rather than a Fuse or Breaker
 * variant: it does not model I2t heating or reclosing and it can drive an
 * independently supplied sink object.
 */
class TimeOverCurrentRelay: public Relay {
  public:
    enum class Curve {
        IEC_STANDARD_INVERSE,
        IEC_VERY_INVERSE,
        IEC_EXTREMELY_INVERSE,
        IEC_LONG_TIME_INVERSE,
        IEEE_MODERATELY_INVERSE,
        IEEE_VERY_INVERSE,
        IEEE_EXTREMELY_INVERSE,
        DEFINITE_TIME,
        TIME_CURRENT_TABLE,
    };

    struct TimeCurrentPoint {
        double current;
        CoreTime time;
    };

    explicit TimeOverCurrentRelay(const std::string& objName = "timeOverCurrentRelay_$");

    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    std::string getString(std::string_view param) const override;
    void getParameterStrings(stringVec& pstr,
                             ParamStringType pstype = ParamStringType::all) const override;

    /**
     * @brief Install a monotone, piecewise-linear time/current characteristic.
     *
     * The point currents are converted to the relay's puA storage using the
     * configured voltage base.  This is used by the constrained TIOCR1 DYR
     * compatibility path because that record supplies six explicit points
     * instead of an IEC or IEEE analytic curve.
     */
    void setTimeCurrentCurve(std::span<const TimeCurrentPoint> points,
                             units::unit currentUnit = units::defunit);

    std::size_t timeCurrentCurveSize() const { return mTimeCurrentCurve.size(); }

    /**
     * @brief Calculate the operating time for a measured current in puA.
     *
     * Returns maxTime when current is at or below pickup, or when the
     * selected inverse curve cannot operate.  The method is public so reader
     * and protection tests can validate a characteristic without constructing
     * a complete network.
     */
    CoreTime operatingTime(double current) const;

  protected:
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void updateA(CoreTime time) override;
    void conditionTriggered(index_t conditionNum, CoreTime triggerTime) override;
    void actionTaken(index_t actionNum,
                     index_t conditionNum,
                     ChangeCode actionReturn,
                     CoreTime actionTime) override;
    void conditionCleared(index_t conditionNum, CoreTime clearTime) override;

  private:
    static Curve curveFromString(std::string_view curveName);
    static std::string curveToString(Curve curve);
    void validateParameters() const;

    Curve mCurve = Curve::IEC_STANDARD_INVERSE;
    model_parameter mPickup = 1.0;  //!< [puA] pickup current
    model_parameter mInstantaneousPickup = kBigNum;  //!< [puA] optional high-set pickup
    CoreTime mInstantaneousDelay = timeZero;  //!< [s] high-set delay
    CoreTime mDefiniteTime = timeZero;  //!< [s] additive/minimum delay, or definite delay
    double mTimeDial = 1.0;  //!< dimensionless inverse-time multiplier
    double mResetMargin = 0.0;  //!< [puA] amount below pickup required to reset
    index_t mTerminal = 1;  //!< link terminal (1 or 2)
    double mVoltageBase = 120.0;  //!< [kV], retained for puA conversion parity with other relays
    std::vector<TimeCurrentPoint> mTimeCurrentCurve;  //!< current [puA], time [s]
    bool mTripped = false;
    bool mRetirementPending = false;
};

}  // namespace griddyn::relays
