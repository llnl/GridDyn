/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "sundialsInterface.h"
#include <memory>
#include <string>

namespace griddyn {
namespace solvers {
    /** @brief SolverInterface interfacing to the SUNDIALS kinsol solver
     */
    class kinsolInterface: public sundialsInterface {
      public:
        /** @brief constructor*/
        explicit kinsolInterface(const std::string& objName = "kinsol");
        /** @brief constructor loading the SolverInterface structure*
    @param[in] gds  the gridDynSimulation to link with
    @param[in] sMode the solverMode for the solver
    */
        kinsolInterface(gridDynSimulation* gds, const solverMode& sMode);
        /** @brief destructor
         */
        virtual ~kinsolInterface();

        virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

        virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
        virtual void allocate(count_t stateCount, count_t numRoots = 0) override;
        virtual void initialize(coreTime time0) override;
        virtual void sparseReInit(sparse_reinit_modes sparseReinitMode) override;
        int solve(coreTime tStop,
                  coreTime& tReturn,
                  step_mode stepMode = step_mode::normal) override;
        void setConstraints() override;

        void logSolverStats(print_level logLevel, bool iconly = false) const override;
        void logErrorWeights(print_level /*logLevel*/) const override {}
        virtual double get(const std::string& param) const override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param, double val) override;
        // wrapper functions used by kinsol and ida to call the internal functions
        friend int kinsolFunc(N_Vector state, N_Vector resid, void* user_data);
        friend int kinsolJac(N_Vector state,
                             N_Vector resid,
                             SUNMatrix J,
                             void* user_data,
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

}  // namespace solvers
}  // namespace griddyn
