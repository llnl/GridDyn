/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "optHelperClasses.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {

/** Classification produced while materializing a native continuous LP/QP. */
enum class NativeProblemClass {
    INVALID,  //!< Storage has not been materialized or did not pass validation.
    SUPPORTED_LINEAR,  //!< Continuous DC linear objective and constraints.
    SUPPORTED_CONVEX_DIAGONAL_QUADRATIC,  //!< Continuous DC convex diagonal QP.
    UNSUPPORTED_MODE,  //!< Requested optimization mode is outside the native scope.
    UNSUPPORTED_NONLINEAR,  //!< Nonlinear constraints or objective terms are present.
    UNSUPPORTED_INTEGER,  //!< Integer or binary variables are present.
    NONCONVEX,  //!< The quadratic objective is not convex.
};

/**
 * Solver-neutral dense representation of a GridDyn optimization problem.
 *
 * The objective is
 *
 *   objectiveConstant + linearObjective' * x
 *       + sum(quadraticObjective[i] * x[i] * x[i])
 *
 * and each row is represented as
 *
 *   constraintLowerBounds[r] <= A[r] * x + constraintOffsets[r]
 *       <= constraintUpperBounds[r].
 *
 * A backend that accepts standard bounded rows uses
 * `constraintLowerBounds[r] - constraintOffsets[r]` and
 * `constraintUpperBounds[r] - constraintOffsets[r]`.  Keeping the affine
 * offset here makes phase-shifted branch constraints explicit and lets a
 * future HiGHS adapter consume the same model without re-deriving physics.
 */
struct NativeQpProblem {
    std::uint64_t modelVersion = 0;
    OptimizationMode mode{};
    NativeProblemClass classification = NativeProblemClass::INVALID;
    bool valid = false;
    std::string validationError;

    std::size_t variableCount = 0;
    std::size_t constraintCount = 0;

    std::vector<double> initialValues;
    std::vector<double> variableLowerBounds;
    std::vector<double> variableUpperBounds;
    std::vector<double> linearObjective;
    std::vector<double> quadraticObjective;
    double objectiveConstant = 0.0;

    std::vector<double> constraintLowerBounds;
    std::vector<double> constraintUpperBounds;
    std::vector<double> constraintOffsets;
    std::vector<double> constraintMatrix;  //!< row-major A, with constraintCount * variableCount entries
    std::vector<double> initialConstraintValues;
    std::vector<double> initialGradient;

    std::vector<double> variableTypes;
    std::vector<double> tolerances;
    std::vector<std::string> variableNames;
    std::vector<std::string> constraintNames;

    /**
     * Evaluate one canonical affine row.
     * @param row zero-based row index.
     * @param values decision vector in the original GridDyn variable ordering.
     * @return `constraintMatrix[row] * values + constraintOffsets[row]`.
     */
    double constraintValue(std::size_t row, const std::vector<double>& values) const
    {
        double value = constraintOffsets[row];
        const auto rowStart = row * variableCount;
        for (std::size_t column = 0; column < variableCount; ++column) {
            value += constraintMatrix[rowStart + column] * values[column];
        }
        return value;
    }

    /**
     * Return the lower side for a backend bounded-row representation.
     * @param row zero-based row index.
     * @return `constraintLowerBounds[row] - constraintOffsets[row]`.
     */
    double solverConstraintLowerBound(std::size_t row) const
    {
        return constraintLowerBounds[row] - constraintOffsets[row];
    }

    /**
     * Return the upper side for a backend bounded-row representation.
     * @param row zero-based row index.
     * @return `constraintUpperBounds[row] - constraintOffsets[row]`.
     */
    double solverConstraintUpperBound(std::size_t row) const
    {
        return constraintUpperBounds[row] - constraintOffsets[row];
    }

