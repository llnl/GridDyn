/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../ExcitationLimiter.h"
#include "../GridComponentHelperClasses.h"
#include "solvers/SolverMode.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn::limiters::detail {
/** Value and analytic derivatives of a limiter block diagram. */
template<count_t N>
struct Signal {
    double mValue = 0.0;
    std::array<double, N + excitationLimiterInputCount> mSlope{};
};

template<count_t N>
Signal<N> constant(double value)
{
    return {.mValue = value};
}

template<count_t N>
Signal<N> state(double value, index_t index)
{
    auto result = constant<N>(value);
    result.mSlope[index] = 1.0;
    return result;
}

template<count_t N>
Signal<N> input(const IOdata& inputs, index_t index)
{
    auto result = constant<N>(inputs[index]);
    result.mSlope[N + index] = 1.0;
    return result;
}

template<count_t N>
Signal<N> scale(Signal<N> signal, double factor)
{
    signal.mValue *= factor;
    for (auto& slope : signal.mSlope) {
        slope *= factor;
    }
    return signal;
}

template<count_t N>
Signal<N> add(Signal<N> left, const Signal<N>& right)
{
    left.mValue += right.mValue;
    for (index_t index = 0; index < left.mSlope.size(); ++index) {
        left.mSlope[index] += right.mSlope[index];
    }
    return left;
}

template<count_t N>
Signal<N> subtract(const Signal<N>& left, const Signal<N>& right)
{
    return add(left, scale(right, -1.0));
}

template<count_t N>
Signal<N> multiply(const Signal<N>& left, const Signal<N>& right)
{
    auto result = constant<N>(left.mValue * right.mValue);
    for (index_t index = 0; index < result.mSlope.size(); ++index) {
        result.mSlope[index] =
            left.mSlope[index] * right.mValue + left.mValue * right.mSlope[index];
    }
    return result;
}

template<count_t N>
Signal<N> function(const Signal<N>& signal, double value, double derivative)
{
    auto result = scale(signal, derivative);
    result.mValue = value;
    return result;
}

template<count_t N>
Signal<N> power(const Signal<N>& signal, int exponent)
{
    if (exponent == 0) {
        return constant<N>(1.0);
    }
    return function(signal,
                    std::pow(signal.mValue, exponent),
                    exponent * std::pow(signal.mValue, exponent - 1));
}

template<count_t N>
Signal<N> reciprocal(const Signal<N>& signal)
{
    return function(signal, 1.0 / signal.mValue, -1.0 / (signal.mValue * signal.mValue));
}

template<count_t N>
Signal<N> magnitude(const Signal<N>& firstComponent, const Signal<N>& secondComponent)
{
    const double value = std::hypot(firstComponent.mValue, secondComponent.mValue);
    if (value <= 1e-14) {
        return constant<N>(0.0);
    }
    auto result = add(scale(firstComponent, firstComponent.mValue / value),
                      scale(secondComponent, secondComponent.mValue / value));
    result.mValue = value;
    return result;
}

template<count_t N>
Signal<N> clamp(const Signal<N>& signal, double minimum, double maximum)
{
    // Use the interior derivative at the lower bound and the saturated
    // derivative at the upper bound for these piecewise-smooth equations.
    if (signal.mValue < minimum || signal.mValue >= maximum) {
        return constant<N>(std::clamp(signal.mValue, minimum, maximum));
    }
    return signal;
}

template<count_t N>
Signal<N> limitedRate(const Signal<N>& drive, double value, double minimum, double maximum)
{
    return ((value <= minimum && drive.mValue < 0.0) || (value >= maximum && drive.mValue > 0.0)) ?
        constant<N>(0.0) :
        drive;
}

template<count_t N>
struct Evaluation {
    Signal<N> mOutput;
    std::array<Signal<N>, N> mRates{};
};

/** Shared DAE, partitioned, and analytic-Jacobian implementation. */
template<count_t N>
class LimiterDynamics: public ExcitationLimiter {
  public:
    explicit LimiterDynamics(const std::string& name): ExcitationLimiter(name)
    {
        offsets.local().local.algSize = 1;
        offsets.local().local.diffSize = N;
        offsets.local().local.jacSize = (N + 1) * (N + 1 + excitationLimiterInputCount);
    }

    void residual(const IOdata& inputs,
                  const StateData& data,
                  double residual[],
                  const SolverMode& mode) override
    {
        const auto loc = offsets.getLocations(data, residual, mode, this);
        const auto result = evaluate(inputs, loc.diffStateLoc);
        if (hasAlgebraic(mode)) {
            loc.destLoc[0] = result.mOutput.mValue - loc.algStateLoc[0];
        }
        if (hasDifferential(mode)) {
            for (index_t index = 0; index < N; ++index) {
                loc.destDiffLoc[index] = result.mRates[index].mValue - loc.dstateLoc[index];
            }
        }
    }

    void derivative(const IOdata& inputs,
                    const StateData& data,
                    double derivative[],
                    const SolverMode& mode) override
    {
        if (hasDifferential(mode)) {
            const auto loc = offsets.getLocations(data, derivative, mode, this);
            const auto result = evaluate(inputs, loc.diffStateLoc);
            for (index_t index = 0; index < N; ++index) {
                loc.destDiffLoc[index] = result.mRates[index].mValue;
            }
        }
    }

    void algebraicUpdate(const IOdata& inputs,
                         const StateData& data,
                         double update[],
                         const SolverMode& mode,
                         double /*alpha*/) override
    {
        if (hasAlgebraic(mode)) {
            const auto loc = offsets.getLocations(data, update, mode, this);
            loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).mOutput.mValue;
        }
    }

    void jacobianElements(const IOdata& inputs,
                          const StateData& data,
                          MatrixData<double>& matrix,
                          const IOlocs& inputLocs,
                          const SolverMode& mode) override
    {
        const auto loc = offsets.getLocations(data, mode, this);
        const auto result = evaluate(inputs, loc.diffStateLoc);
        auto write = [&](index_t row, const Signal<N>& signal, bool differential) {
            if (!isAlgebraicOnly(mode)) {
                for (index_t column = 0; column < N; ++column) {
                    if (signal.mSlope[column] != 0.0 &&
                        (!differential || row != loc.diffOffset + column)) {
                        matrix.assign(row, loc.diffOffset + column, signal.mSlope[column]);
                    }
                }
            }
            for (index_t input = 0; input < excitationLimiterInputCount; ++input) {
                if (signal.mSlope[N + input] != 0.0) {
                    matrix.assignCheckCol(row, inputLocs[input], signal.mSlope[N + input]);
                }
            }
        };
        if (hasAlgebraic(mode)) {
            matrix.assign(loc.algOffset, loc.algOffset, -1.0);
            write(loc.algOffset, result.mOutput, false);
        }
        if (hasDifferential(mode)) {
            for (index_t row = 0; row < N; ++row) {
                write(loc.diffOffset + row, result.mRates[row], true);
                matrix.assign(loc.diffOffset + row,
                              loc.diffOffset + row,
                              result.mRates[row].mSlope[row] - data.cj);
            }
        }
    }

  private:
    virtual ::griddyn::limiters::detail::Evaluation<N> evaluate(const IOdata& inputs,
                                                                const double states[]) const = 0;
};
}  // namespace griddyn::limiters::detail
