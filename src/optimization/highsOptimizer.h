/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "optimizerInterface.h"

namespace griddyn {

/**
 * HiGHS-backed optimizer for the solver-neutral continuous DC LP/QP model.
 *
 * `HighsOptimizer` deliberately reuses `NativeOptimizer::prepareProblemData`
 * and its callback validation. The only backend-specific work is translating
 * the immutable `NativeQpProblem` into HiGHS' column-compressed model and
 * translating the HiGHS result back into the common optimizer state.
 */
class HighsOptimizer final: public NativeOptimizer {
  protected:
    /** @return the backend name used in diagnostics. */
    std::string_view backendName() const override { return "HiGHS"; }

  public:
    /** Construct an unbound HiGHS optimizer. */
    explicit HighsOptimizer(std::string_view optName = "highs");

    /** Construct a HiGHS optimizer bound to a GridDyn optimization model. */
    HighsOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode);

    /**
     * Assemble the shared native problem, solve it with HiGHS, and validate
     * the returned candidate through the common native acceptance path.
     *
     * @param tStop time at which callbacks are evaluated.
     * @param tReturn receives the time associated with the accepted result.
     * @return success only for a validated HiGHS optimal solution.
     */
    int solve(double tStop, double& tReturn) override;

    /** Return common diagnostics plus HiGHS backend identity. */
    double get(std::string_view param) const override;
};

}  // namespace griddyn
