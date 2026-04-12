/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RAMP_LIMITER_H_
#define RAMP_LIMITER_H_
#pragma once

#include <limits>
namespace griddyn {
namespace blocks {
    /** class that limits the rate of change of a value between an upper and lower limit and
     * maintains state of whether it is clamping or not
     */
    class rampLimiter {
      private:
        double minRamp = std::numeric_limits<double>::min();  //!< the minimum ramp
        double maxRamp = std::numeric_limits<double>::max();  //!< the maximum ramp to allow
        double resetLevel =
            0.0;  //!< the level by which an input ramp has to be below the limits to reset
        bool limiterEngaged = false;  //!< flag indicating the limiter is engaged
        bool limiterHigh = false;  //!< flag indicating the high limit is engaged
      public:
        /** default constructor*/
        rampLimiter() = default;
        /** constructor with the min and max limits
    @param[in] nmin the minimum ramp
    @param[in] nmax the maximum ramp
    */
        rampLimiter(double nmin, double nmax);
        /** set the limits
    @param[in] nmin the minimum ramp
    @param[in] nmax the maximum ramp
    */
        void setLimits(double nmin, double nmax);
        /** set the reset level*/
        void setResetLevel(double newReset);
        /** check how close an input value is to changing the clamping state*/
        double limitCheck(double currentVal, double input, double dIdt) const;
        /** actively change the state depending on the input*/
        void changeLimitActivation(double dIdt);
        /** get the output
    @return if the limiter is clamping it will be at one of the limits, if it is not it will be the
    input*/
        double output(double dIdt) const;
        /** returns the time derivative of the output
    @param dIdt the time derivative of the input
    @return either dIdt or one of the ramp limits*/
        double deriv(double dIdt) const;
        /** return the partial derivative of the output with respect to the input
    @return either 1 or 0 if the limiter is actively clamping*/
        double DoutDin() const;
        /** perform the function without the knowledge of the state*
    @param dIdt the time derivative of the input
    @return either dIdt or the ramp limits if dIdt is greater than the max or less than the min
    allowable ramp*/
        double clampOutputRamp(double dIdt) const;
    };
}  // namespace blocks
}  // namespace griddyn
#endif
