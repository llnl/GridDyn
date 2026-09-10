/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "nativeDenseSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace griddyn {
namespace {

    constexpr double kRegularization = 1e-10;

    enum class ConstraintType {
        EQUALITY,
        INEQUALITY,
    };

    // The active-set core stores every row as coeffs' * y <= rhs. Equality rows
    // are kept as exact rows in the KKT system; bounded NativeQpProblem rows are
    // split into one lower-side and/or one upper-side inequality here.
    struct DenseConstraint {
        std::vector<double> coefficients;
        double rhs = 0.0;
        ConstraintType type = ConstraintType::INEQUALITY;
        bool variableBound = false;
    };

    // DenseModel is an internal, one-sided view of NativeQpProblem. Variable
    // bounds remain separate so they can be identified as bound constraints when
    // choosing and releasing the working set.
    struct DenseModel {
        std::size_t variableCount = 0;
        std::vector<double> lowerBounds;
        std::vector<double> upperBounds;
        std::vector<double> linearObjective;
        std::vector<double> quadraticObjective;
        std::vector<DenseConstraint> constraints;
    };

    // Presolve solves in scaled coordinates z, with x = variableScale .* z. The
    // free-column map and fixed values allow the final result to be expanded back
    // into the original NativeQpProblem ordering and units.
    struct SolverTransform {
        NativeQpProblem problem;
        std::vector<double> variableScale;
        std::vector<std::size_t> freeColumns;
        std::vector<bool> fixedColumns;
        std::vector<double> fixedValues;
    };

    struct PresolveResult {
        bool successful = true;
        NativeSolveStatus status = NativeSolveStatus::NUMERICAL_FAILURE;
        std::string message;
    };

    struct LinearSolveResult {
        bool solved = false;
        bool singular = false;
        std::vector<double> solution;
    };

    double dot(const std::vector<double>& lhs, const std::vector<double>& rhs)
    {
        return std::inner_product(lhs.begin(), lhs.end(), rhs.begin(), 0.0);
    }

    bool finiteVector(const std::vector<double>& values)
    {
        return std::all_of(values.begin(), values.end(), [](double value) {
            return std::isfinite(value);
        });
    }

    LinearSolveResult solveLinearSystem(std::vector<double> matrix,
                                        std::vector<double> rhs,
                                        double pivotTolerance)
    {
        // Partial-pivot Gaussian elimination is sufficient for the small dense
        // backend. The caller owns the KKT construction; taking the matrix and
        // right-hand side by value lets this routine eliminate in place without
        // changing the caller's diagnostic data.
        const auto dimension = rhs.size();
        if (matrix.size() != dimension * dimension) {
            return {.singular = true};
        }
        if (dimension == 0) {
            return {.solved = true, .solution = {}};
        }

        double matrixScale = 0.0;
        for (const auto value : matrix) {
            matrixScale = (std::max)(matrixScale, std::abs(value));
        }
        const double pivotLimit = pivotTolerance * (std::max)(1.0, matrixScale);

        for (std::size_t column = 0; column < dimension; ++column) {
            std::size_t pivotRow = column;
            double pivotMagnitude = std::abs(matrix[column * dimension + column]);
            for (std::size_t row = column + 1; row < dimension; ++row) {
                const double candidateMagnitude = std::abs(matrix[row * dimension + column]);
                if (candidateMagnitude > pivotMagnitude) {
                    pivotMagnitude = candidateMagnitude;
                    pivotRow = row;
                }
            }
            if (!std::isfinite(pivotMagnitude) || (pivotMagnitude <= pivotLimit)) {
                return {.singular = true};
            }
            if (pivotRow != column) {
                for (std::size_t entry = column; entry < dimension; ++entry) {
                    std::swap(matrix[column * dimension + entry],
                              matrix[pivotRow * dimension + entry]);
                }
                std::swap(rhs[column], rhs[pivotRow]);
            }

            for (std::size_t row = column + 1; row < dimension; ++row) {
                const double factor =
                    matrix[row * dimension + column] / matrix[column * dimension + column];
                if (factor == 0.0) {
                    continue;
                }
                matrix[row * dimension + column] = 0.0;
                for (std::size_t entry = column + 1; entry < dimension; ++entry) {
                    matrix[row * dimension + entry] -= factor * matrix[column * dimension + entry];
                }
                rhs[row] -= factor * rhs[column];
            }
        }

        std::vector<double> solution(dimension, 0.0);
        for (std::size_t row = dimension; row-- > 0;) {
            double value = rhs[row];
            for (std::size_t column = row + 1; column < dimension; ++column) {
                value -= matrix[row * dimension + column] * solution[column];
            }
            const double pivot = matrix[row * dimension + row];
            if (std::abs(pivot) <= pivotLimit) {
                return {.singular = true};
            }
            solution[row] = value / pivot;
        }
        if (!finiteVector(solution)) {
            return {.solved = false, .solution = std::move(solution)};
        }
        return {.solved = true, .solution = std::move(solution)};
    }

    bool isFiniteLower(double bound)
    {
        return std::isfinite(bound);
    }

    bool isFiniteUpper(double bound)
    {
        return std::isfinite(bound);
    }

    bool isEquality(double lower, double upper, double tolerance)
    {
        return std::isfinite(lower) && std::isfinite(upper) &&
            std::abs(lower - upper) <=
            tolerance * (1.0 + (std::max)(std::abs(lower), std::abs(upper)));
    }

