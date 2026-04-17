/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "basicOdeSolver.h"

#include "../gridDynSimulation.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace griddyn {
namespace solvers {
    basicOdeSolver::basicOdeSolver(const std::string& objName): SolverInterface(objName)
    {
        mode.dynamic = true;
        mode.differential = true;
        mode.algebraic = false;
    }
    basicOdeSolver::basicOdeSolver(gridDynSimulation* gds, const solverMode& sMode):
        SolverInterface(gds, sMode)
    {
    }
    std::unique_ptr<SolverInterface> basicOdeSolver::clone(bool fullCopy) const
    {
        std::unique_ptr<SolverInterface> si = std::make_unique<basicOdeSolver>();
        basicOdeSolver::cloneTo(si.get(), fullCopy);
        return si;
    }

    void basicOdeSolver::cloneTo(SolverInterface* si, bool fullCopy) const
    {
        SolverInterface::cloneTo(si, fullCopy);
        auto bos = dynamic_cast<basicOdeSolver*>(si);
        if (bos == nullptr) {
            return;
        }
        bos->deltaT = deltaT;
    }

    double* basicOdeSolver::state_data() noexcept
    {
        return state.data();
    }
    double* basicOdeSolver::deriv_data() noexcept
    {
        return deriv.data();
    }
    double* basicOdeSolver::type_data() noexcept
    {
        return type.data();
    }
    const double* basicOdeSolver::state_data() const noexcept
    {
        return state.data();
    }
    const double* basicOdeSolver::deriv_data() const noexcept
    {
        return deriv.data();
    }
    const double* basicOdeSolver::type_data() const noexcept
    {
        return type.data();
    }
    void basicOdeSolver::allocate(count_t stateCount, count_t numRoots)
    {
        // load the vectors
        if (stateCount != svsize) {
            state.resize(stateCount);
            deriv.resize(stateCount);
            state2.resize(stateCount);
            svsize = stateCount;
            flags.reset(initialized_flag);
            flags.set(allocated_flag);
            rootsfound.resize(numRoots);
        }
    }

    void basicOdeSolver::initialize(coreTime t0)
    {
        if (!flags[allocated_flag]) {
            throw(InvalidSolverOperation(-2));
        }
        flags.set(initialized_flag);
        solverCallCount = 0;
        solveTime = t0;
    }

    double basicOdeSolver::get(const std::string& param) const
    {
        if (param == "deltat") {
            return deltaT;
        }
        return SolverInterface::get(param);
    }
    void basicOdeSolver::set(const std::string& param, const std::string& val)
    {
        if (param[0] == '#') {
        } else {
            SolverInterface::set(param, val);
        }
    }
    void basicOdeSolver::set(const std::string& param, double val)
    {
        if ((param == "delta") || (param == "deltat") || (param == "step") ||
            (param == "steptime")) {
            deltaT = val;
        } else {
            SolverInterface::set(param, val);
        }
    }

    int basicOdeSolver::solve(coreTime tStop, coreTime& tReturn, step_mode stepMode)
    {
        if (solveTime == tStop) {
            tReturn = tStop;
            return FUNCTION_EXECUTION_SUCCESS;
        }
        coreTime Tstep = (std::min)(deltaT, tStop - solveTime);
        if (mode.pairedOffsetIndex != kNullLocation) {
            int ret = m_gds->dynAlgebraicSolve(solveTime, state.data(), deriv.data(), mode);
            if (ret < FUNCTION_EXECUTION_SUCCESS) {
                return ret;
            }
        }
        m_gds->derivativeFunction(solveTime, state.data(), deriv.data(), mode);
        std::transform(state.begin(),
                       state.end(),
                       deriv.begin(),
                       state.begin(),
                       [Tstep](double a, double b) { return fma(Tstep, b, a); });
        solveTime += Tstep;

        count_t iterations = 0;
        // if we are in single step mode don't go into the loop
        if (stepMode == step_mode::normal) {
            while (solveTime < tStop && iterations < max_iterations) {
                Tstep = (std::min)(deltaT, tStop - solveTime);
                if (mode.pairedOffsetIndex != kNullLocation) {
                    int ret = m_gds->dynAlgebraicSolve(solveTime, state.data(), deriv.data(), mode);
                    if (ret < FUNCTION_EXECUTION_SUCCESS) {
                        return ret;
                    }
                }
                m_gds->derivativeFunction(solveTime, state.data(), deriv.data(), mode);
                std::transform(state.begin(),
                               state.end(),
                               deriv.begin(),
                               state.begin(),
                               [Tstep](double a, double b) { return fma(Tstep, b, a); });
                solveTime += Tstep;
                ++iterations;
            }
        }
        tReturn = solveTime;
        return FUNCTION_EXECUTION_SUCCESS;
    }

}  // namespace solvers
}  // namespace griddyn
