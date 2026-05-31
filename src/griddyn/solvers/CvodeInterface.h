/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "SundialsInterface.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::solvers {
/** @brief SolverInterface interfacing to the SUNDIALS cvode solver
 */
class CvodeInterface: public SundialsInterface {
  public:
    using SundialsInterface::set;
    count_t icCount = 0;  //!< total number of initial condition calls

  private:
    MatrixDataSparse<double> a1;  //!< array structure for holding the Jacobian information
    std::vector<double> tempState;  //!< temporary holding location for a state vector
    double maxStep = -1.0;  //!< the maximum step size permitted
    double minStep = -1.0;  //!< the minimum step size permitted
    double step = 0.0;  //!< the requested step size

  public:
    /** @brief constructor*/
    explicit CvodeInterface(const std::string& objName = "cvode");
    /** @brief alternate constructor
@param[in] gds  the GridDynSimulation object to connect to
@param[in] sMode the SolverMode to solve For
*/
    CvodeInterface(GridDynSimulation* gds, const SolverMode& sMode);
    /** @brief destructor*/
    virtual ~CvodeInterface();

    virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

    virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
    virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
    virtual void initialize(CoreTime time0) override;
    virtual void setMaxNonZeros(count_t nonZeroCount) override;
    virtual void sparseReInit(SparseReinitMode reInitMode) override;
    virtual void getCurrentData() override;
    virtual int
        solve(CoreTime tStop, CoreTime& tReturn, StepMode stepMode = StepMode::NORMAL) override;
    virtual void getRoots() override;
    virtual void setRootFinding(count_t numRoots) override;

    virtual void logSolverStats(PrintLevel logLevel, bool iconly = false) const override;
    virtual void logErrorWeights(PrintLevel logLevel) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;
    virtual double get(std::string_view param) const override;

    // declare friend some helper functions
    friend int cvodeFunc(sunrealtype time, N_Vector state, N_Vector dstateDt, void* userData);

    friend int cvodeJac(sunrealtype time,
                        N_Vector state,
                        N_Vector dstateDt,
                        SUNMatrix J,
                        void* userData,
                        N_Vector tmp1,
                        N_Vector tmp2,
                        N_Vector tmp3);

    friend int cvodeRootFunc(sunrealtype time, N_Vector state, sunrealtype* gout, void* userData);

  protected:
    void loadMaskElements();
};

}  // namespace griddyn::solvers
