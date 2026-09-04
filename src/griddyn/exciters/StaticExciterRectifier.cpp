/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "StaticExciterRectifier.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace griddyn::exciters::detail {
RectifierFactorData computeRectifierFactor(double normalizedCurrent)
{
    constexpr double lowCurrentSlope = 577.0 / 1000.0;
    constexpr double highCurrentSlope = 1732.0 / 1000.0;
    if (normalizedCurrent <= 0.0) {
        return {.factor = 1.0, .derivative = 0.0};
    }
    if (normalizedCurrent <= 0.433) {
        return {.factor = 1.0 - (lowCurrentSlope * normalizedCurrent),
                .derivative = -lowCurrentSlope};
    }
    if (normalizedCurrent < 0.75) {
        const double factor =
            std::sqrt(std::max(0.0, 0.75 - (normalizedCurrent * normalizedCurrent)));
        return {.factor = factor, .derivative = (factor > 0.0) ? -normalizedCurrent / factor : 0.0};
    }
    if (normalizedCurrent <= 1.0) {
        return {.factor = highCurrentSlope * (1.0 - normalizedCurrent),
                .derivative = -highCurrentSlope};
    }
    return {.factor = 0.0, .derivative = 0.0};
}

RectifierData computeRectifierData(const IOdata& inputs,
                                   double potentialGain,
                                   double currentGain,
                                   double commutatingFactor,
                                   double leakageReactance,
                                   double thetaDegrees,
                                   double maximumVoltage)
{
    const double theta = thetaDegrees * std::numbers::pi_v<double> / 180.0;
    const double kpcReal = potentialGain * std::cos(theta);
    const double kpcImag = potentialGain * std::sin(theta);
    const double impedanceReal = currentGain + (leakageReactance * kpcReal);
    const double impedanceImag = leakageReactance * kpcImag;
    // Id and Vd are translated from GridDyn to the OpenIPSL/ANDES convention.
    const double directCurrent = inputs[exciterIdInLocation];
    const double quadratureCurrent = inputs[exciterIqInLocation];
    const double directVoltage = inputs[exciterVdInLocation];
    const double quadratureVoltage = inputs[exciterVqInLocation];
    const double realPart = (-kpcReal * directVoltage) - (kpcImag * quadratureVoltage) +
        (impedanceImag * directCurrent) - (impedanceReal * quadratureCurrent);
    const double imaginaryPart = (-kpcImag * directVoltage) + (kpcReal * quadratureVoltage) -
        (impedanceReal * directCurrent) - (impedanceImag * quadratureCurrent);
    const double sourceVoltage = std::hypot(realPart, imaginaryPart);
    std::array<double, 5> sourceDerivatives{};
    if (sourceVoltage > 0.0) {
        sourceDerivatives[0] =
            ((realPart * impedanceImag) - (imaginaryPart * impedanceReal)) / sourceVoltage;
        sourceDerivatives[1] =
            ((-realPart * impedanceReal) - (imaginaryPart * impedanceImag)) / sourceVoltage;
        sourceDerivatives[2] = ((-realPart * kpcReal) - (imaginaryPart * kpcImag)) / sourceVoltage;
        sourceDerivatives[3] = ((-realPart * kpcImag) + (imaginaryPart * kpcReal)) / sourceVoltage;
    }
    const double normalizedCurrent = (sourceVoltage > 0.0) ?
        commutatingFactor * inputs[exciterXadIfdInLocation] / sourceVoltage :
        0.0;
    const auto rectifier = computeRectifierFactor(normalizedCurrent);
    const double factor = rectifier.factor;
    const double factorDerivative = rectifier.derivative;
    const double unlimitedVoltage = sourceVoltage * factor;
    RectifierData result{.voltage = std::min(unlimitedVoltage, maximumVoltage), .derivatives = {}};
    if (unlimitedVoltage < maximumVoltage) {
        const double voltageFactor = factor - (factorDerivative * normalizedCurrent);
        for (index_t index = 0; index < 4; ++index) {
            result.derivatives[index] = voltageFactor * sourceDerivatives[index];
        }
        result.derivatives[4] = commutatingFactor * factorDerivative;
    }
    return result;
}
}  // namespace griddyn::exciters::detail