    void addRowConstraints(const NativeQpProblem& problem,
                           DenseModel& model,
                           bool includeEqualities = true)
    {
        // Move each affine offset into the row bounds before splitting a bounded
        // row. The lower side -a*y <= -lower uses the same sign convention as the
        // upper side a*y <= upper, which makes both sides interchangeable in the
        // blocking-step and multiplier logic.
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            const auto rowStart = row * problem.variableCount;
            std::vector<double> coefficients(problem.variableCount, 0.0);
            std::copy_n(problem.constraintMatrix.begin() + static_cast<std::ptrdiff_t>(rowStart),
                        problem.variableCount,
                        coefficients.begin());
            const double lower = problem.solverConstraintLowerBound(row);
            const double upper = problem.solverConstraintUpperBound(row);
            if (isEquality(lower, upper, 1e-10)) {
                if (includeEqualities) {
                    model.constraints.push_back(
                        {std::move(coefficients), 0.5 * (lower + upper), ConstraintType::EQUALITY});
                }
                continue;
            }
            if (isFiniteLower(lower)) {
                auto lowerCoefficients = coefficients;
                for (auto& coefficient : lowerCoefficients) {
                    coefficient = -coefficient;
                }
                model.constraints.push_back(
                    {std::move(lowerCoefficients), -lower, ConstraintType::INEQUALITY});
            }
            if (isFiniteUpper(upper)) {
                model.constraints.push_back(
                    {std::move(coefficients), upper, ConstraintType::INEQUALITY});
            }
        }
    }

    void addVariableBoundConstraints(DenseModel& model)
    {
        // Variable bounds are represented as ordinary one-sided rows only after
        // presolve has removed fixed variables. Marking them as variableBound
        // preserves the distinction used by active-set degeneracy handling.
        for (std::size_t column = 0; column < model.variableCount; ++column) {
            if (isFiniteLower(model.lowerBounds[column])) {
                std::vector<double> coefficients(model.variableCount, 0.0);
                coefficients[column] = -1.0;
                model.constraints.push_back(
                    {std::move(coefficients),
                     -model.lowerBounds[column],
                     isEquality(model.lowerBounds[column], model.upperBounds[column], 1e-10) ?
                         ConstraintType::EQUALITY :
                         ConstraintType::INEQUALITY,
                     true});
            }
            if (isFiniteUpper(model.upperBounds[column]) &&
                !isEquality(model.lowerBounds[column], model.upperBounds[column], 1e-10)) {
                std::vector<double> coefficients(model.variableCount, 0.0);
                coefficients[column] = 1.0;
                model.constraints.push_back({std::move(coefficients),
                                             model.upperBounds[column],
                                             ConstraintType::INEQUALITY,
                                             true});
            }
        }
    }

    DenseModel makeDenseModel(const NativeQpProblem& problem)
    {
        // NativeQpProblem remains the canonical public representation. This
        // function creates the solver's one-sided rows without modifying it.
        DenseModel model;
        model.variableCount = problem.variableCount;
        model.lowerBounds = problem.variableLowerBounds;
        model.upperBounds = problem.variableUpperBounds;
        model.linearObjective = problem.linearObjective;
        model.quadraticObjective = problem.quadraticObjective;
        addRowConstraints(problem, model);
        addVariableBoundConstraints(model);
        return model;
    }

    double scaledBound(double bound, double scale)
    {
        return std::isfinite(bound) ? bound * scale : bound;
    }

    NativeQpProblem makeScaledProblem(const NativeQpProblem& problem,
                                      std::vector<double>& variableScale)
    {
        // Scale only the internal copy. For x = S*z, columns become A*S,
        // variable bounds become bounds/S, and diagonal objective coefficients
        // become q*S^2. Affine offsets are moved into the bounded-row sides so
        // the numerical core can work with a zero-offset model.
        NativeQpProblem scaled = problem;
        variableScale.assign(problem.variableCount, 1.0);

        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            double columnMagnitude = 0.0;
            for (std::size_t row = 0; row < problem.constraintCount; ++row) {
                columnMagnitude =
                    (std::max)(columnMagnitude,
                               std::abs(
                                   problem.constraintMatrix[row * problem.variableCount + column]));
            }
            // x = scale * z.  Scaling only large columns avoids magnifying small
            // physical coefficients while keeping the internal coordinates close
            // to unit magnitude for normal GridDyn cases.
            variableScale[column] = 1.0 / (std::max)(1.0, columnMagnitude);
            const double scale = variableScale[column];
            scaled.initialValues[column] = problem.initialValues[column] / scale;
            scaled.variableLowerBounds[column] =
                scaledBound(problem.variableLowerBounds[column], 1.0 / scale);
            scaled.variableUpperBounds[column] =
                scaledBound(problem.variableUpperBounds[column], 1.0 / scale);
            scaled.linearObjective[column] = problem.linearObjective[column] * scale;
            scaled.quadraticObjective[column] = problem.quadraticObjective[column] * scale * scale;
        }

        scaled.constraintOffsets.assign(problem.constraintCount, 0.0);
        scaled.initialConstraintValues.assign(problem.constraintCount, 0.0);
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            const auto rowStart = row * problem.variableCount;
            double rowMagnitude = 0.0;
            for (std::size_t column = 0; column < problem.variableCount; ++column) {
                const double coefficient =
                    problem.constraintMatrix[rowStart + column] * variableScale[column];
                scaled.constraintMatrix[rowStart + column] = coefficient;
                rowMagnitude = (std::max)(rowMagnitude, std::abs(coefficient));
            }
            const double rowScale = 1.0 / (std::max)(1.0, rowMagnitude);
            for (std::size_t column = 0; column < problem.variableCount; ++column) {
                scaled.constraintMatrix[rowStart + column] *= rowScale;
            }
            const double lower = problem.solverConstraintLowerBound(row);
            const double upper = problem.solverConstraintUpperBound(row);
            scaled.constraintLowerBounds[row] = scaledBound(lower, rowScale);
            scaled.constraintUpperBounds[row] = scaledBound(upper, rowScale);
            double initialValue = 0.0;
            for (std::size_t column = 0; column < problem.variableCount; ++column) {
                initialValue +=
                    scaled.constraintMatrix[rowStart + column] * scaled.initialValues[column];
            }
            scaled.initialConstraintValues[row] = initialValue;
        }
        scaled.initialGradient = scaled.objectiveGradient(scaled.initialValues);
        return scaled;
    }

    PresolveResult eliminateFixedVariables(SolverTransform& transform,
                                           const NativeDenseSolverOptions& options)
    {
        // A variable whose two finite bounds agree is substituted before KKT
        // construction. Its objective contribution becomes a constant, its row
        // contribution is moved to the row bounds, and only free columns remain
        // in the reduced problem.
        const auto& full = transform.problem;
        transform.fixedColumns.assign(full.variableCount, false);
        transform.fixedValues.assign(full.variableCount, 0.0);
        transform.freeColumns.clear();

        for (std::size_t column = 0; column < full.variableCount; ++column) {
            if (isEquality(full.variableLowerBounds[column],
                           full.variableUpperBounds[column],
                           options.feasibilityTolerance)) {
                transform.fixedColumns[column] = true;
                transform.fixedValues[column] =
                    0.5 * (full.variableLowerBounds[column] + full.variableUpperBounds[column]);
            } else {
                transform.freeColumns.push_back(column);
            }
        }
        if (transform.freeColumns.size() == full.variableCount) {
            return {};
        }

        NativeQpProblem reduced = full;
        reduced.variableCount = transform.freeColumns.size();
        reduced.initialValues.clear();
        reduced.variableLowerBounds.clear();
        reduced.variableUpperBounds.clear();
        reduced.linearObjective.clear();
        reduced.quadraticObjective.clear();
        reduced.initialGradient.clear();
        reduced.variableTypes.clear();
        reduced.tolerances.clear();
        reduced.variableNames.clear();
        reduced.constraintMatrix.assign(full.constraintCount * reduced.variableCount, 0.0);
        reduced.initialValues.reserve(reduced.variableCount);
        reduced.variableLowerBounds.reserve(reduced.variableCount);
        reduced.variableUpperBounds.reserve(reduced.variableCount);
        reduced.linearObjective.reserve(reduced.variableCount);
        reduced.quadraticObjective.reserve(reduced.variableCount);
        reduced.variableTypes.reserve(reduced.variableCount);
        reduced.tolerances.reserve(reduced.variableCount);
        reduced.variableNames.reserve(reduced.variableCount);

        for (const auto column : transform.freeColumns) {
            reduced.initialValues.push_back(full.initialValues[column]);
            reduced.variableLowerBounds.push_back(full.variableLowerBounds[column]);
            reduced.variableUpperBounds.push_back(full.variableUpperBounds[column]);
            reduced.linearObjective.push_back(full.linearObjective[column]);
            reduced.quadraticObjective.push_back(full.quadraticObjective[column]);
            reduced.variableTypes.push_back(full.variableTypes[column]);
            reduced.tolerances.push_back(full.tolerances[column]);
            reduced.variableNames.push_back(full.variableNames[column]);
        }

        for (std::size_t column = 0; column < full.variableCount; ++column) {
            if (!transform.fixedColumns[column]) {
                continue;
            }
            const double fixed = transform.fixedValues[column];
            reduced.objectiveConstant += full.linearObjective[column] * fixed +
                full.quadraticObjective[column] * fixed * fixed;
        }

        for (std::size_t row = 0; row < full.constraintCount; ++row) {
            double fixedContribution = 0.0;
            for (std::size_t column = 0; column < full.variableCount; ++column) {
                if (transform.fixedColumns[column]) {
                    fixedContribution += full.constraintMatrix[row * full.variableCount + column] *
                        transform.fixedValues[column];
                }
            }
            reduced.constraintLowerBounds[row] =
                scaledBound(full.constraintLowerBounds[row] - fixedContribution, 1.0);
            reduced.constraintUpperBounds[row] =
                scaledBound(full.constraintUpperBounds[row] - fixedContribution, 1.0);
            if (reduced.constraintLowerBounds[row] > reduced.constraintUpperBounds[row] +
                    options.feasibilityTolerance *
                        (1.0 +
                         (std::max)(std::abs(reduced.constraintLowerBounds[row]),
                                    std::abs(reduced.constraintUpperBounds[row])))) {
                return {.successful = false,
                        .status = NativeSolveStatus::INFEASIBLE,
                        .message = "fixed-variable presolve found inconsistent row bounds"};
            }
            const auto fullRowStart = row * full.variableCount;
            const auto reducedRowStart = row * reduced.variableCount;
            for (std::size_t reducedColumn = 0; reducedColumn < reduced.variableCount;
                 ++reducedColumn) {
                reduced.constraintMatrix[reducedRowStart + reducedColumn] =
                    full.constraintMatrix[fullRowStart + transform.freeColumns[reducedColumn]];
            }
            reduced.initialConstraintValues[row] =
                full.initialConstraintValues[row] - fixedContribution;
        }
        reduced.initialGradient = reduced.objectiveGradient(reduced.initialValues);
        transform.problem = std::move(reduced);
        return {};
    }

    PresolveResult reduceRedundantEqualities(SolverTransform& transform,
                                             const NativeDenseSolverOptions& options)
    {
        // Build an incremental row-echelon basis of equality rows. A row with no
        // remaining pivot is redundant when its right-hand side also eliminates
        // to zero; a nonzero residual proves inconsistent equalities instead of
        // allowing a singular KKT system to obscure the real diagnosis.
        const auto& problem = transform.problem;
        std::vector<bool> keep(problem.constraintCount, true);
        std::vector<std::vector<double>> basis;
        std::vector<double> basisRhs;
        std::vector<std::size_t> basisPivots;
        basis.reserve(problem.constraintCount);
        basisRhs.reserve(problem.constraintCount);
        basisPivots.reserve(problem.constraintCount);

        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            const double lower = problem.solverConstraintLowerBound(row);
            const double upper = problem.solverConstraintUpperBound(row);
            if (!isEquality(lower, upper, options.feasibilityTolerance)) {
                continue;
            }
            std::vector<double> work(problem.variableCount, 0.0);
            const auto rowStart = row * problem.variableCount;
            std::copy_n(problem.constraintMatrix.begin() + static_cast<std::ptrdiff_t>(rowStart),
                        problem.variableCount,
                        work.begin());
            double workRhs = 0.5 * (lower + upper);
            for (std::size_t basisRow = 0; basisRow < basis.size(); ++basisRow) {
                const auto& pivotRow = basis[basisRow];
                const auto pivotColumn = basisPivots[basisRow];
                const auto pivot = pivotRow[pivotColumn];
                if (std::abs(pivot) <= options.pivotTolerance) {
                    continue;
                }
                const double factor = work[pivotColumn] / pivot;
                if (factor == 0.0) {
                    continue;
                }
                for (std::size_t column = 0; column < problem.variableCount; ++column) {
                    work[column] -= factor * pivotRow[column];
                }
                workRhs -= factor * basisRhs[basisRow];
            }
            std::size_t pivotColumn = problem.variableCount;
            double pivotMagnitude = 0.0;
            for (std::size_t column = 0; column < problem.variableCount; ++column) {
                if (std::abs(work[column]) > pivotMagnitude) {
                    pivotMagnitude = std::abs(work[column]);
                    pivotColumn = column;
                }
            }
            if (pivotColumn == problem.variableCount) {
                if (std::abs(workRhs) > options.feasibilityTolerance * (1.0 + std::abs(workRhs))) {
                    return {.successful = false,
                            .status = NativeSolveStatus::INFEASIBLE,
                            .message = "equality presolve found inconsistent redundant row " +
                                std::to_string(row) + " (residual=" + std::to_string(workRhs) +
                                ")"};
                }
                keep[row] = false;
                continue;
            }
            const double pivot = work[pivotColumn];
            for (auto& coefficient : work) {
                coefficient /= pivot;
            }
            basis.push_back(std::move(work));
            basisRhs.push_back(workRhs / pivot);
            basisPivots.push_back(pivotColumn);
        }

        std::size_t removed = 0;
        for (const auto value : keep) {
            removed += value ? 0U : 1U;
        }
        if (removed == 0) {
            return {};
        }

        NativeQpProblem reduced = problem;
        reduced.constraintCount = problem.constraintCount - removed;
        reduced.constraintLowerBounds.clear();
        reduced.constraintUpperBounds.clear();
        reduced.constraintOffsets.clear();
        reduced.initialConstraintValues.clear();
        reduced.constraintNames.clear();
        reduced.constraintMatrix.clear();
        reduced.constraintLowerBounds.reserve(reduced.constraintCount);
        reduced.constraintUpperBounds.reserve(reduced.constraintCount);
        reduced.constraintOffsets.reserve(reduced.constraintCount);
        reduced.initialConstraintValues.reserve(reduced.constraintCount);
        reduced.constraintNames.reserve(reduced.constraintCount);
        reduced.constraintMatrix.reserve(reduced.constraintCount * problem.variableCount);
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            if (!keep[row]) {
                continue;
            }
            reduced.constraintLowerBounds.push_back(problem.constraintLowerBounds[row]);
            reduced.constraintUpperBounds.push_back(problem.constraintUpperBounds[row]);
            reduced.constraintOffsets.push_back(problem.constraintOffsets[row]);
            reduced.initialConstraintValues.push_back(problem.initialConstraintValues[row]);
            reduced.constraintNames.push_back(problem.constraintNames[row]);
            const auto rowStart = row * problem.variableCount;
            reduced.constraintMatrix.insert(reduced.constraintMatrix.end(),
                                            problem.constraintMatrix.begin() +
                                                static_cast<std::ptrdiff_t>(rowStart),
                                            problem.constraintMatrix.begin() +
                                                static_cast<std::ptrdiff_t>(rowStart +
                                                                            problem.variableCount));
        }
        transform.problem = std::move(reduced);
        return {};
    }

    PresolveResult prepareSolverTransform(const NativeQpProblem& problem,
                                          const NativeDenseSolverOptions& options,
                                          SolverTransform& transform)
    {
        // Keep this order deliberate: scaling makes pivot tests comparable,
        // fixed substitution reduces the KKT dimension, and equality reduction
        // sees the bounds after fixed contributions have been applied.
        transform.problem = makeScaledProblem(problem, transform.variableScale);
        auto fixedResult = eliminateFixedVariables(transform, options);
        if (!fixedResult.successful) {
            return fixedResult;
        }
        return reduceRedundantEqualities(transform, options);
    }

    std::vector<double> expandSolution(const SolverTransform& transform,
                                       const std::vector<double>& reducedValues)
    {
        // Reinsert fixed scaled values, then undo x = S*z. The public result is
        // always in the unscaled coordinates supplied by the caller.
        std::vector<double> scaledValues(transform.fixedColumns.size(), 0.0);
        std::size_t reducedColumn = 0;
        for (std::size_t column = 0; column < scaledValues.size(); ++column) {
            if (transform.fixedColumns[column]) {
                scaledValues[column] = transform.fixedValues[column];
            } else {
                scaledValues[column] = reducedValues[reducedColumn++];
            }
        }
        std::vector<double> values(scaledValues.size(), 0.0);
        for (std::size_t column = 0; column < values.size(); ++column) {
            values[column] = scaledValues[column] * transform.variableScale[column];
        }
        return values;
    }

    double nativeMaximumConstraintViolation(const NativeQpProblem& problem,
                                            const std::vector<double>& values)
    {
        // Report residuals in the caller's canonical affine units, including the
        // offset that was intentionally removed from the numerical model.
        double violation = 0.0;
        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            const double value = problem.constraintValue(row, values);
            if (std::isfinite(problem.constraintLowerBounds[row])) {
                violation = (std::max)(violation, problem.constraintLowerBounds[row] - value);
            }
            if (std::isfinite(problem.constraintUpperBounds[row])) {
                violation = (std::max)(violation, value - problem.constraintUpperBounds[row]);
            }
        }
        return (std::max)(0.0, violation);
    }

    double nativeMaximumBoundViolation(const NativeQpProblem& problem,
                                       const std::vector<double>& values)
    {
        double violation = 0.0;
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            if (std::isfinite(problem.variableLowerBounds[column])) {
                violation =
                    (std::max)(violation, problem.variableLowerBounds[column] - values[column]);
            }
            if (std::isfinite(problem.variableUpperBounds[column])) {
                violation =
                    (std::max)(violation, values[column] - problem.variableUpperBounds[column]);
            }
        }
        return (std::max)(0.0, violation);
    }

    double constraintValue(const DenseConstraint& constraint, const std::vector<double>& values)
    {
        return dot(constraint.coefficients, values);
    }

    double maximumConstraintViolation(const DenseModel& model, const std::vector<double>& values)
    {
        double violation = 0.0;
        for (const auto& constraint : model.constraints) {
            const double residual = constraintValue(constraint, values) - constraint.rhs;
            violation = (std::max)(violation, (std::max)(0.0, residual));
            if (constraint.type == ConstraintType::EQUALITY) {
                violation = (std::max)(violation, std::abs(residual));
            }
        }
        for (std::size_t column = 0; column < model.variableCount; ++column) {
            if (isFiniteLower(model.lowerBounds[column])) {
                violation = (std::max)(violation, model.lowerBounds[column] - values[column]);
            }
            if (isFiniteUpper(model.upperBounds[column])) {
                violation = (std::max)(violation, values[column] - model.upperBounds[column]);
            }
        }
        return violation;
    }

    double maximumBoundViolation(const DenseModel& model, const std::vector<double>& values)
    {
        double violation = 0.0;
        for (std::size_t column = 0; column < model.variableCount; ++column) {
            if (isFiniteLower(model.lowerBounds[column])) {
                violation = (std::max)(violation, model.lowerBounds[column] - values[column]);
            }
            if (isFiniteUpper(model.upperBounds[column])) {
                violation = (std::max)(violation, values[column] - model.upperBounds[column]);
            }
        }
        return violation;
    }

    double objectiveValue(const DenseModel& model, const std::vector<double>& values)
    {
        double value = dot(model.linearObjective, values);
        for (std::size_t column = 0; column < model.variableCount; ++column) {
            value += model.quadraticObjective[column] * values[column] * values[column];
        }
        return value;
    }

    std::vector<double> objectiveGradient(const DenseModel& model,
                                          const std::vector<double>& values)
    {
        std::vector<double> gradient(model.variableCount, 0.0);
        for (std::size_t column = 0; column < model.variableCount; ++column) {
            gradient[column] = model.linearObjective[column] +
                2.0 * model.quadraticObjective[column] * values[column];
        }
        return gradient;
    }

    double maximumStationarity(const DenseModel& model,
                               const std::vector<double>& values,
                               const std::vector<int>& workingSet,
                               const std::vector<double>& multipliers)
    {
        auto stationarity = objectiveGradient(model, values);
        for (std::size_t active = 0; active < workingSet.size(); ++active) {
            const auto& coefficients =
                model.constraints[static_cast<std::size_t>(workingSet[active])].coefficients;
            for (std::size_t column = 0; column < model.variableCount; ++column) {
                stationarity[column] += multipliers[active] * coefficients[column];
            }
        }
        double maximum = 0.0;
        for (const auto value : stationarity) {
            maximum = (std::max)(maximum, std::abs(value));
        }
        return maximum;
    }

    double maximumComplementarity(const DenseModel& model,
                                  const std::vector<double>& values,
                                  const std::vector<int>& workingSet,
                                  const std::vector<double>& multipliers)
    {
        double maximum = 0.0;
        for (std::size_t active = 0; active < workingSet.size(); ++active) {
            const auto& constraint =
                model.constraints[static_cast<std::size_t>(workingSet[active])];
            if (constraint.type == ConstraintType::EQUALITY) {
                continue;
            }
            const double slack =
                (std::max)(0.0, constraint.rhs - constraintValue(constraint, values));
            maximum = (std::max)(maximum, std::abs(multipliers[active] * slack));
        }
        return maximum;
    }

    bool isActive(const std::vector<int>& workingSet, int constraintIndex)
    {
        return std::find(workingSet.begin(), workingSet.end(), constraintIndex) != workingSet.end();
    }

    bool isFeasiblePoint(const DenseModel& model,
                         const std::vector<double>& values,
                         double tolerance)
    {
        return maximumConstraintViolation(model, values) <= tolerance;
    }

    /**
     * Minimize the internal diagonal-QP objective from a feasible point.
     *
     * The working set always contains all equalities and the currently active
     * inequality rows. At each iteration the KKT system computes a direction
     * tangent to that set. The direction is taken until the first inactive row
     * blocks it; at a zero direction, a negative inequality multiplier releases
     * that row. This is the complete numerical solve after Phase-I and presolve.
     */
    NativeSolveResult solveActiveSet(const DenseModel& model,
                                     std::vector<double> values,
                                     const NativeDenseSolverOptions& options)
    {
        NativeSolveResult result;
        result.values = std::move(values);
        const double initialViolation = maximumConstraintViolation(model, result.values);
        if (initialViolation > options.feasibilityTolerance) {
            result.status = NativeSolveStatus::INFEASIBLE;
            result.maximumConstraintViolation = initialViolation;
            result.message = "active-set solver received an infeasible starting point (violation=" +
                std::to_string(initialViolation) + ")";
            return result;
        }

        std::vector<int> workingSet;
        workingSet.reserve(model.constraints.size());
        for (std::size_t index = 0; index < model.constraints.size(); ++index) {
            const auto& constraint = model.constraints[index];
            // Start from the equality manifold and any active variable bounds.
            // Other inequalities that happen to be tight at the initial point
            // are added by the blocking-step logic; seeding all of them makes
            // degenerate phase-I starts singular when several slack variables are
            // at zero.
            const double residual = constraintValue(constraint, result.values) - constraint.rhs;
            if ((constraint.type == ConstraintType::EQUALITY) ||
                (constraint.variableBound &&
                 (std::abs(residual) <= options.feasibilityTolerance))) {
                workingSet.push_back(static_cast<int>(index));
            }
        }

        for (std::size_t iteration = 0; iteration < options.maxIterations; ++iteration) {
            result.iterationCount = iteration + 1;
            const auto gradient = objectiveGradient(model, result.values);
            const auto activeCount = workingSet.size();
            const auto kktDimension = model.variableCount + activeCount;
            std::vector<double> kktMatrix(kktDimension * kktDimension, 0.0);
            std::vector<double> kktRhs(kktDimension, 0.0);
            // Solve [H C'; C 0] [direction; multiplier] = [-gradient; 0].
            // H is diagonal for this solver. A small diagonal regularization
            // supplies a deterministic direction for flat LP objective terms;
            // it is not included in the reported objective or stationarity
            // convention.
            for (std::size_t column = 0; column < model.variableCount; ++column) {
                kktMatrix[column * kktDimension + column] =
                    2.0 * model.quadraticObjective[column] + kRegularization;
                kktRhs[column] = -gradient[column];
            }
            for (std::size_t active = 0; active < activeCount; ++active) {
                const auto& coefficients =
                    model.constraints[static_cast<std::size_t>(workingSet[active])].coefficients;
                for (std::size_t column = 0; column < model.variableCount; ++column) {
                    kktMatrix[column * kktDimension + model.variableCount + active] =
                        coefficients[column];
                    kktMatrix[(model.variableCount + active) * kktDimension + column] =
                        coefficients[column];
                }
            }

            const auto kktResult =
                solveLinearSystem(std::move(kktMatrix), std::move(kktRhs), options.pivotTolerance);
            if (kktResult.singular) {
                auto removable = workingSet.end();
                for (auto candidate = workingSet.end(); candidate != workingSet.begin();) {
                    --candidate;
                    const auto& constraint =
                        model.constraints[static_cast<std::size_t>(*candidate)];
                    if ((constraint.type == ConstraintType::INEQUALITY) &&
                        !constraint.variableBound) {
                        removable = candidate;
                        break;
                    }
                }
                if (removable == workingSet.end()) {
                    for (auto candidate = workingSet.end(); candidate != workingSet.begin();) {
                        --candidate;
                        if (model.constraints[static_cast<std::size_t>(*candidate)].type ==
                            ConstraintType::INEQUALITY) {
                            removable = candidate;
                            break;
                        }
                    }
                }
                if (removable != workingSet.end()) {
                    workingSet.erase(removable);
                    continue;
                }
                result.status = NativeSolveStatus::SINGULAR;
                result.message = "active-set KKT system is singular or rank deficient";
                result.activeSetSize = workingSet.size();
                return result;
            }
            if (!kktResult.solved) {
                result.status = NativeSolveStatus::NUMERICAL_FAILURE;
                result.message = "active-set KKT factorization produced non-finite values";
                result.activeSetSize = workingSet.size();
                return result;
            }

            std::vector<double> direction(kktResult.solution.begin(),
                                          kktResult.solution.begin() +
                                              static_cast<std::ptrdiff_t>(model.variableCount));
            const auto stepNorm = std::sqrt(dot(direction, direction));
            const auto valueNorm = std::sqrt(dot(result.values, result.values));
            const double stepTolerance = options.optimalityTolerance * (1.0 + valueNorm);
            std::vector<double> multipliers(kktResult.solution.begin() +
                                                static_cast<std::ptrdiff_t>(model.variableCount),
                                            kktResult.solution.end());
            if (stepNorm <= stepTolerance) {
                if (!isFeasiblePoint(model, result.values, options.feasibilityTolerance)) {
                    result.status = NativeSolveStatus::NUMERICAL_FAILURE;
                    result.maximumConstraintViolation =
                        maximumConstraintViolation(model, result.values);
                    result.message = "active-set converged with an infeasible point";
                    result.activeSetSize = workingSet.size();
                    return result;
                }
                int releaseIndex = -1;
                double releaseMultiplier = 0.0;
                for (std::size_t active = 0; active < activeCount; ++active) {
                    if (model.constraints[static_cast<std::size_t>(workingSet[active])].type ==
                        ConstraintType::EQUALITY) {
                        continue;
                    }
                    const double multiplier = multipliers[active];
                    if (multiplier < -options.optimalityTolerance &&
                        ((releaseIndex < 0) || (multiplier < releaseMultiplier))) {
                        releaseIndex = static_cast<int>(active);
                        releaseMultiplier = multiplier;
                    }
                }
                if (releaseIndex >= 0) {
                    // The current point is stationary for the equality manifold,
                    // but a negative multiplier means this inequality cannot
                    // remain active for the minimization convention used here.
                    workingSet.erase(workingSet.begin() + releaseIndex);
                    continue;
                }

                result.status = NativeSolveStatus::OPTIMAL;
                result.objectiveValue = objectiveValue(model, result.values);
                result.maximumConstraintViolation =
                    maximumConstraintViolation(model, result.values);
                result.maximumBoundViolation = maximumBoundViolation(model, result.values);
                result.maximumStationarity =
                    maximumStationarity(model, result.values, workingSet, multipliers);
                result.maximumComplementarity =
                    maximumComplementarity(model, result.values, workingSet, multipliers);
                result.activeSetSize = workingSet.size();
                result.message = "active-set convergence reached";
                return result;
            }

            // Select the first inactive inequality reached by the search
            // direction. Equal scalar steps are resolved by row order so a
            // degenerate but repeatable active set is produced.
            double stepLength = 1.0;
            int blockingConstraint = -1;
            for (std::size_t index = 0; index < model.constraints.size(); ++index) {
                const auto& constraint = model.constraints[index];
                if ((constraint.type == ConstraintType::EQUALITY) ||
                    isActive(workingSet, static_cast<int>(index))) {
                    continue;
                }
                const double currentValue = constraintValue(constraint, result.values);
                const double directionValue = dot(constraint.coefficients, direction);
                if (directionValue <= options.optimalityTolerance) {
                    continue;
                }
                const double candidateStep = (constraint.rhs - currentValue) / directionValue;
                if (candidateStep < -options.feasibilityTolerance) {
                    result.status = NativeSolveStatus::NUMERICAL_FAILURE;
                    result.message =
                        "active-set step encountered an infeasible blocking constraint";
                    result.activeSetSize = workingSet.size();
                    return result;
                }
                // Feasibility tolerance is in model units, while the step is a
                // scalar.  Using the former as an absolute step tie-breaker can
                // discard the genuinely first blocking row when the KKT system
                // produces a large search direction (for example during Phase-I).
                const double stepTieTolerance =
                    options.feasibilityTolerance * (std::max)(std::abs(stepLength), 1e-12);
                if (candidateStep < stepLength - stepTieTolerance ||
                    ((std::abs(candidateStep - stepLength) <= stepTieTolerance) &&
                     ((blockingConstraint < 0) ||
                      (static_cast<int>(index) < blockingConstraint)))) {
                    stepLength = (std::max)(0.0, candidateStep);
                    blockingConstraint = static_cast<int>(index);
                }
            }

            if (blockingConstraint < 0) {
                double curvature = 0.0;
                for (std::size_t column = 0; column < model.variableCount; ++column) {
                    curvature += 2.0 * model.quadraticObjective[column] * direction[column] *
                        direction[column];
                }
                if ((curvature <= options.optimalityTolerance) &&
                    (dot(gradient, direction) < -options.optimalityTolerance)) {
                    result.status = NativeSolveStatus::UNBOUNDED;
                    result.message = "objective decreases along an unbounded feasible direction";
                    result.activeSetSize = workingSet.size();
                    return result;
                }
            }

            for (std::size_t column = 0; column < model.variableCount; ++column) {
                result.values[column] += stepLength * direction[column];
            }
            if (!finiteVector(result.values)) {
                result.status = NativeSolveStatus::NUMERICAL_FAILURE;
                result.message = "active-set step produced non-finite values";
                result.activeSetSize = workingSet.size();
                return result;
            }
            if (blockingConstraint >= 0) {
                workingSet.push_back(blockingConstraint);
            }
        }

        result.status = NativeSolveStatus::ITERATION_LIMIT;
        result.message = "active-set iteration limit reached";
        result.activeSetSize = workingSet.size();
        return result;
    }

    struct PhaseOneSide {
        std::vector<double> coefficients;
        double rhs = 0.0;
        double violation = 0.0;
    };

    struct PhaseOneModel {
        DenseModel model;
        std::size_t originalVariableCount = 0;
        std::vector<PhaseOneSide> sides;
    };

    /**
     * Construct the L1 feasibility model used to obtain a starting point.
     *
     * Every initially satisfied side is retained as a hard row. Only initially
     * violated sides receive a nonnegative slack s in a row of the form
     * a*y - s <= rhs, and the Phase-I objective minimizes sum(s). This keeps the
     * Phase-I system smaller for large, mostly feasible GridDyn models while
     * preserving all original constraints for the final active-set solve.
     */
    PhaseOneModel makePhaseOneModel(const NativeQpProblem& problem,
                                    const std::vector<double>& initialValues,
                                    double feasibilityTolerance)
    {
        PhaseOneModel phase;
        phase.originalVariableCount = problem.variableCount;
        phase.model.variableCount = problem.variableCount;
        phase.model.lowerBounds = problem.variableLowerBounds;
        phase.model.upperBounds = problem.variableUpperBounds;
        phase.model.linearObjective.assign(problem.variableCount, 0.0);
        phase.model.quadraticObjective.assign(problem.variableCount, 0.0);
        phase.model.constraints.reserve(problem.constraintCount * 2);

        for (std::size_t row = 0; row < problem.constraintCount; ++row) {
            const auto rowStart = row * problem.variableCount;
            std::vector<double> baseCoefficients(problem.variableCount, 0.0);
            std::copy_n(problem.constraintMatrix.begin() + static_cast<std::ptrdiff_t>(rowStart),
                        problem.variableCount,
                        baseCoefficients.begin());
            const double lower = problem.solverConstraintLowerBound(row);
            const double upper = problem.solverConstraintUpperBound(row);
            const double rowValue = dot(baseCoefficients, initialValues);
            if (isEquality(lower, upper, feasibilityTolerance)) {
                const double equalityRhs = 0.5 * (lower + upper);
                const double equalityResidual = rowValue - equalityRhs;
                if (std::abs(equalityResidual) <= feasibilityTolerance) {
                    phase.model.constraints.push_back(
                        {baseCoefficients, equalityRhs, ConstraintType::EQUALITY});
                } else if (equalityResidual < 0.0) {
                    auto coefficients = baseCoefficients;
                    for (auto& coefficient : coefficients) {
                        coefficient = -coefficient;
                    }
                    phase.sides.push_back({std::move(coefficients), -lower, -equalityResidual});
                    // The initially satisfied side remains hard.  Keeping only
                    // the violated side here would let a zero Phase-I slack
                    // settle above the equality instead of enforcing the row.
                    phase.model.constraints.push_back(
                        {baseCoefficients, upper, ConstraintType::INEQUALITY});
                } else {
                    auto coefficients = baseCoefficients;
                    for (auto& coefficient : coefficients) {
                        coefficient = -coefficient;
                    }
                    phase.model.constraints.push_back(
                        {std::move(coefficients), -lower, ConstraintType::INEQUALITY});
                    phase.sides.push_back({baseCoefficients, upper, equalityResidual});
                }
                continue;
            }
            if (isFiniteLower(lower) && (lower - rowValue > feasibilityTolerance)) {
                auto coefficients = baseCoefficients;
                for (auto& coefficient : coefficients) {
                    coefficient = -coefficient;
                }
                phase.sides.push_back({std::move(coefficients), -lower, lower - rowValue});
            } else if (isFiniteLower(lower)) {
                auto lowerCoefficients = baseCoefficients;
                for (auto& coefficient : lowerCoefficients) {
                    coefficient = -coefficient;
                }
                phase.model.constraints.push_back(
                    {std::move(lowerCoefficients), -lower, ConstraintType::INEQUALITY});
            }
            if (isFiniteUpper(upper) && (rowValue - upper > feasibilityTolerance)) {
                phase.sides.push_back({baseCoefficients, upper, rowValue - upper});
            } else if (isFiniteUpper(upper)) {
                phase.model.constraints.push_back(
                    {baseCoefficients, upper, ConstraintType::INEQUALITY});
            }
        }

        const auto slackCount = phase.sides.size();
        phase.model.variableCount += slackCount;
        for (auto& constraint : phase.model.constraints) {
            constraint.coefficients.resize(phase.model.variableCount, 0.0);
        }
        phase.model.lowerBounds.resize(phase.model.variableCount, 0.0);
        phase.model.upperBounds.resize(phase.model.variableCount,
                                       std::numeric_limits<double>::infinity());
        phase.model.linearObjective.resize(phase.model.variableCount, 1.0);
        phase.model.quadraticObjective.resize(phase.model.variableCount, 0.0);
        phase.model.constraints.reserve(slackCount);
        for (std::size_t side = 0; side < slackCount; ++side) {
            auto coefficients = std::vector<double>(phase.model.variableCount, 0.0);
            std::copy(phase.sides[side].coefficients.begin(),
                      phase.sides[side].coefficients.end(),
                      coefficients.begin());
            coefficients[problem.variableCount + side] = -1.0;
            phase.model.constraints.push_back(
                {std::move(coefficients), phase.sides[side].rhs, ConstraintType::INEQUALITY});
            phase.model.linearObjective[problem.variableCount + side] = 1.0;
        }
        addVariableBoundConstraints(phase.model);

        phase.model.lowerBounds.resize(phase.model.variableCount);
        phase.model.upperBounds.resize(phase.model.variableCount);
        return phase;
    }

    std::vector<double> clampToBounds(const NativeQpProblem& problem)
    {
        auto values = problem.initialValues;
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            if (std::isfinite(problem.variableLowerBounds[column])) {
                values[column] = (std::max)(values[column], problem.variableLowerBounds[column]);
            }
            if (std::isfinite(problem.variableUpperBounds[column])) {
                values[column] = (std::min)(values[column], problem.variableUpperBounds[column]);
            }
        }
        return values;
    }

    /** Run Phase-I and return a feasible point in the solver's internal coordinates. */
    NativeSolveResult runPhaseOne(const NativeQpProblem& problem,
                                  const NativeDenseSolverOptions& options)
    {
        auto initialValues = clampToBounds(problem);
        auto phase = makePhaseOneModel(problem, initialValues, options.feasibilityTolerance);
        std::vector<double> phaseInitial(phase.model.variableCount, 0.0);
        std::copy(initialValues.begin(), initialValues.end(), phaseInitial.begin());
        for (std::size_t side = 0; side < phase.sides.size(); ++side) {
            phaseInitial[problem.variableCount + side] = phase.sides[side].violation;
        }

        auto result = solveActiveSet(phase.model, std::move(phaseInitial), options);
        if (result.status != NativeSolveStatus::OPTIMAL) {
            result.status = (result.status == NativeSolveStatus::SINGULAR) ?
                NativeSolveStatus::NUMERICAL_FAILURE :
                result.status;
            result.message = "Phase-I solve failed: " + result.message;
            return result;
        }
        const double phaseFeasibility = maximumConstraintViolation(phase.model, result.values);
        if (phaseFeasibility > options.feasibilityTolerance) {
            result.status = NativeSolveStatus::NUMERICAL_FAILURE;
            result.maximumConstraintViolation = phaseFeasibility;
            result.message = "Phase-I returned an infeasible point";
            result.values.resize(problem.variableCount);
            return result;
        }
        double violation = 0.0;
        for (std::size_t side = 0; side < phase.sides.size(); ++side) {
            if (result.values[problem.variableCount + side] < -options.feasibilityTolerance) {
                result.status = NativeSolveStatus::NUMERICAL_FAILURE;
                result.message = "Phase-I returned a negative violation slack";
                result.values.resize(problem.variableCount);
                return result;
            }
            violation += result.values[problem.variableCount + side];
        }
        if (violation > options.feasibilityTolerance) {
            result.status = NativeSolveStatus::INFEASIBLE;
            result.message = "Phase-I minimum constraint violation is nonzero";
            result.values.resize(problem.variableCount);
            return result;
        }
        result.values.resize(problem.variableCount);
        return result;
    }

    /** Validate the public contract and reject problem classes this backend cannot solve. */
    bool supportedProblem(const NativeQpProblem& problem,
                          const NativeDenseSolverOptions& options,
                          std::string& message,
                          NativeSolveStatus& status)
    {
        std::string validationError;
        if (!problem.validate(&validationError)) {
            status = NativeSolveStatus::NUMERICAL_FAILURE;
            message = "invalid native problem: " + validationError;
            return false;
        }
        if (!problem.valid || (problem.classification == NativeProblemClass::INVALID)) {
            status = NativeSolveStatus::UNSUPPORTED;
            message = "native problem has not been successfully materialized";
            return false;
        }
        if ((problem.classification == NativeProblemClass::UNSUPPORTED_MODE) ||
            (problem.classification == NativeProblemClass::UNSUPPORTED_NONLINEAR) ||
            (problem.classification == NativeProblemClass::UNSUPPORTED_INTEGER)) {
            status = NativeSolveStatus::UNSUPPORTED;
            message = "native problem classification is unsupported";
            return false;
        }
        for (const auto type : problem.variableTypes) {
            if (type != CONTINUOUS_OBJECTIVE_VARIABLE) {
                status = NativeSolveStatus::UNSUPPORTED;
                message = "dense native solver supports continuous variables only";
                return false;
            }
        }
        for (const auto coefficient : problem.quadraticObjective) {
            if (coefficient < -options.optimalityTolerance) {
                status = NativeSolveStatus::NONCONVEX;
                message = "dense native solver requires a convex diagonal quadratic objective";
                return false;
            }
        }
        if ((options.maxIterations == 0) || (options.feasibilityTolerance <= 0.0) ||
            (options.optimalityTolerance <= 0.0) || (options.pivotTolerance <= 0.0)) {
            status = NativeSolveStatus::NUMERICAL_FAILURE;
            message = "dense native solver options are invalid";
            return false;
        }
        return true;
    }

}  // namespace

