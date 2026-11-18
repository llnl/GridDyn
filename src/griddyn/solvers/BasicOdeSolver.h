/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "SolverInterface.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::solvers {
/** @brief fixed-step explicit (forward Euler) solver for differential equations
 *
 * The step size is set with the "deltat" parameter and must be positive. Each call to solve()
 * advances at most max_iterations steps; if the limit is reached before the stop time the
 * solver returns SOLVER_STEP_LIMIT_REACHED with the time it actually reached.
 */
class BasicOdeSolver: public SolverInterface {
  private:
    std::vector<double> state;  //!< state data
    std::vector<double> deriv;  //!< derivative data
    std::vector<double> type;  //!< type data (all differential)
    CoreTime deltaT = 0.005;  //!< the default time step

    /** @brief take one explicit step of size min(deltaT, tStop - solveTime)
    @return FUNCTION_EXECUTION_SUCCESS or the error code from the model evaluation
    */
    int advanceStep(CoreTime tStop);

  public:
    using SolverInterface::set;
    /** @brief default constructor*/
    explicit BasicOdeSolver(const std::string& objName = "basicOde");
    /** alternate constructor to feed to SolverInterface
@param[in] gds  the GridDynSimulation to link to
@param[in] sMode the SolverMode to solve with
*/
    BasicOdeSolver(GridDynSimulation* gds, const SolverMode& sMode);

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
    double* stateData() noexcept override;
    double* derivData() noexcept override;
    double* typeData() noexcept override;

    const double* stateData() const noexcept override;
    const double* derivData() const noexcept override;
    const double* typeData() const noexcept override;
    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    virtual void initialize(CoreTime t0) override;

    virtual double get(std::string_view param) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;

    virtual int
        solve(CoreTime tStop, CoreTime& tReturn, StepMode stepMode = StepMode::NORMAL) override;
};

}  // namespace griddyn::solvers
