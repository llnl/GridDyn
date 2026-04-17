/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "valueLimiter.h"

#include <algorithm>

namespace griddyn::blocks {
valueLimiter::valueLimiter(double nmin, double nmax): minVal(nmin), maxVal(nmax) {}
void valueLimiter::setLimits(double nmin, double nmax)
{
    minVal = nmin;
    maxVal = nmax;
}
void valueLimiter::setResetLevel(double newReset)
{
    resetLevel = newReset;
}
double valueLimiter::limitCheck(double input) const
{
    double val;
    if (limiterEngaged) {
        if (limiterHigh) {
            val = input - maxVal + resetLevel;
        } else {
            val = minVal - input + resetLevel;
        }
    } else {
        val = std::min(maxVal - input, input - minVal);
    }
    return val;
}

void valueLimiter::changeLimitActivation(double input)
{
    if (limiterEngaged) {
        if (limiterHigh) {
            if (input <= maxVal) {
                limiterHigh = false;
            }
            if (input >= minVal) {
                limiterEngaged = false;
            }
        } else {
            if (input >= minVal) {
                if (input <= maxVal) {
                    limiterEngaged = false;
                } else {
                    limiterHigh = true;
                }
            }
        }
    } else {
        if (input >= maxVal) {
            limiterHigh = true;
            limiterEngaged = true;
        } else if (input <= minVal) {
            limiterEngaged = true;
        }
    }
}

double valueLimiter::output(double input) const
{
    return (limiterEngaged) ? ((limiterHigh) ? maxVal : minVal) : input;
}

double valueLimiter::deriv(double dIdt) const
{
    return (limiterEngaged) ? 0.0 : dIdt;
}
double valueLimiter::DoutDin() const
{
    return (limiterEngaged) ? 0.0 : 1.0;
}
double valueLimiter::clampOutput(double input) const
{
    return (input > maxVal) ? maxVal : (input < minVal) ? minVal : input;
}

}  // namespace griddyn::blocks