NativeSolveResult NativeDenseSolver::solve(const NativeQpProblem& problem,
                                           const NativeDenseSolverOptions& options) const
{
    // The solve pipeline is intentionally visible here:
    //   1. validate the solver-neutral contract;
    //   2. scale and presolve into a smaller internal problem;
    //   3. find a feasible point with L1 Phase-I;
    //   4. run the diagonal-QP active set;
    //   5. expand and validate the result in the caller's original units.
    // Keeping this orchestration separate from GridDyn callbacks is what lets
    // a future HiGHS adapter consume the same NativeQpProblem.
    NativeSolveResult result;
    NativeSolveStatus failureStatus = NativeSolveStatus::NUMERICAL_FAILURE;
    if (!supportedProblem(problem, options, result.message, failureStatus)) {
        result.status = failureStatus;
        return result;
    }

    SolverTransform transform;
    const auto presolve = prepareSolverTransform(problem, options, transform);
    if (!presolve.successful) {
        result.status = presolve.status;
        result.message = presolve.message;
        return result;
    }

    const auto model = makeDenseModel(transform.problem);
    const auto phaseResult = runPhaseOne(transform.problem, options);
    if (phaseResult.status != NativeSolveStatus::OPTIMAL) {
        return phaseResult;
    }

    result = solveActiveSet(model, phaseResult.values, options);
    if (result.status == NativeSolveStatus::OPTIMAL) {
        result.values = expandSolution(transform, result.values);
        if (!finiteVector(result.values)) {
            result.status = NativeSolveStatus::NUMERICAL_FAILURE;
            result.message = "native solution expansion produced non-finite values";
            return result;
        }
        result.objectiveValue = problem.objectiveValue(result.values);
        result.maximumConstraintViolation =
            nativeMaximumConstraintViolation(problem, result.values);
        result.maximumBoundViolation = nativeMaximumBoundViolation(problem, result.values);
    }
    return result;
}

}  // namespace griddyn
