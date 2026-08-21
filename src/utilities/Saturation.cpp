/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Saturation.h"

#include <cmath>
#include <string>

namespace utilities {
// These functions implement standard fitted curves. Conventional arithmetic
// grouping is kept to make the equations recognizable.
// NOLINTBEGIN(readability-math-missing-parentheses)
Saturation::Saturation(SaturationType saturationType): type(saturationType)
{
    loadFunctions();
    computeParam();
}

Saturation::Saturation(const std::string& satType): type(SaturationType::NONE)
{
    setType(satType);
}

Saturation::Saturation(const Saturation& other):
    s10(other.s10), s12(other.s12), A(other.A), B(other.B), type(other.type)
{
    loadFunctions();
}

Saturation::Saturation(Saturation&& other) noexcept:
    s10(other.s10), s12(other.s12), A(other.A), B(other.B), type(other.type)
{
    loadFunctions();
}

Saturation& Saturation::operator=(const Saturation& other)
{
    if (this != &other) {
        s10 = other.s10;
        s12 = other.s12;
        A = other.A;
        B = other.B;
        type = other.type;
        loadFunctions();
    }
    return *this;
}

Saturation& Saturation::operator=(Saturation&& other) noexcept
{
    if (this != &other) {
        s10 = other.s10;
        s12 = other.s12;
        A = other.A;
        B = other.B;
        type = other.type;
        loadFunctions();
    }
    return *this;
}

void Saturation::setType(const std::string& stype)
{
    if (stype == "none") {
        type = SaturationType::NONE;
    } else if (stype == "quadratic") {
        type = SaturationType::QUADRATIC;
    } else if (stype == "scaled_quadratic") {
        type = SaturationType::SCALED_QUADRATIC;
    } else if ((stype == "cutoff_scaled_quadratic") || (stype == "clamped_scaled_quadratic")) {
        type = SaturationType::CUTOFF_SCALED_QUADRATIC;
    } else if (stype == "exponential") {
        type = SaturationType::EXPONENTIAL;
    } else if (stype == "linear") {
        type = SaturationType::LINEAR;
    }
    loadFunctions();
    computeParam();
}

void Saturation::setParam(double saturationAtOne, double saturationAtOnePointTwo)
{
    s10 = saturationAtOne;
    s12 = saturationAtOnePointTwo;
    computeParam();
}

void Saturation::setParam(double firstInput,
                          double firstSaturation,
                          double secondInput,
                          double secondSaturation)
{
    switch (type) {
        case SaturationType::QUADRATIC: {
            const double ssv = sqrt(firstSaturation / secondSaturation);
            A = -(secondInput * ssv - firstInput) / (firstInput - ssv);
            B = firstSaturation / ((firstInput - A) * (firstInput - A));
        } break;
        case SaturationType::SCALED_QUADRATIC:
        case SaturationType::CUTOFF_SCALED_QUADRATIC: {
            if ((firstInput <= 0.0) || (secondInput <= 0.0) ||
                (firstSaturation <= 0.0) || (secondSaturation <= 0.0)) {
                A = 0.0;
                B = 0.0;
                break;
            }
            const double ssv =
                sqrt((firstSaturation * firstInput) / (secondSaturation * secondInput));
            const double fitDenominator = firstInput - ssv;
            if (std::abs(fitDenominator) < 1e-12) {
                A = 0.0;
                B = 0.0;
                break;
            }
            A = -(secondInput * ssv - firstInput) / fitDenominator;
            const double distance = firstInput - A;
            B = (std::abs(distance) < 1e-12) ?
                0.0 :
                firstSaturation / (distance * distance);
        } break;
        case SaturationType::EXPONENTIAL:
            A = log(firstSaturation / secondSaturation) / log(firstInput / secondInput);
            B = firstSaturation / (pow(firstInput, A));
            break;
        case SaturationType::LINEAR:
            B = (secondSaturation - firstSaturation) / (secondInput - firstInput);
            A = firstInput - firstSaturation / B;
            break;
        case SaturationType::NONE:
        default:
            A = 0;
            B = 0;
            break;
    }
    s10 = compute(1.0);
    s12 = compute(1.2);
}

void Saturation::setType(SaturationType saturationType)
{
    type = saturationType;
    loadFunctions();
    computeParam();
}

Saturation::SaturationType Saturation::getType() const
{
    return type;
}
double Saturation::operator()(double val) const
{
    return satFunc(val);
}
double Saturation::compute(double val) const
{
    return satFunc(val);
}
double Saturation::deriv(double val) const
{
    return derivFunc(val);
}
Saturation::Evaluation Saturation::evaluate(double val) const
{
    return {.value = satFunc(val), .derivative = derivFunc(val)};
}
double Saturation::inv(double val) const
{
    if ((val < 0.00001) || (B == 0.0)) {
        return 0.5;
    }
    double ret = 0.5;
    switch (type) {
        case SaturationType::QUADRATIC:
            ret = sqrt(val / B) + A;
            break;
        case SaturationType::SCALED_QUADRATIC:
        case SaturationType::CUTOFF_SCALED_QUADRATIC: {
            const double temp = (2 * A + val / B);
            ret = (temp + sqrt(temp * temp - 4 * A * A)) / 2;
            break;
        }
        case SaturationType::EXPONENTIAL:
            ret = pow(val / B, 1 / A);
            break;
        case SaturationType::LINEAR:
            ret = A + val / B;
            break;
        case SaturationType::NONE:
        default:
            break;
    }
    return ret;
}

void Saturation::computeParam()
{
    switch (type) {
        case SaturationType::QUADRATIC: {
            if ((s10 <= 0.0) || (s12 <= 0.0)) {
                A = 0.0;
                B = 0.0;
                break;
            }
            const double ssv = sqrt(s10 / s12);
            const double fitDenominator = 1.0 - ssv;
            if (std::abs(fitDenominator) < 1e-12) {
                A = 0.0;
                B = 0.0;
                break;
            }
            A = -(1.2 * ssv - 1.0) / fitDenominator;
            const double distance = 1.0 - A;
            B = (std::abs(distance) < 1e-12) ? 0.0 : s10 / (distance * distance);
            break;
        }
        case SaturationType::SCALED_QUADRATIC:
        case SaturationType::CUTOFF_SCALED_QUADRATIC: {
            if ((s10 <= 0.0) || (s12 <= 0.0)) {
                A = 0.0;
                B = 0.0;
                break;
            }
            const double ssv = sqrt((s10 * 1.0) / (s12 * 1.2));
            const double fitDenominator = 1.0 - ssv;
            if (std::abs(fitDenominator) < 1e-12) {
                A = 0.0;
                B = 0.0;
                break;
            }
            A = -(1.2 * ssv - 1.0) / fitDenominator;
            const double distance = 1.0 - A;
            B = (std::abs(distance) < 1e-12) ? 0.0 : s10 / (distance * distance);
            break;
        }
        case SaturationType::EXPONENTIAL:
            if ((s10 <= 0.0) || (s12 <= 0.0)) {
                A = 0.0;
                B = 0.0;
            } else {
                A = -log(s10 / s12) / log(1.2);
                B = s10;
            }
            break;
        case SaturationType::LINEAR:
            B = (s12 - s10) / 0.2;
            A = (std::abs(B) < 1e-12) ? 0.0 : 1.0 - s10 / B;
            break;
        case SaturationType::NONE:
        default:
            A = 0;
            B = 0;
            break;
    }
}

void Saturation::loadFunctions()
{
    switch (type) {
        case SaturationType::QUADRATIC:
            satFunc = [this](double val) { return (B * (val - A) * (val - A)); };
            derivFunc = [this](double val) { return (2 * B * (val - A)); };
            break;
        case SaturationType::SCALED_QUADRATIC:
            satFunc = [this](double val) {
                return ((B == 0.0) || (val == 0.0)) ? 0.0 : (B * (val - A) * (val - A) / val);
            };
            derivFunc = [this](double val) {
                if ((B == 0.0) || (val == 0.0)) {
                    return 0.0;
                }
                const double distance = (val - A);
                return (B * (2 * val * distance - distance * distance) / (val * val));
            };
            break;
        case SaturationType::CUTOFF_SCALED_QUADRATIC:
            satFunc = [this](double val) {
                if ((B == 0.0) || (val <= 0.0) || (val < A)) {
                    return 0.0;
                }
                const double distance = val - A;
                return B * distance * distance / val;
            };
            derivFunc = [this](double val) {
                if ((B == 0.0) || (val <= 0.0) || (val < A)) {
                    return 0.0;
                }
                const double distance = val - A;
                return B * distance * (val + A) / (val * val);
            };
            break;
        case SaturationType::EXPONENTIAL:
            satFunc = [this](double val) { return (B == 0.0) ? 0.0 : (B * pow(val, A)); };
            derivFunc = [this](double val) { return (B == 0.0) ? 0.0 : (A * B * pow(val, A - 1)); };
            break;
        case SaturationType::LINEAR:
            satFunc = [this](double val) { return (val <= A) ? 0 : (B * (val - A)); };
            derivFunc = [this](double val) { return (val <= A) ? 0 : B; };
            break;
        case SaturationType::NONE:
        default:
            satFunc = [](double) { return 0; };
            derivFunc = [](double) { return 0; };
            break;
    }
}

// NOLINTEND(readability-math-missing-parentheses)
}  // namespace utilities
