/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "sundialsInterface.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::solvers {
/** @brief SolverInterface interfacing to the SUNDIALS arkode solver
 */
class arkodeInterface: public sundialsInterface {
  public:
    count_t icCount =
        0;  //!< counter for the number of times the initial condition function was called

  private:
    matrixDataSparse<double> a1;  //!< array structure for holding the Jacobian information

    std::vector<double> tempState;  //!< temporary holding location for a state vector
    double maxStep = -1.0;  //!< the maximum step size to take
    double minStep = -1.0;  //!< the minimum step size to take
    double step = 0.0;  //!< the current step size

  public:
    /** @brief constructor*/
    explicit arkodeInterface(const std::string& objName = "arkode");
    /** @brief alternate constructor
@param[in] gds  the gridDynSimulation object to connect to
@param[in] sMode the solverMode to solve For
*/
    arkodeInterface(gridDynSimulation* gds, const solverMode& sMode);
    /** @brief destructor*/
    virtual ~arkodeInterface();

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    virtual void initialize(coreTime time0) override;
    virtual void setMaxNonZeros(count_t nonZeroCount) override;
    virtual void sparseReInit(sparse_reinit_modes sparseReinitMode) override;
    virtual void getCurrentData() override;
    virtual int
        solve(coreTime tStop, coreTime& tReturn, step_mode stepMode = step_mode::normal) override;
    virtual void getRoots() override;
    virtual void setRootFinding(count_t numRoots) override;

    virtual void logSolverStats(print_level logLevel, bool iconly = false) const override;
    virtual void logErrorWeights(print_level logLevel) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;
    virtual double get(std::string_view param) const override;

    // declare friend some helper functions
    friend int arkodeFunc(realtype time, N_Vector state, N_Vector dstate_dt, void* user_data);
    friend int arkodeJac(realtype time,
                         N_Vector state,
                         N_Vector dstate_dt,
                         SUNMatrix J,
                         void* user_data,
                         N_Vector tmp1,
                         N_Vector tmp2,
                         N_Vector tmp3);
    friend int arkodeRootFunc(realtype time, N_Vector state, realtype* gout, void* user_data);

  protected:
    /** load up masking element if needed*/
    void loadMaskElements();
};

}  // namespace griddyn::solvers
