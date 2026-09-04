/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include <array>

namespace griddyn::exciters::detail {
/** PSS/E AC-exciter rectifier-loading factor and slope with respect to normalized current. */
struct RectifierFactorData {
    double factor;
    double derivative;
};

RectifierFactorData computeRectifierFactor(double normalizedCurrent);

/** Potential-source and commutating-reactance result shared by ESST3A/ESST4B. */
struct RectifierData {
    double voltage;
    /** Derivatives with respect to Id, Iq, Vd, Vq, and XadIfd. */
    std::array<double, 5> derivatives;
};

RectifierData computeRectifierData(const IOdata& inputs,
                                   double potentialGain,
                                   double currentGain,
                                   double commutatingFactor,
                                   double leakageReactance,
                                   double thetaDegrees,
                                   double maximumVoltage);
}  // namespace griddyn::exciters::detail
