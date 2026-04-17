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

namespace griddyn {
/** namespace containing the various types of solvers that can be used with GridDyn*/
namespace solvers {
    /** @brief class implementing a Gauss Seidel solver for algebraic variables in a power system
     */
    class basicSolver: public SolverInterface {
      public:
        /** define whether to use the gauss algorithm or the gauss-seidel algorithm*/
        enum class mode_t { gauss, gauss_seidel };

      private:
        std::vector<double> state;  //!< state data/
        std::vector<double> tempState1;  //!< temp state data location 1
        std::vector<double> tempState2;  //!< temp state data location 2
        std::vector<double> type;  //!< type data
        double alpha = 1.1;  //!< convergence gain;
        /** @brief enumeration listing the algorithm types*/

        mode_t algorithm;  //!< the algorithm to use
        count_t iterations = 0;  //!< counter for the number of iterations
      public:
        explicit basicSolver(mode_t alg);
        /** @brief default constructor*/
        explicit basicSolver(const std::string& objName = "basic", mode_t alg = mode_t::gauss);
        /** alternate constructor to feed to SolverInterface
    @param[in] gds  the gridDynSimulation to link to
    @param[in] sMode the solverMode to solve with
    */
        basicSolver(gridDynSimulation* gds, const solverMode& sMode);

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

        virtual double get(const std::string& param) const override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param, double val) override;

        virtual int solve(coreTime tStop,
                          coreTime& tReturn,
                          step_mode stepMode = step_mode::normal) override;
    };

}  // namespace solvers
}  // namespace griddyn
