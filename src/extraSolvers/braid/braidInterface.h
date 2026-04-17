#pragma once

/*
 * LLNS Copyright Start
 * Copyright (c) 2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#include "../paradae/equations/EqGridDyn.h"
#include "../paradae/equations/Equation.h"
#include "../paradae/math/paradaeArrayData.h"
#include "../paradae/problems/ODEProblem.h"
#include "griddyn/solvers/solverInterface.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::braid {
    using griddyn::paradae::Equation;
    using griddyn::paradae::MapParam;
    using griddyn::paradae::ODEProblem;
    using griddyn::paradae::Real;

    /** @brief class implementing XBraid algorithms for power system DAEs
     */
    class braidSolver: public SolverInterface {
      private:
        std::vector<double> state;  //!< state data/
        std::vector<double> deriv;  //!< temp state data location 1
        std::vector<double> state2;  //!< temp state data location 2
        std::vector<double> type;  //!< type data
        coreTime deltaT = 0.005;  //!< the default time step
        coreTime tStart = 0.0;  //!< the start time
        std::string configFile{
            "braid_params.ini"};  //!< file containing additional braid configuration data
        std::vector<double>
            discontinuities;  //<! vector containing any known event (discontinuity) locations
      public:
        /** @brief default constructor*/
        explicit braidSolver(const std::string& objName = "braid");
        /** alternate constructor to feed to solverInterface
    @param[in] gds  the gridDynSimulation to link to
    @param[in] sMode the solverMode to solve with
    */
        braidSolver(gridDynSimulation* gds, const solverMode& sMode);

        Equation* equation = nullptr;

        virtual std::unique_ptr<SolverInterface> clone(bool fullCopy = false) const override;

        virtual void cloneTo(SolverInterface* si, bool fullCopy = false) const override;
        double* state_data() noexcept override;
        double* deriv_data() noexcept override;
        double* type_data() noexcept override;

        const double* state_data() const noexcept override;
        const double* deriv_data() const noexcept override;
        const double* type_data() const noexcept override;
        virtual void allocate(count_t size, count_t numroots = 0) override;
        virtual void initialize(coreTime t0) override;

        virtual double get(const std::string& param) const override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param, double val) override;

        virtual int calcIC(coreTime t0, coreTime tstep0, ic_modes mode, bool constraints) override;

        virtual int solve(coreTime tStop,
                          coreTime& tReturn,
                          step_mode stepMode = step_mode::normal) override;
        /** execute the braid solve*/
        virtual int RunBraid(ODEProblem* ode, MapParam* param, Real*& timegrid, int Ngridpoints);
    };
}  // namespace griddyn::braid
