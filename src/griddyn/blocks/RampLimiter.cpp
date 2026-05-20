/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RampLimiter.h"

#include <algorithm>
namespace griddyn::blocks {
RampLimiter::RampLimiter(double nmin, double nmax): minRamp(nmin), maxRamp(nmax) {}
void RampLimiter::setLimits(double nmin, double nmax)
{
    minRamp = nmin;
    maxRamp = nmax;
}
void RampLimiter::setResetLevel(double newReset)
{
    resetLevel = newReset;
}
double RampLimiter::limitCheck(double currentVal, double input, double dIdt) const
{
    double val;
    if (limiterEngaged) {
        if (limiterHigh) {
            val = input - currentVal + resetLevel;
        } else {
            val = currentVal - input + resetLevel;
        }
    } else {
        val = std::min(maxRamp - dIdt, dIdt - minRamp);
    }
    return val;
}

void RampLimiter::changeLimitActivation(double dIdt)
{
    if (limiterEngaged) {
        if (limiterHigh) {
            if (dIdt <= maxRamp) {
                limiterHigh = false;
            }
            if (dIdt >= minRamp) {
                limiterEngaged = false;
            }
        } else {
            if (dIdt >= minRamp) {
                if (dIdt <= maxRamp) {
                    limiterEngaged = false;
                } else {
                    limiterHigh = true;
                }
            }
        }
    } else {
        if (dIdt >= maxRamp) {
            limiterHigh = true;
            limiterEngaged = true;
        } else if (dIdt <= minRamp) {
            limiterEngaged = true;
        }
    }
}

double RampLimiter::output(double dIdt) const
{
    if (limiterEngaged) {
        return (limiterHigh) ? maxRamp : minRamp;
    }
    return dIdt;
}

double RampLimiter::deriv(double dIdt) const
{
    if (limiterEngaged) {
        return (limiterHigh) ? maxRamp : minRamp;
    }
    return dIdt;
}

double RampLimiter::DoutDin() const
{
    return (limiterEngaged) ? 0.0 : 1.0;
}
double RampLimiter::clampOutputRamp(double dIdt) const
{
    if (dIdt > maxRamp) {
        return maxRamp;
    }
    if (dIdt < minRamp) {
        return minRamp;
    }
    return dIdt;
}
}  // namespace griddyn::blocks