    /**
     * Evaluate the canonical objective.
     * @param values decision vector in the original GridDyn variable ordering.
     * @return `objectiveConstant + c' * values + sum(q[i] * values[i]^2)`.
     */
    double objectiveValue(const std::vector<double>& values) const
    {
        double objective = objectiveConstant;
        for (std::size_t column = 0; column < variableCount; ++column) {
            objective += linearObjective[column] * values[column] +
                quadraticObjective[column] * values[column] * values[column];
        }
        return objective;
    }

    /**
     * Evaluate the canonical objective gradient.
     * @param values decision vector in the original GridDyn variable ordering.
     * @return gradient with entries `linearObjective[i] +
     *   2 * quadraticObjective[i] * values[i]`.
     */
    std::vector<double> objectiveGradient(const std::vector<double>& values) const
    {
        std::vector<double> gradient(variableCount, 0.0);
        for (std::size_t column = 0; column < variableCount; ++column) {
            gradient[column] = linearObjective[column] +
                2.0 * quadraticObjective[column] * values[column];
        }
        return gradient;
    }

    /**
     * Validate backend-neutral storage independently of a numerical solver.
     *
     * Coefficients, offsets, initial values, types, tolerances, and the
     * objective constant must be finite. Infinity is permitted only as a bound
     * side and represents an unbounded LP/QP side. This check validates storage
     * shape and numeric legality; it does not prove feasibility or optimality.
     *
     * @param error optional destination for a concise validation failure.
     * @return true when all vectors have compatible sizes and legal values.
     */
    bool validate(std::string* error = nullptr) const
    {
        const auto fail = [error](std::string message) {
            if (error != nullptr) {
                *error = std::move(message);
            }
            return false;
        };
        const auto vectorHasNonFinite = [](const std::vector<double>& vector) {
            for (const auto value : vector) {
                if (!std::isfinite(value)) {
                    return true;
                }
            }
            return false;
        };
        const auto validBound = [](double lower, double upper) {
            if (std::isnan(lower) || std::isnan(upper)) {
                return false;
            }
            if ((std::isinf(lower) && (lower > 0.0)) ||
                (std::isinf(upper) && (upper < 0.0))) {
                return false;
            }
            return lower <= upper;
        };

        if ((initialValues.size() != variableCount) ||
            (variableLowerBounds.size() != variableCount) ||
            (variableUpperBounds.size() != variableCount) ||
            (linearObjective.size() != variableCount) ||
            (quadraticObjective.size() != variableCount) ||
            (variableTypes.size() != variableCount) ||
            (tolerances.size() != variableCount) ||
            (initialGradient.size() != variableCount)) {
            return fail("native problem variable vectors do not match variableCount");
        }
        if ((constraintLowerBounds.size() != constraintCount) ||
            (constraintUpperBounds.size() != constraintCount) ||
            (constraintOffsets.size() != constraintCount) ||
            (initialConstraintValues.size() != constraintCount) ||
            (constraintMatrix.size() != constraintCount * variableCount) ||
            (constraintNames.size() != constraintCount) ||
            (variableNames.size() != variableCount) || !std::isfinite(objectiveConstant)) {
            return fail("native problem constraint, name, or objective storage has invalid size");
        }
        if (vectorHasNonFinite(initialValues) || vectorHasNonFinite(linearObjective) ||
            vectorHasNonFinite(quadraticObjective) || vectorHasNonFinite(constraintMatrix) ||
            vectorHasNonFinite(constraintOffsets) || vectorHasNonFinite(initialConstraintValues) ||
            vectorHasNonFinite(initialGradient) || vectorHasNonFinite(tolerances) ||
            vectorHasNonFinite(variableTypes)) {
            return fail("native problem contains a non-finite coefficient or value");
        }
        for (std::size_t column = 0; column < variableCount; ++column) {
            if (!validBound(variableLowerBounds[column], variableUpperBounds[column])) {
                return fail("native problem has an invalid variable bound");
            }
        }
        for (std::size_t row = 0; row < constraintCount; ++row) {
            if (!validBound(constraintLowerBounds[row], constraintUpperBounds[row])) {
                return fail("native problem has an invalid constraint bound");
            }
        }
        return true;
    }
};

}  // namespace griddyn
