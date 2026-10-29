/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "optHelperClasses.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {

/**
 * Solver-neutral compressed-row sparse matrix.
 *
 * Rows are stored in deterministic increasing column order.  Duplicate
 * coordinates are combined while the matrix is assembled, and explicit zero
 * values are omitted.  This is the canonical constraint representation; a
 * dense matrix is created only inside the small native reference solver.
 */
struct NativeSparseMatrix {
    std::size_t rowCount = 0;  //!< number of matrix rows
    std::size_t columnCount = 0;  //!< number of matrix columns
    std::vector<std::size_t> rowStarts;  //!< CSR offsets; has rowCount + 1 entries
    std::vector<std::size_t> columnIndices;  //!< column index for each stored value
    std::vector<double> values;  //!< nonzero coefficients in row-major order

    /** Creates an empty sparse matrix. */
    NativeSparseMatrix() = default;

    /** Creates an empty matrix with the specified dimensions.
     * @param[in] rows number of rows
     * @param[in] columns number of columns
     */
    NativeSparseMatrix(std::size_t rows, std::size_t columns):
        rowCount(rows), columnCount(columns), rowStarts(rows + 1, 0)
    {
    }

    /** Resets dimensions and clears all stored coefficients.
     * @param[in] rows new row count
     * @param[in] columns new column count
     */
    void setDimensions(std::size_t rows, std::size_t columns)
    {
        rowCount = rows;
        columnCount = columns;
        rowStarts.assign(rows + 1, 0);
        columnIndices.clear();
        values.clear();
    }

    /** Clears dimensions and all stored coefficients. */
    void clear()
    {
        rowCount = 0;
        columnCount = 0;
        rowStarts.clear();
        columnIndices.clear();
        values.clear();
    }

    /** Checks whether the matrix contains any stored coefficients. */
    bool empty() const { return values.empty(); }

    /** Gets the number of stored coefficients. */
    std::size_t size() const { return values.size(); }

    /** Assign a row-major dense vector, primarily for compact solver tests. */
    void assignDense(const std::vector<double>& dense)
    {
        if (dense.size() != rowCount * columnCount) {
            clear();
            return;
        }
        rowStarts.assign(rowCount + 1, 0);
        columnIndices.clear();
        values.clear();
        for (std::size_t row = 0; row < rowCount; ++row) {
            for (std::size_t column = 0; column < columnCount; ++column) {
                const double value = dense[(row * columnCount) + column];
                if (value != 0.0) {
                    columnIndices.push_back(column);
                    values.push_back(value);
                }
            }
            rowStarts[row + 1] = values.size();
        }
    }

    /** Return one coefficient, or zero when the coordinate is not stored. */
    double coefficient(std::size_t row, std::size_t column) const
    {
        if ((row >= rowCount) || (column >= columnCount) || (rowStarts.size() != rowCount + 1)) {
            return 0.0;
        }
        const auto begin = columnIndices.begin() + static_cast<std::ptrdiff_t>(rowStarts[row]);
        const auto end = columnIndices.begin() + static_cast<std::ptrdiff_t>(rowStarts[row + 1]);
        const auto entry = std::lower_bound(begin, end, column);
        if ((entry == end) || (*entry != column)) {
            return 0.0;
        }
        return values[static_cast<std::size_t>(entry - columnIndices.begin())];
    }

    /** Evaluate one sparse row against a decision vector. */
    double rowDot(std::size_t row, const std::vector<double>& decisionValues) const
    {
        double result = 0.0;
        if ((row >= rowCount) || (decisionValues.size() < columnCount) ||
            (rowStarts.size() != rowCount + 1)) {
            return result;
        }
        for (std::size_t entry = rowStarts[row]; entry < rowStarts[row + 1]; ++entry) {
            result += values[entry] * decisionValues[columnIndices[entry]];
        }
        return result;
    }

