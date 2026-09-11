/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "optimizerInterface.h"

namespace griddyn {

/**
 * Policy controlling when the HiGHS backend should apply problem scaling.
 *
 * The policy is configuration scaffolding for the solver-boundary scaling
 * implementation.  `NO_SCALING` and `SCALING` are explicit requests, while
 * `AUTO` selects scaling when the configured variable-count threshold is
 * reached.  The matrix transformation is intentionally kept separate from
 * this policy selection.
 */
enum class HighsScalingMode {
    NO_SCALING = 0,  //!< Never apply solver-boundary scaling.
    SCALING = 1,  //!< Always apply solver-boundary scaling.
    AUTO = 2,  //!< Apply scaling automatically for sufficiently large models.
};

/** Default variable-count threshold for `HighsScalingMode::AUTO`. */
inline constexpr count_t kDefaultHighsScalingVariableThreshold = 6000;

/**
 * HiGHS-backed optimizer for the solver-neutral continuous DC LP/QP model.
 *
 * `HighsOptimizer` deliberately reuses `NativeOptimizer::prepareProblemData`
 * and its callback validation. The only backend-specific work is translating
 * the immutable `NativeQpProblem` into HiGHS' column-compressed model and
 * translating the HiGHS result back into the common optimizer state.
 */
class HighsOptimizer final: public NativeOptimizer {
  private:
    HighsScalingMode mScalingMode = HighsScalingMode::AUTO;
    count_t mScalingVariableThreshold = kDefaultHighsScalingVariableThreshold;
    bool mScalingApplied = false;

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

    /**
     * Set HiGHS-specific string options.
     *
     * The `scaling` and `scaling_mode` parameters accept `no_scaling`,
     * `scaling`, or `auto`.  The policy is stored now; applying the selected
     * transformation is a separate solver-boundary implementation step.
     */
    void set(std::string_view param, std::string_view val) override;

    /**
     * Set HiGHS-specific numeric options.
     *
     * `scaling_threshold` and `scaling_variable_threshold` set the variable
     * count at which `auto` selects scaling.  Numeric mode values use the
     * `HighsScalingMode` enumeration values 0, 1, and 2.
     */
    void set(std::string_view param, double val) override;

    /** Return common diagnostics plus HiGHS backend identity and scaling state. */
    double get(std::string_view param) const override;

    /** Return the configured scaling policy. */
    HighsScalingMode scalingMode() const noexcept { return mScalingMode; }

    /** Return the variable-count threshold used by automatic scaling. */
    count_t scalingVariableThreshold() const noexcept { return mScalingVariableThreshold; }

    /**
     * Return whether the configured policy selects scaling for the current
     * allocated problem.  This is a policy query only until the scaling
     * transformation is implemented.
     */
    bool scalingRequestedForProblem() const noexcept;
};

}  // namespace griddyn
