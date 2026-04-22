/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "solverInterface.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::solvers {
/** @brief class implementing a Gauss Seidel solver for algebraic variables in a power system
 */
class basicOdeSolver: public SolverInterface {
  private:
    std::vector<double> state;  //!< state data/
    std::vector<double> deriv;  //!< temp state data location 1
    std::vector<double> state2;  //!< temp state data location 2
    std::vector<double> type;  //!< type data
    coreTime deltaT = 0.005;  //!< the default time step
  public:
    /** @brief default constructor*/
    explicit basicOdeSolver(const std::string& objName = "basicOde");
    /** alternate constructor to feed to SolverInterface
@param[in] gds  the gridDynSimulation to link to
@param[in] sMode the solverMode to solve with
*/
    basicOdeSolver(gridDynSimulation* gds, const solverMode& sMode);

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
    double* state_data() noexcept override;
    double* deriv_data() noexcept override;
    double* type_data() noexcept override;

    const double* state_data() const noexcept override;
    const double* deriv_data() const noexcept override;
    const double* type_data() const noexcept override;
    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    virtual void initialize(coreTime t0) override;

    virtual double get(std::string_view param) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;

    virtual int
        solve(coreTime tStop, coreTime& tReturn, step_mode stepMode = step_mode::normal) override;
};

}  // namespace griddyn::solvers

