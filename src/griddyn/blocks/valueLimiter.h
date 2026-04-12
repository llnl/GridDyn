/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VALUE_LIMITER_H_
#define VALUE_LIMITER_H_
#pragma once

#include <limits>

namespace griddyn {
namespace blocks {
    /** class that clamps a value between an upper and lower limit and maintains state of whether it
     * is clamping or not
     */
    class valueLimiter {
      private:
        double minVal = std::numeric_limits<double>::min();  //!< Minimum value
        double maxVal = std::numeric_limits<double>::max();  //!< maximum value
        double resetLevel = 0;  //!< the amount the value has to go above or below a min or max to
                                //!< be considered reset
        bool limiterEngaged = false;  //!< flag indicating the limiter is active
        bool limiterHigh = false;  //!< flag indicating the limit is engaged on the high side
      public:
        /** default constructor*/
        valueLimiter() = default;
        /** constructor with the min and max values defined*/
        valueLimiter(double nmin, double nmax);
        /** define the lower and upper limits*/
        void setLimits(double nmin, double nmax);
        /** set the gap required to go below the minimum to reset the values*/
        void setResetLevel(double newReset);
        /** return the proximity of an input value to the boundary conditions
    @param input the value to check
    @return the proximity of a value to changing the limit State*/
        double limitCheck(double input) const;
        /** feed an input that actively change whether the value is limited or not based on the
         * value*/
        void changeLimitActivation(double input);
        /** run a value through the system return the original or one of the limits*/
        double output(double input) const;
        /** return the time derivative  either didt or 0 if the limiter was hitting the limits*/
        double deriv(double dIdt) const;
        /** get derivative of the output with respect to the input, either 0 or 1 depending on
         * whether the limiter was engaged*/
        double DoutDin() const;
        /** apply the limiter without respect for the internal state*/
        double clampOutput(double input) const;
        /** return true if the limiter is actually limiting*/
        bool isActive() const { return limiterEngaged; }
    };
}  // namespace blocks
}  // namespace griddyn
#endif
