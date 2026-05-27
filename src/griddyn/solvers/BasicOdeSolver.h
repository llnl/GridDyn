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
/** @brief class implementing a Gauss Seidel solver for algebraic variables in a power system
 */
class BasicOdeSolver: public SolverInterface {
  private:
    std::vector<double> state;  //!< state data/
    std::vector<double> deriv;  //!< temp state data location 1
    std::vector<double> state2;  //!< temp state data location 2
    std::vector<double> type;  //!< type data
    coreTime deltaT = 0.005;  //!< the default time step

  public:
    using SolverInterface::set;
    /** @brief default constructor*/
    explicit BasicOdeSolver(const std::string& objName = "basicOde");
    /** alternate constructor to feed to SolverInterface
@param[in] gds  the GridDynSimulation to link to
@param[in] sMode the solverMode to solve with
*/
    BasicOdeSolver(GridDynSimulation* gds, const solverMode& sMode);

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
    double* stateData() noexcept override;
    double* derivData() noexcept override;
    double* typeData() noexcept override;

    const double* stateData() const noexcept override;
    const double* derivData() const noexcept override;
    const double* typeData() const noexcept override;
    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    virtual void initialize(coreTime t0) override;

    virtual double get(std::string_view param) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;

    virtual int
        solve(coreTime tStop, coreTime& tReturn, StepMode stepMode = StepMode::NORMAL) override;
};

}  // namespace griddyn::solvers
