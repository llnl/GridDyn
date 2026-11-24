/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "../Governor.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace griddyn::governors::detail {
/** Values and analytic first derivatives for the small governor block diagrams. */
template<std::size_t N>
struct GovernorSignal {
    double mValue = 0.0;
    std::array<double, N> mStateGradient{};
    std::array<double, 3> mInputGradient{};
};
template<std::size_t N>
GovernorSignal<N> constant(double value)
{
    return {.mValue = value};
}
template<std::size_t N>
GovernorSignal<N> state(double value, index_t index)
{
    auto result = constant<N>(value);
    result.mStateGradient[index] = 1.0;
    return result;
}
template<std::size_t N>
GovernorSignal<N> input(const IOdata& inputs, index_t index)
{
    auto result = constant<N>(inputs[index]);
    result.mInputGradient[index] = 1.0;
    return result;
}
template<std::size_t N>
GovernorSignal<N> operator+(const GovernorSignal<N>& left, const GovernorSignal<N>& right)
{
    auto result = constant<N>(left.mValue + right.mValue);
    for (std::size_t index = 0; index < N; ++index) {
        result.mStateGradient[index] = left.mStateGradient[index] + right.mStateGradient[index];
    }
    for (std::size_t index = 0; index < 3; ++index) {
        result.mInputGradient[index] = left.mInputGradient[index] + right.mInputGradient[index];
    }
    return result;
}
template<std::size_t N>
GovernorSignal<N> operator*(double factor, const GovernorSignal<N>& left)
{
    auto result = constant<N>(factor * left.mValue);
    for (std::size_t index = 0; index < N; ++index) {
        result.mStateGradient[index] = factor * left.mStateGradient[index];
    }
    for (std::size_t index = 0; index < 3; ++index) {
        result.mInputGradient[index] = factor * left.mInputGradient[index];
    }
    return result;
}
template<std::size_t N>
GovernorSignal<N> operator-(const GovernorSignal<N>& left, const GovernorSignal<N>& right)
{
    return left + (-1.0 * right);
}
template<std::size_t N>
GovernorSignal<N> clamp(const GovernorSignal<N>& left, double lower, double upper)
{
    if (left.mValue <= lower) {
        return constant<N>(lower);
    }
    if (left.mValue >= upper) {
        return constant<N>(upper);
    }
    return left;
}
template<std::size_t N>
struct GovernorEvaluation {
    GovernorSignal<N> mOutput;
    std::array<GovernorSignal<N>, N> mRate{};
};

/** RK4 with held inputs and substeps bounded by the fastest block time. */
template<std::size_t N, class Evaluate>
void advance(double* states, index_t count, double elapsedTime, double maxStep, Evaluate evaluate)
{
    const auto steps = static_cast<std::size_t>(std::max(1.0, std::ceil(elapsedTime / maxStep)));
    const double timeStep = elapsedTime / static_cast<double>(steps);
    std::array<double, N> trial{};
    for (std::size_t step = 0; step < steps; ++step) {
        const auto firstStage = evaluate(states);
        for (index_t index = 0; index < count; ++index) {
            trial[index] = states[index] + timeStep * firstStage.mRate[index].mValue / 2.0;
        }
        const auto secondStage = evaluate(trial.data());
        for (index_t index = 0; index < count; ++index) {
            trial[index] = states[index] + timeStep * secondStage.mRate[index].mValue / 2.0;
        }
        const auto thirdStage = evaluate(trial.data());
        for (index_t index = 0; index < count; ++index) {
            trial[index] = states[index] + timeStep * thirdStage.mRate[index].mValue;
        }
        const auto fourthStage = evaluate(trial.data());
        for (index_t index = 0; index < count; ++index) {
            states[index] += timeStep *
                (firstStage.mRate[index].mValue + 2.0 * secondStage.mRate[index].mValue +
                 2.0 * thirdStage.mRate[index].mValue + fourthStage.mRate[index].mValue) /
                6.0;
        }
    }
}
template<std::size_t N, class Locations>
void writeJacobian(const GovernorEvaluation<N>& evaluation,
                   const Locations& locations,
                   const StateData& stateData,
                   MatrixData<double>& matrix,
                   const IOlocs& inputLocs,
                   const SolverMode& mode)
{
    const auto write = [&](index_t row, const GovernorSignal<N>& signal) {
        if (!isAlgebraicOnly(mode)) {
            for (index_t index = 0; index < locations.diffSize; ++index) {
                if (signal.mStateGradient[index] != 0.0) {
                    matrix.assign(row, locations.diffOffset + index, signal.mStateGradient[index]);
                }
            }
        }
        for (std::size_t index = 0;
             index < std::min(inputLocs.size(), signal.mInputGradient.size());
             ++index) {
            if (signal.mInputGradient[index] != 0.0) {
                matrix.assignCheckCol(row, inputLocs[index], signal.mInputGradient[index]);
            }
        }
    };
    if (hasAlgebraic(mode)) {
        write(locations.algOffset, evaluation.mOutput);
        matrix.assign(locations.algOffset, locations.algOffset, -1.0);
    }
    if (hasDifferential(mode)) {
        for (index_t index = 0; index < locations.diffSize; ++index) {
            const index_t row = locations.diffOffset + index;
            write(row, evaluation.mRate[index]);
            matrix.assign(row, row, -stateData.cj);
        }
    }
}
}  // namespace griddyn::governors::detail
