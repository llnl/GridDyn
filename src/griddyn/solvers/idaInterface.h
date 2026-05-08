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
/** @brief SolverInterface interfacing to the SUNDIALS IDA solver
 */
class idaInterface: public sundialsInterface {
  public:
    using sundialsInterface::set;
    count_t icCount = 0;  //!< the number of times the initical condition function was called

  private:
    matrixDataSparse<double> a1;  //!< array structure for holding the Jacobian information

    std::vector<double> tempState;  //!< temporary holding location for a state vector
  public:
    /** @brief constructor*/
    explicit idaInterface(const std::string& objName = "ida");
    /** @brief alternate constructor
@param[in] gds  the gridDynSimulation object to connect to
@param[in] sMode the solverMode to solve For
*/
    idaInterface(gridDynSimulation* gds, const solverMode& sMode);
    /** @brief destructor*/
    ~idaInterface();

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;

    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    void setMaxNonZeros(count_t nonZeros) override;
    virtual void initialize(coreTime t0) override;
    virtual void sparseReInit(sparse_reinit_modes sparseReInitMode) override;
    int calcIC(coreTime t0, coreTime tstep0, ic_modes initCondMode, bool constraints) override;
    virtual void getCurrentData() override;
    int solve(coreTime tStop, coreTime& tReturn, step_mode stepMode = step_mode::normal) override;
    virtual void getRoots() override;
    virtual void setRootFinding(count_t numRoots) override;

    void logSolverStats(print_level logLevel, bool iconly = false) const override;
    void logErrorWeights(print_level logLevel) const override;
    virtual void set(std::string_view param, double val) override;
    virtual double get(std::string_view param) const override;

    void setConstraints() override;

    // declare friend some helper functions
    friend int idaFunc(sunrealtype time,
                       N_Vector state,
                       N_Vector dstate_dt,
                       N_Vector resid,
                       void* user_data);

    friend int idaJac(sunrealtype time,
                      sunrealtype cj,
                      N_Vector state,
                      N_Vector dstate_dt,
                      N_Vector resid,
                      SUNMatrix J,
                      void* user_data,
                      N_Vector tmp1,
                      N_Vector tmp2,
                      N_Vector tmp3);

    friend int idaRootFunc(sunrealtype time,
                           N_Vector state,
                           N_Vector dstate_dt,
                           sunrealtype* gout,
                           void* user_data);

  protected:
    void loadMaskElements();
};

}  // namespace griddyn::solvers