    /** Expand into row-major storage for the native dense reference backend. */
    std::vector<double> toDense() const
    {
        std::vector<double> dense(rowCount * columnCount, 0.0);
        if (rowStarts.size() != rowCount + 1) {
            return dense;
        }
        for (std::size_t row = 0; row < rowCount; ++row) {
            for (std::size_t entry = rowStarts[row]; entry < rowStarts[row + 1]; ++entry) {
                dense[(row * columnCount) + columnIndices[entry]] = values[entry];
            }
        }
        return dense;
    }

    /** Checks compressed-row dimensions, ordering, and coefficient values.
     * @param[out] error optional destination for a validation diagnostic
     * @return true when storage is canonical and internally consistent
     */
    bool validate(std::string* error = nullptr) const
    {
        const auto fail = [error](std::string message) {
            if (error != nullptr) {
                *error = std::move(message);
            }
            return false;
        };
        if (rowStarts.size() != rowCount + 1) {
            return fail("sparse matrix rowStarts has the wrong size");
        }
        if (rowStarts.front() != 0 || rowStarts.back() != values.size() ||
            columnIndices.size() != values.size()) {
            return fail("sparse matrix compressed storage is inconsistent");
        }
        for (std::size_t row = 0; row < rowCount; ++row) {
            if (rowStarts[row] > rowStarts[row + 1]) {
                return fail("sparse matrix rowStarts is not monotonic");
            }
            std::size_t previousColumn = 0;
            bool first = true;
            for (std::size_t entry = rowStarts[row]; entry < rowStarts[row + 1]; ++entry) {
                const auto column = columnIndices[entry];
                if ((column >= columnCount) || (!first && (column <= previousColumn))) {
                    return fail("sparse matrix columns are invalid or not canonical");
                }
                if (!std::isfinite(values[entry]) || (values[entry] == 0.0)) {
                    return fail("sparse matrix contains an invalid value");
                }
                previousColumn = column;
                first = false;
            }
        }
        return true;
    }

    /** Compares dimensions and compressed storage for exact equality. */
    bool operator==(const NativeSparseMatrix&) const = default;
};

/** Classification produced while materializing a native DC LP/QP/MILP. */
enum class NativeProblemClass {
    INVALID,  //!< Storage has not been materialized or did not pass validation.
    SUPPORTED_LINEAR,  //!< Continuous DC linear objective and constraints.
    SUPPORTED_CONVEX_DIAGONAL_QUADRATIC,  //!< Continuous DC convex diagonal QP.
    SUPPORTED_MIXED_INTEGER_LINEAR,  //!< DC mixed-integer linear objective and constraints.
    UNSUPPORTED_MODE,  //!< Requested optimization mode is outside the native scope.
    UNSUPPORTED_NONLINEAR,  //!< Nonlinear constraints or objective terms are present.
    UNSUPPORTED_INTEGER,  //!< Integer or binary variables are present.
    NONCONVEX,  //!< The quadratic objective is not convex.
};

