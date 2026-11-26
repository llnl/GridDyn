/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "BasicOdeSolver.h"

#include "../GridDynSimulation.h"
#include "core/CoreExceptions.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace griddyn::solvers {
namespace {
    bool isStepSizeParam(std::string_view param)
    {
        return (param == "delta") || (param == "deltat") || (param == "step") ||
            (param == "steptime");
    }

    bool allFinite(const std::vector<double>& values)
    {
        return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
    }
}  // namespace

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
        // every variable is a differential variable for this solver
        type.assign(stateCount, 1.0);
        svsize = stateCount;
        flags.reset(INITIALIZED_FLAG);
        flags.set(ALLOCATED_FLAG);
        rootsfound.resize(numRoots);
    }
}

void BasicOdeSolver::initialize(CoreTime t0)
{
    if (!flags[ALLOCATED_FLAG]) {
throw InvalidSolverOperation(SOLVER_INITIAL_SETUP_ERROR,
                                     "cannot initialize solver before allocating state");
    }
    flags.set(INITIALIZED_FLAG);
    solverCallCount = 0;
    solveTime = t0;
}

void BasicOdeSolver::set(std::string_view param, double val)
{
    if (isStepSizeParam(param) && (!(val > 0.0) || !std::isfinite(val))) {
        throw(InvalidParameterValue(param));
    }
    SolverInterface::set(param, val);
}

void BasicOdeSolver::restoreFromCheckpoint(CoreTime time)
{
    if (!isInitialized()) {
throw InvalidSolverOperation(SOLVER_INVALID_STATE_ERROR,
                                     "cannot restore checkpoint before solver initialization");
    }
    solveTime = time;
    solverCallCount = 0;
}

const SolverInterface::Description& BasicOdeSolver::getSolverDescription() const
{
    static constexpr Property<double> numeric[]{
        fixedStepProperty<BasicOdeSolver, &BasicOdeSolver::deltaT>()};
    static const Description description{.numeric = numeric,
                                         .parent = &SolverInterface::getSolverDescription()};
    return description;
}

int BasicOdeSolver::advanceStep(CoreTime tStop)
{
    const CoreTime remaining = tStop - solveTime;
    const bool lastStep = remaining <= deltaT;
    const CoreTime timeStep = lastStep ? remaining : deltaT;

    if (mode.pairedOffsetIndex != kNullLocation) {
        const int ret = m_gds->dynAlgebraicSolve(solveTime, state.data(), deriv.data(), mode);
        if (ret < FUNCTION_EXECUTION_SUCCESS) {
            return ret;
        }
    }
    const int ret = m_gds->derivativeFunction(solveTime, state.data(), deriv.data(), mode);
    if (ret < FUNCTION_EXECUTION_SUCCESS) {
        return ret;
    }
    for (std::size_t kk = 0; kk < state.size(); ++kk) {
        state[kk] = std::fma(timeStep, deriv[kk], state[kk]);
    }
    // land exactly on the stop time so repeated rounding does not leave a sliver step
    solveTime = lastStep ? tStop : solveTime + timeStep;

    if (!allFinite(state)) {
        lastErrorString = "basicode solver produced a non-finite state";
        return FUNCTION_EXECUTION_FAILURE;
    }
    return FUNCTION_EXECUTION_SUCCESS;
}

int BasicOdeSolver::solve(CoreTime tStop, CoreTime& tReturn, StepMode stepMode)
{
    ++solverCallCount;
    if (!(deltaT > 0.0)) {
        lastErrorString = "basicode solver step size must be positive";
        tReturn = solveTime;
        return FUNCTION_EXECUTION_FAILURE;
    }
    if (tStop < solveTime) {
        lastErrorString = "basicode solver cannot integrate backwards in time";
        tReturn = solveTime;
        return FUNCTION_EXECUTION_FAILURE;
    }

    int ret = FUNCTION_EXECUTION_SUCCESS;
    count_t steps = 0;
    while (solveTime < tStop) {
        if (steps >= max_iterations) {
            ret = SOLVER_STEP_LIMIT_REACHED;
            break;
        }
        ret = advanceStep(tStop);
        if (ret < FUNCTION_EXECUTION_SUCCESS) {
            break;
        }
        ++steps;
        // in single step mode only one step is taken per call
        if (stepMode == StepMode::SINGLE_STEP) {
            break;
        }
    }
    tReturn = solveTime;
    return ret;
}

}  // namespace griddyn::solvers
