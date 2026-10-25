/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

/** @file
 * @brief Local function derivatives shared by measurement expression nodes.
 *
 * These helpers know mathematical operations, not objects, solver-state
 * locations, or unit bases. Callers combine the returned partials with operand
 * Jacobians and apply measurement gain and unit-conversion scales separately.
 * Keep these helpers independent of grabber classes so other Jacobian-producing
 * code can reuse the local mathematics without adopting an expression tree.
 * Replacing an existing finite-difference path also requires checking its
 * function domains, discontinuity rules, and numerical fallback behavior.
 */

#include "core/CoreExceptions.h"
#include "utilities/functionInterpreter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

namespace griddyn::measurement {
/** @brief Evaluate a registered three-argument function with clamp validation.
 * @param name Canonical lower-case function name matching @p function.
 * @param function Non-null function returned by get3ArgFunction().
 * @param x First argument (the input value for clamp).
 * @param y Second argument (the lower bound for clamp).
 * @param z Third argument (the upper bound for clamp).
 * @return Function value; other mathematical domains follow the function itself.
 * @throws InvalidParameterValue If clamp bounds are reversed or contain NaN.
 */
inline double
    ternaryValue(std::string_view name, function3_t function, double x, double y, double z)
{
    if (name == "clamp" && !(y <= z)) {
        throw InvalidParameterValue("clamp requires ordered, non-NaN bounds");
    }
    return function(x, y, z);
}

/** @brief Compute one scalar function partial before applying the chain rule.
 * @param name Canonical lower-case name of a supported deterministic function.
 * @param values One, two, or three scalar operand values in call order.
 * @param argument Zero-based operand whose partial derivative is requested.
 * @return Local partial derivative with respect to values[argument].
 * @pre The function is registered for this arity and argument < values.size().
 *      Clamp bounds have already been validated with ternaryValue().
 *
 * Unary calls use a registered derivative or a centered numerical difference
 * with a magnitude-scaled step. Supported binary/ternary calls use analytic
 * partials, including arithmetic aliases. This is not a registry validator:
 * unhandled multi-argument operations return zero.
 *
 * Extrema choose the first tied operand; median chooses the stable middle.
 * Clamp follows its input at either bound and its lower bound when bounds are
 * equal. Hypot uses zero partials at the origin. Singularities and invalid
 * mathematical domains are not sanitized. Callers must not treat branch rules
 * as smooth derivatives across a discontinuity.
 * @see reductionPartials, MeasurementExpression
 */
inline double callPartial(std::string_view name, std::span<const double> values, size_t argument)
{
    const double x = values[0];
    if (values.size() == 1) {
        if (auto derivative = getDerivative1ArgFunction(name)) {
            return derivative(x);
        }
        const auto function = get1ArgFunction(name);
        const double step = 1e-6 * std::max(1.0, std::abs(x));
        return (function(x + step) - function(x - step)) / (2.0 * step);
    }
    const double y = values[1];
    const auto function = values.size() == 2 ? get2ArgFunction(name) : nullptr;
    if (function == get2ArgFunction("+")) {
        return 1.0;
    }
    if (function == get2ArgFunction("-")) {
        return argument == 0 ? 1.0 : -1.0;
    }
    if (function == get2ArgFunction("*")) {
        return argument == 0 ? y : x;
    }
    if (function == get2ArgFunction("/")) {
        return argument == 0 ? 1.0 / y : -x / (y * y);
    }
    if (name == "pow" || name == "^") {
        return argument == 0 ? (y == 0.0 ? 0.0 : y * std::pow(x, y - 1.0)) :
                               std::pow(x, y) * std::log(x);
    }
    if (name == "atan2") {
        return argument == 0 ? y / ((x * x) + (y * y)) : -x / ((x * x) + (y * y));
    }
    if (name == "mod" || name == "%") {
        return argument == 0 ? 1.0 : -std::trunc(x / y);
    }
    if (name == "hypot" || name == "mag") {
        const double magnitude =
            values.size() == 2 ? std::hypot(x, y) : std::hypot(x, y, values[2]);
        return magnitude == 0.0 ? 0.0 : values[argument] / magnitude;
    }
    if (name == "clamp") {
        const auto selected = y == values[2] ? 1U : (x < y ? 1U : (x > values[2] ? 2U : 0U));
        return argument == selected ? 1.0 : 0.0;
    }
    if (name == "sum") {
        return 1.0;
    }
    if (name == "product") {
        double result = 1.0;
        for (size_t index = 0; index < values.size(); ++index) {
            if (index != argument) {
                result *= values[index];
            }
        }
        return result;
    }
    if (name == "min" || name == "max") {
        const auto selected = name == "min" ? std::min_element(values.begin(), values.end()) :
                                              std::max_element(values.begin(), values.end());
        return argument == static_cast<size_t>(selected - values.begin()) ? 1.0 : 0.0;
    }
    if (name == "median") {
        std::array<size_t, 3> order{0, 1, 2};
        std::stable_sort(order.begin(), order.end(), [&values](size_t a, size_t b) {
            return values[a] < values[b];
        });
        return argument == order[1] ? 1.0 : 0.0;
    }
    return 0.0;
}

/** @brief Compute all input partials for a reduction as a reusable weight vector.
 * @param name One of sum, avg, product, stdev, median, min, max, absmin, or absmax.
 * @param values Input vector for this sample, after operand transformations.
 * @param[out] weights Replaced with one partial derivative per input element;
 *        cleared for an empty input. Must not alias @p values.
 *
 * Reuse the weights while assembling each element's contribution to a reduction
 * Jacobian, rather than rereading the whole vector for each contribution.
 * Product handles zero factors explicitly. Stdev is the population statistic
 * and has zero weights for a constant vector. Median uses a stable ordering
 * and equally weights the middle pair for an even length. Extrema select the
 * first tie; absolute extrema also apply its sign, choosing +1 at zero.
 *
 * Setup is linear except for median's sorting step. This helper computes no
 * reduction value, solver coordinates, or gain/unit scale. Dot products use
 * the opposite vector as their weights instead.
 * @see callPartial, MeasurementExpression
 */
inline void reductionPartials(std::string_view name,
                              const std::vector<double>& values,
                              std::vector<double>& weights)
{
    weights.assign(values.size(), 0.0);
    if (values.empty()) {
        return;
    }
    if (name == "sum" || name == "avg") {
        std::fill(weights.begin(),
                  weights.end(),
                  name == "sum" ? 1.0 : 1.0 / static_cast<double>(values.size()));
    } else if (name == "product") {
        const auto zeros = std::count(values.begin(), values.end(), 0.0);
        double product = 1.0;
        for (const auto value : values) {
            if (value != 0.0) {
                product *= value;
            }
        }
        for (size_t index = 0; index < values.size(); ++index) {
            weights[index] = zeros == 0 ? product / values[index] :
                                          (zeros == 1 && values[index] == 0.0 ? product : 0.0);
        }
    } else if (name == "stdev") {
        const double mean =
            std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        const double deviation = getArrayFunction("stdev")(values);
        if (deviation != 0.0) {
            for (size_t index = 0; index < values.size(); ++index) {
                weights[index] =
                    (values[index] - mean) / (static_cast<double>(values.size()) * deviation);
            }
        }
    } else if (name == "median") {
        std::vector<size_t> order(values.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::stable_sort(order.begin(), order.end(), [&values](size_t a, size_t b) {
            return values[a] < values[b];
        });
        weights[order[order.size() / 2]] = order.size() % 2 == 0 ? 0.5 : 1.0;
        if (order.size() % 2 == 0) {
            weights[order[(order.size() / 2) - 1]] = 0.5;
        }
    } else {
        const bool absolute = name == "absmin" || name == "absmax";
        const bool minimum = name == "min" || name == "absmin";
        size_t selected = 0;
        for (size_t index = 1; index < values.size(); ++index) {
            const auto value = absolute ? std::abs(values[index]) : values[index];
            const auto previous = absolute ? std::abs(values[selected]) : values[selected];
            if (minimum ? value < previous : value > previous) {
                selected = index;
            }
        }
        weights[selected] = absolute ? (values[selected] >= 0.0 ? 1.0 : -1.0) : 1.0;
    }
}
}  // namespace griddyn::measurement
