/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "SundialsInterface.h"
#include <memory>
#include <string>

namespace griddyn::solvers {
/** @brief SolverInterface interfacing to the SUNDIALS kinsol solver
 */
class KinsolInterface: public SundialsInterface {
  public:
    using SundialsInterface::set;
    /** @brief constructor*/
    explicit KinsolInterface(const std::string& objName = "kinsol");
    /** @brief constructor loading the SolverInterface structure*
@param[in] gds  the GridDynSimulation to link with
@param[in] sMode the SolverMode for the solver
*/
    KinsolInterface(GridDynSimulation* gds, const SolverMode& sMode);
    /** @brief destructor
     */
    virtual ~KinsolInterface();

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    virtual void initialize(coreTime time0) override;
    virtual void sparseReInit(SparseReinitMode sparseReinitMode) override;
    int solve(coreTime tStop, coreTime& tReturn, StepMode stepMode = StepMode::NORMAL) override;
    void setConstraints() override;

    void logSolverStats(PrintLevel logLevel, bool iconly = false) const override;
    void logErrorWeights(PrintLevel /*logLevel*/) const override {}
    virtual double get(std::string_view param) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;
    // wrapper functions used by kinsol and ida to call the internal functions
    friend int kinsolFunc(N_Vector state, N_Vector resid, void* userData);
    friend int kinsolJac(N_Vector state,
                         N_Vector resid,
                         SUNMatrix J,
                         void* userData,
                         N_Vector tmp1,
                         N_Vector tmp2);

  private:
#if MEASURE_TIMINGS > 0
    double kinTime = 0;  //!< the total time spent in kinsol
    double residTime = 0;  //!< the total time spent in the residual calls
    double jacTime = 0;  //!< the total time spent in the Jacobian calls
    double jac1Time = 0;  //!< the total time spent in the first Jacobian call
    double kinsol1Time = 0;  //!< the total time spent in kinsol
#endif
};

}  // namespace griddyn::solvers