/**
 * Solver-neutral sparse representation of a GridDyn optimization problem.
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
    std::uint64_t modelVersion = 0;  //!< source-model revision used to build this snapshot
    OptimizationMode mode{};  //!< optimization formulation used for materialization
    NativeProblemClass classification = NativeProblemClass::INVALID;  //!< supported solver class
    bool valid = false;  //!< whether validation succeeded
    std::string validationError;  //!< reason validation or materialization failed

    std::size_t variableCount = 0;  //!< number of decision variables
    std::size_t constraintCount = 0;  //!< number of constraint rows

    std::vector<double> initialValues;  //!< initial decision vector in GridDyn variable order
    std::vector<double> variableLowerBounds;  //!< lower bound for each decision variable
    std::vector<double> variableUpperBounds;  //!< upper bound for each decision variable
    std::vector<double> linearObjective;  //!< linear objective coefficient c
    std::vector<double> quadraticObjective;  //!< diagonal quadratic coefficient q
    double objectiveConstant = 0.0;  //!< constant objective contribution

    std::vector<double> constraintLowerBounds;  //!< lower bound for each affine row
    std::vector<double> constraintUpperBounds;  //!< upper bound for each affine row
    std::vector<double> constraintOffsets;  //!< affine row offsets retained outside the matrix
    NativeSparseMatrix constraintMatrix;  //!< canonical compressed-row constraint matrix A
    std::vector<double> initialConstraintValues;  //!< row values evaluated at initialValues
    std::vector<double> initialGradient;  //!< objective gradient evaluated at initialValues

    std::vector<double> variableTypes;  //!< continuous, integer, or binary marker per variable
    std::vector<double> tolerances;  //!< variable scaling or tolerance hints
    std::vector<std::string> variableNames;  //!< names in GridDyn variable order
    std::vector<std::string> constraintNames;  //!< names in constraint-row order

    /**
     * Evaluate one canonical affine row.
     * @param row zero-based row index.
     * @param values decision vector in the original GridDyn variable ordering.
     * @return `constraintMatrix[row] * values + constraintOffsets[row]`.
     */
    double constraintValue(std::size_t row, const std::vector<double>& values) const
    {
        double value = constraintOffsets[row];
        value += constraintMatrix.rowDot(row, values);
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
            gradient[column] =
                linearObjective[column] + 2.0 * quadraticObjective[column] * values[column];
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
            if ((std::isinf(lower) && (lower > 0.0)) || (std::isinf(upper) && (upper < 0.0))) {
                return false;
            }
            return lower <= upper;
        };

        if ((initialValues.size() != variableCount) ||
            (variableLowerBounds.size() != variableCount) ||
            (variableUpperBounds.size() != variableCount) ||
            (linearObjective.size() != variableCount) ||
            (quadraticObjective.size() != variableCount) ||
            (variableTypes.size() != variableCount) || (tolerances.size() != variableCount) ||
            (initialGradient.size() != variableCount)) {
            return fail("native problem variable vectors do not match variableCount");
        }
        if ((constraintLowerBounds.size() != constraintCount) ||
            (constraintUpperBounds.size() != constraintCount) ||
            (constraintOffsets.size() != constraintCount) ||
            (initialConstraintValues.size() != constraintCount) ||
            (constraintNames.size() != constraintCount) ||
            (variableNames.size() != variableCount) || !std::isfinite(objectiveConstant)) {
            return fail("native problem constraint, name, or objective storage has invalid size");
        }
        std::string matrixError;
        if ((constraintMatrix.rowCount != constraintCount) ||
            (constraintMatrix.columnCount != variableCount) ||
            !constraintMatrix.validate(&matrixError)) {
            return fail("native problem has invalid sparse constraint storage: " + matrixError);
        }
        if (vectorHasNonFinite(initialValues) || vectorHasNonFinite(linearObjective) ||
            vectorHasNonFinite(quadraticObjective) || vectorHasNonFinite(constraintOffsets) ||
            vectorHasNonFinite(initialConstraintValues) || vectorHasNonFinite(initialGradient) ||
            vectorHasNonFinite(tolerances) || vectorHasNonFinite(variableTypes)) {
            return fail("native problem contains a non-finite coefficient or value");
        }
        for (std::size_t column = 0; column < variableCount; ++column) {
            if (!validBound(variableLowerBounds[column], variableUpperBounds[column])) {
                return fail("native problem has an invalid variable bound");
            }
            const double type = variableTypes[column];
            if ((type != CONTINUOUS_OBJECTIVE_VARIABLE) && (type != INTEGER_OBJECTIVE_VARIABLE) &&
                (type != BINARY_OBJECTIVE_VARIABLE)) {
                return fail("native problem has an unknown variable type");
            }
            if ((type == BINARY_OBJECTIVE_VARIABLE) &&
                ((variableLowerBounds[column] < 0.0) || (variableUpperBounds[column] > 1.0))) {
                return fail("native problem binary variable bounds must lie in [0, 1]");
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
