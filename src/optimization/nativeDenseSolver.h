/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "nativeProblem.h"
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace griddyn {

/** Outcomes reported by the small dependency-free dense LP/QP solver. */
enum class NativeSolveStatus {
    OPTIMAL,  //!< A feasible point satisfying the solver's optimality tests was found.
    INFEASIBLE,  //!< Phase-I proved that the constraint violation cannot be removed.
    UNBOUNDED,  //!< The objective decreases along an unbounded feasible direction.
    SINGULAR,  //!< A required KKT system is rank deficient.
    NUMERICAL_FAILURE,  //!< Factorization or an iterate became numerically invalid.
    UNSUPPORTED,  //!< The problem is outside this solver's continuous LP/QP scope.
    NONCONVEX,  //!< A negative diagonal quadratic objective term was detected.
    ITERATION_LIMIT,  //!< The active-set iteration limit was reached.
};

/** Numerical controls for the dependency-free dense active-set solver. */
struct NativeDenseSolverOptions {
    std::size_t maxIterations = 1000;  //!< Maximum active-set iterations per solve phase.
    double feasibilityTolerance = 1e-8;  //!< Allowed row and variable-bound violation.
    double optimalityTolerance = 1e-8;  //!< Allowed step and multiplier optimality residual.
    double pivotTolerance = 1e-12;  //!< Relative threshold for dense Gaussian pivots.
};

/** Result and diagnostics returned by `NativeDenseSolver::solve()`. */
struct NativeSolveResult {
    NativeSolveStatus status = NativeSolveStatus::NUMERICAL_FAILURE;  //!< Solver outcome.
    std::vector<double> values;  //!< Primal values in the input problem's original coordinates.
    double objectiveValue = std::numeric_limits<double>::quiet_NaN();  //!< Objective at `values`.
    double maximumConstraintViolation =
        std::numeric_limits<double>::infinity();  //!< Maximum row violation.
    double maximumBoundViolation =
        std::numeric_limits<double>::infinity();  //!< Maximum variable-bound violation.
    double maximumStationarity =
        std::numeric_limits<double>::infinity();  //!< Active-set stationarity residual.
    double maximumComplementarity =
        std::numeric_limits<double>::infinity();  //!< Active-set complementarity residual.
    std::size_t iterationCount = 0;  //!< Active-set iterations in the final solve phase.
    std::size_t activeSetSize = 0;  //!< Number of rows in the final working set.
    std::string message;  //!< Human-readable outcome or failure explanation.

    /** @return true only when the result is an accepted optimal solution. */
    bool successful() const { return status == NativeSolveStatus::OPTIMAL; }
};

/**
 * Small standard-library-only dense active-set solver for continuous LP/QP
 * problems.
 *
 * The solver accepts the solver-neutral `NativeQpProblem`, performs internal
 * row/variable scaling and presolve, finds a feasible point with an L1
 * violation Phase-I, and then minimizes the diagonal-quadratic objective with
 * an equality/active-inequality working set. It does not call GridDyn
 * callbacks; callback evaluation and model ownership remain in
 * `NativeOptimizer`.
 */
class NativeDenseSolver {
  public:
    /**
     * Solve a continuous linear or convex diagonal-quadratic problem.
     *
     * @param problem validated solver-neutral objective, bounds, and affine rows.
     * @param options numerical tolerances and iteration limits for both Phase-I
     *   and the optimization active-set pass.
     * @return a result whose `values` are expressed in the original problem
     *   coordinates when `OPTIMAL`; failed phases may omit values or retain
     *   an internal diagnostic iterate. Only `OPTIMAL` results are suitable
     *   for write-back.
     */
    static NativeSolveResult solve(const NativeQpProblem& problem,
                                   const NativeDenseSolverOptions& options = {});
};

}  // namespace griddyn
