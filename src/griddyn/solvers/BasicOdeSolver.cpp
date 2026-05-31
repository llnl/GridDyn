/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "BasicOdeSolver.h"

#include "../GridDynSimulation.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace griddyn::solvers {
BasicOdeSolver::BasicOdeSolver(const std::string& objName): SolverInterface(objName)
{
    mode.dynamic = true;
    mode.differential = true;
    mode.algebraic = false;
}
BasicOdeSolver::BasicOdeSolver(GridDynSimulation* gds, const SolverMode& sMode):
    SolverInterface(gds, sMode)
{
}
std::unique_ptr<SolverInterface> BasicOdeSolver::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<BasicOdeSolver>();
    BasicOdeSolver::cloneTo(si.get(), fullCopy);
    return si;
}

void BasicOdeSolver::cloneTo(SolverInterface* si, bool fullCopy) const
{
    SolverInterface::cloneTo(si, fullCopy);
    auto bos = dynamic_cast<BasicOdeSolver*>(si);
    if (bos == nullptr) {
        return;
    }
    bos->deltaT = deltaT;
}

double* BasicOdeSolver::stateData() noexcept
{
    return state.data();
}
double* BasicOdeSolver::derivData() noexcept
{
    return deriv.data();
}
double* BasicOdeSolver::typeData() noexcept
{
    return type.data();
}
const double* BasicOdeSolver::stateData() const noexcept
{
    return state.data();
}
const double* BasicOdeSolver::derivData() const noexcept
{
    return deriv.data();
}
const double* BasicOdeSolver::typeData() const noexcept
{
    return type.data();
}
void BasicOdeSolver::allocate(count_t stateCount, count_t numRoots)
{
    // load the vectors
    if (stateCount != svsize) {
        state.resize(stateCount);
        deriv.resize(stateCount);
        state2.resize(stateCount);
        svsize = stateCount;
        flags.reset(INITIALIZED_FLAG);
        flags.set(ALLOCATED_FLAG);
        rootsfound.resize(numRoots);
    }
}

void BasicOdeSolver::initialize(CoreTime t0)
{
    if (!flags[ALLOCATED_FLAG]) {
        throw(InvalidSolverOperation(-2));
    }
    flags.set(INITIALIZED_FLAG);
    solverCallCount = 0;
    solveTime = t0;
}

double BasicOdeSolver::get(std::string_view param) const
{
    if (param == "deltat") {
        return deltaT;
    }
    return SolverInterface::get(param);
}
void BasicOdeSolver::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        SolverInterface::set(param, val);
    }
}
void BasicOdeSolver::set(std::string_view param, double val)
{
    if ((param == "delta") || (param == "deltat") || (param == "step") || (param == "steptime")) {
        deltaT = val;
    } else {
        SolverInterface::set(param, val);
    }
}

int BasicOdeSolver::solve(CoreTime tStop, CoreTime& tReturn, StepMode stepMode)
{
    if (solveTime == tStop) {
        tReturn = tStop;
        return FUNCTION_EXECUTION_SUCCESS;
    }
    CoreTime Tstep = (std::min)(deltaT, tStop - solveTime);
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
    if (stepMode == StepMode::NORMAL) {
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

}  // namespace griddyn::solvers
