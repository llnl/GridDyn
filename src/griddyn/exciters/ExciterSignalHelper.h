/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include "StaticExciterRectifier.h"
#include <array>
#include <cmath>

namespace griddyn::exciters::detail {
template<index_t StateCount>
struct ExciterSignal {
    double value = 0.0;
    double algebraic = 0.0;
    std::array<double, StateCount> state{};
    std::array<double, exciterInputCount> input{};
};

template<index_t StateCount>
ExciterSignal<StateCount> constantSignal(double value)
{
    ExciterSignal<StateCount> result;
    result.value = value;
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> algebraicSignal(double value)
{
    auto result = constantSignal<StateCount>(value);
    result.algebraic = 1.0;
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> stateSignal(double value, index_t index)
{
    auto result = constantSignal<StateCount>(value);
    result.state[index] = 1.0;
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> inputSignal(const IOdata& inputs, index_t index)
{
    auto result = constantSignal<StateCount>(inputs[index]);
    result.input[index] = 1.0;
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> addSignals(const ExciterSignal<StateCount>& left,
                                     const ExciterSignal<StateCount>& right)
{
    ExciterSignal<StateCount> result;
    result.value = left.value + right.value;
    result.algebraic = left.algebraic + right.algebraic;
    for (index_t index = 0; index < StateCount; ++index) {
        result.state[index] = left.state[index] + right.state[index];
    }
    for (index_t index = 0; index < exciterInputCount; ++index) {
        result.input[index] = left.input[index] + right.input[index];
    }
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> scaleSignal(const ExciterSignal<StateCount>& signal, double factor)
{
    ExciterSignal<StateCount> result;
    result.value = factor * signal.value;
    result.algebraic = factor * signal.algebraic;
    for (index_t index = 0; index < StateCount; ++index) {
        result.state[index] = factor * signal.state[index];
    }
    for (index_t index = 0; index < exciterInputCount; ++index) {
        result.input[index] = factor * signal.input[index];
    }
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> subtractSignals(const ExciterSignal<StateCount>& left,
                                          const ExciterSignal<StateCount>& right)
{
    return addSignals(left, scaleSignal(right, -1.0));
}

template<index_t StateCount>
ExciterSignal<StateCount> multiplySignals(const ExciterSignal<StateCount>& left,
                                          const ExciterSignal<StateCount>& right)
{
    ExciterSignal<StateCount> result;
    result.value = left.value * right.value;
    result.algebraic = (left.algebraic * right.value) + (left.value * right.algebraic);
    for (index_t index = 0; index < StateCount; ++index) {
        result.state[index] = (left.state[index] * right.value) + (left.value * right.state[index]);
    }
    for (index_t index = 0; index < exciterInputCount; ++index) {
        result.input[index] = (left.input[index] * right.value) + (left.value * right.input[index]);
    }
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> divideSignals(const ExciterSignal<StateCount>& numerator,
                                        const ExciterSignal<StateCount>& denominator)
{
    if (std::abs(denominator.value) <= 1e-12) {
        return constantSignal<StateCount>(0.0);
    }
    ExciterSignal<StateCount> result;
    result.value = numerator.value / denominator.value;
    const double denominatorSquared = denominator.value * denominator.value;
    result.algebraic =
        ((numerator.algebraic * denominator.value) - (numerator.value * denominator.algebraic)) /
        denominatorSquared;
    for (index_t index = 0; index < StateCount; ++index) {
        result.state[index] = ((numerator.state[index] * denominator.value) -
                               (numerator.value * denominator.state[index])) /
            denominatorSquared;
    }
    for (index_t index = 0; index < exciterInputCount; ++index) {
        result.input[index] = ((numerator.input[index] * denominator.value) -
                               (numerator.value * denominator.input[index])) /
            denominatorSquared;
    }
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount>
    applyFunction(const ExciterSignal<StateCount>& signal, double value, double derivative)
{
    ExciterSignal<StateCount> result;
    result.value = value;
    result.algebraic = derivative * signal.algebraic;
    for (index_t index = 0; index < StateCount; ++index) {
        result.state[index] = derivative * signal.state[index];
    }
    for (index_t index = 0; index < exciterInputCount; ++index) {
        result.input[index] = derivative * signal.input[index];
    }
    return result;
}

template<index_t StateCount>
ExciterSignal<StateCount> sqrtSignal(const ExciterSignal<StateCount>& signal)
{
    if (signal.value <= 0.0) {
        return constantSignal<StateCount>(0.0);
    }
    const double root = std::sqrt(signal.value);
    return applyFunction(signal, root, 0.5 / root);
}

template<index_t StateCount>
ExciterSignal<StateCount>
    clampSignal(const ExciterSignal<StateCount>& signal, double lower, double upper)
{
    if (signal.value <= lower) {
        return constantSignal<StateCount>(lower);
    }
    if (signal.value >= upper) {
        return constantSignal<StateCount>(upper);
    }
    return signal;
}

template<index_t StateCount>
ExciterSignal<StateCount> clampSignal(const ExciterSignal<StateCount>& signal,
                                      const ExciterSignal<StateCount>& lower,
                                      const ExciterSignal<StateCount>& upper)
{
    if (signal.value <= lower.value) {
        return lower;
    }
    if (signal.value >= upper.value) {
        return upper;
    }
    return signal;
}

template<index_t StateCount>
ExciterSignal<StateCount> rectifierFactorSignal(const ExciterSignal<StateCount>& normalizedCurrent)
{
    const auto data = computeRectifierFactor(normalizedCurrent.value);
    return applyFunction(normalizedCurrent, data.factor, data.derivative);
}

template<index_t StateCount>
bool integrationBlocked(double state,
                        double minimum,
                        double maximum,
                        const ExciterSignal<StateCount>& drive)
{
    return ((state >= maximum) && (drive.value > 0.0)) ||
        ((state <= minimum) && (drive.value < 0.0));
}
}  // namespace griddyn::exciters::detail
