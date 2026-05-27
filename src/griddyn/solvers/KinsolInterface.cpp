/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "KinsolInterface.h"

#include "../GridDynSimulation.h"
#include "../simulation/GridDynSimulationFileOps.h"
#include "SundialsMatrixData.h"
// #include "matrixDataBoost.h"
#include "core/CoreExceptions.h"
#include "utilities/matrixCreation.h"
#include <kinsol/kinsol.h>
#include <kinsol/kinsol_ls.h>
#include <sunlinsol/sunlinsol_dense.h>

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#if MEASURE_TIMINGS > 0
#    include <chrono>
#endif

#include <cassert>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <print>
#include <string>

namespace griddyn::solvers {
int kinsolFunc(N_Vector state, N_Vector resid, void* user_data);
int kinsolJac(N_Vector state,
              N_Vector resid,
              SUNMatrix J,
              void* user_data,
              N_Vector tmp1,
              N_Vector tmp2);

// int kinsolAlgFunc (N_Vector u, N_Vector f, void *user_data);
// int kinsolAlgJacDense (long int N, N_Vector u, N_Vector f, DlsMat J, void *user_data,
// N_Vector tmp1, N_Vector tmp2);

kinsolInterface::kinsolInterface(const std::string& objName): SundialsInterface(objName)
{
    tolerance = 1e-8;
    mode.algebraic = true;
    mode.differential = false;
    max_iterations = 50;
}

kinsolInterface::kinsolInterface(GridDynSimulation* gds, const solverMode& sMode):
    SundialsInterface(gds, sMode)
{
    tolerance = 1e-8;
    mode.algebraic = true;
    mode.differential = false;
    max_iterations = 50;
}

kinsolInterface::~kinsolInterface()
{
    // clear the memory,  the SundialsInterface destructor will clear the rest
    if (flags[initialized_flag]) {
        KINFree(&solverMem);
    }
}

std::unique_ptr<SolverInterface> kinsolInterface::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<kinsolInterface>();
    kinsolInterface::cloneTo(si.get(), fullCopy);
    return si;
}

void kinsolInterface::cloneTo(SolverInterface* si, bool fullCopy) const
{
    SundialsInterface::cloneTo(si, fullCopy);
    auto* ai = dynamic_cast<kinsolInterface*>(si);
    if (ai == nullptr) {
        return;
    }
}

void kinsolInterface::allocate(count_t stateCount, count_t /*numRoots*/)
{
    // load the vectors
    if (stateCount == svsize) {
        return;
    }

    if (solverMem != nullptr) {
        KINFree(&(solverMem));
    }
    solverMem = KINCreate(sunctx);
    checkFlag(solverMem, "KINCreate", 0);

    SundialsInterface::allocate(stateCount, 0);
}

// output solver stats
void kinsolInterface::logSolverStats(PrintLevel logLevel, bool /*iconly*/) const
{
    if (!flags[initialized_flag]) {
        return;
    }
    long int nni{0};
    long int nfe{0};
    long int nje{0};
    long int nfeD{0};
    int flag = KINGetNumNonlinSolvIters(solverMem, &nni);
    checkFlag(&flag, "KINGetNumNonlinSolvIters", 1);
    flag = KINGetNumFuncEvals(solverMem, &nfe);
    checkFlag(&flag, "KINGetNumFuncEvals", 1);

    flag = KINGetNumJacEvals(solverMem, &nje);
    checkFlag(&flag, "KINGetNumJacEvals", 1);
    flag = KINGetNumLinFuncEvals(solverMem, &nfeD);
    checkFlag(&flag, "KINGetNumLinFuncEvals", 1);

    auto logstr = std::format("Kinsoln Statistics: \n"
                              "Number of nonlinear iterations    = {}\n"
                              "Number of function evaluations    = {}\n"
                              "Number of Jacobian evaluations    = {}\n",
                              nni,
                              nfe,
                              nje);
    if (nfeD > 0) {
        logstr += std::format("Number of Jacobian function calls = {}\n", nfeD);
    }

    if (m_gds != nullptr) {
        logging::logTo(m_gds, m_gds, logLevel, logstr);
    } else {
        std::print("\n{}", logstr);
    }
}

void kinsolInterface::initialize(coreTime /*t0*/)
{
    if (!flags[allocated_flag]) {
        throw(InvalidSolverOperation());
    }
    if (flags[directLogging_flag]) {
        if (!(solverLogFile.empty())) {
            if (m_sundialsInfoFile == nullptr) {
                m_sundialsInfoFile = fopen(solverLogFile.c_str(), "w");
            }
        } else {
            if (m_sundialsInfoFile == nullptr) {
                solverLogFile = "kinsol.out";
                m_sundialsInfoFile = fopen("kinsol.out", "w");
            }
        }
    }

    int retval = KINSetUserData(solverMem, this);
    checkFlag(&retval, "KINSetUserData", 1);

    // retval = KINSetFuncNormTol (solverMem, 1.e-9);
    retval = KINSetFuncNormTol(solverMem, tolerance);
    checkFlag(&retval, "KINSetFuncNormTol", 1);

    // retval = KINSetScaledStepTol (solverMem, 1.e-9);
    retval = KINSetScaledStepTol(solverMem, tolerance / 100);
    checkFlag(&retval, "KINSetScaledStepTol", 1);

    retval = KINSetNoInitSetup(solverMem, SUNTRUE);
    checkFlag(&retval, "KINSetNoInitSetup", 1);

    retval = KINInit(solverMem, kinsolFunc, state);

    checkFlag(&retval, "KINInit", 1);

#ifdef GRIDDYN_ENABLE_KLU
    if (flags[dense_flag]) {
        J = SUNDenseMatrix(svsize, svsize, sunctx);
        checkFlag(J, "SUNDenseMatrix", 0);
        /* Create KLU solver object */
        LS = SUNLinSol_Dense(state, J, sunctx);
        checkFlag(LS, "SUNLinSol_Dense", 0);
    } else {
        /* Create sparse SUNMatrix */
        J = SUNSparseMatrix(svsize, svsize, maxNNZ, CSR_MAT, sunctx);
        checkFlag(J, "SUNSparseMatrix", 0);

        /* Create KLU solver object */
        LS = SUNLinSol_KLU(state, J, sunctx);
        checkFlag(LS, "SUNLinSol_KLU", 0);

        retval = SUNLinSol_KLUSetOrdering(LS, 0);
        checkFlag(&retval, "SUNLinSol_KLUSetOrdering", 1);
    }
#else
    J = SUNDenseMatrix(svsize, svsize, sunctx);
    checkFlag(J, "SUNSparseMatrix", 0);
    /* Create KLU solver object */
    LS = SUNLinSol_Dense(state, J, sunctx);
    checkFlag(LS, "SUNLinSol_Dense", 0);
#endif

    retval = KINSetLinearSolver(solverMem, LS, J);

    checkFlag(&retval, "KINSetLinearSolver", 1);

    retval = KINSetJacFn(solverMem, kinsolJac);
    checkFlag(&retval, "KINSetJacFn", 1);

    retval = KINSetMaxSetupCalls(solverMem, 1);  // exact Newton
    checkFlag(&retval, "KINSetMaxSetupCalls", 1);

    retval = KINSetMaxSubSetupCalls(solverMem, 2);  // residual calls
    checkFlag(&retval, "KINSetMaxSubSetupCalls", 1);

    retval = KINSetNumMaxIters(solverMem, max_iterations);  // residual calls
    checkFlag(&retval, "KINSetNumMaxIters", 1);

    flags.set(initialized_flag);
}

void kinsolInterface::sparseReInit(SparseReinitMode sparseReinitMode)
{
    kluReInit(sparseReinitMode);
}

void kinsolInterface::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        SundialsInterface::set(param, val);
    }
}

void kinsolInterface::set(std::string_view param, double val)
{
    if (param.empty()) {
    } else if (param == "maxiterations") {
        max_iterations = static_cast<count_t>(val);
        int retval = KINSetNumMaxIters(solverMem, max_iterations);
        checkFlag(&retval, "KINSetNumMaxIters", 1);
    } else {
        SundialsInterface::set(param, val);
    }
}

double kinsolInterface::get(std::string_view param) const
{
    long int val = -1;
    if (param == "jac calls") {
        KINGetNumJacEvals(solverMem, &val);
    } else if (param == "nliterations") {
        KINGetNumNonlinSolvIters(solverMem, &val);
#if MEASURE_TIMINGS > 0
    } else if (param == "kintime") {
        return kinTime;
    } else if (param == "residtime") {
        return residTime;
    } else if (param == "jactime") {
        return jacTime;
    } else if (param == "jac1time") {
        return jac1Time;
    } else if (param == "kin1time") {
        return kinsol1Time;
#endif
    } else {
        return SundialsInterface::get(param);
    }
    return static_cast<double>(val);
}

#define SHOW_MISSING_ELEMENTS 0

// #define KIN_NONE       0
// #define KIN_LINESEARCH 1
// #define KIN_PICARD     2
// #define KIN_FP         3
int kinsolInterface::solve(coreTime tStop, coreTime& tReturn, StepMode /*mode*/)
{
    // check if the multiple data sets are in use and if we should toggle the data to use
    solveTime = tStop;
#if MEASURE_TIMINGS > 0
    auto start_t = std::chrono::high_resolution_clock::now();

    int retval = KINSol(solverMem, state, KIN_NONE, scale, scale);
    auto stop_t = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_t = stop_t - start_t;
    kinTime += elapsed_t.count();
    std::println("total solve time {}, {:5.3f}% in resid {:5.3f}%  in Jacobian",
                 kinTime,
                 residTime / kinTime * 100.0,
                 jacTime / kinTime * 100);
#else
    int retval = KINSol(solverMem, state, KIN_NONE, scale, scale);
#endif

#if SHOW_MISSING_ELEMENTS > 0
    if (retval == -11) {
        auto mvec = findMissing(&a1);
        if (mvec.size() > 0) {
            stringVec sL;
            m_gds->getStateName(sL, mode);
            for (auto mv : mvec) {
                std::format("state[{}]{} following state {} is singular\n",
                            mv,
                            sL[mv].c_str(),
                            sL[mv - 1].c_str());
            }
        } else {
            // stringVec sL;
            // m_gds->getStateName(sL, mode);
            // auto mrvec=findRank(&a1);
        }
    } else if (retval < 0) {
        auto mvec = findMissing(&a1);
    }
#endif
    tReturn = (retval >= 0) ? solveTime : m_gds->getSimulationTime();
    ++solverCallCount;
    if (retval == KIN_REPTD_SYSFUNC_ERR) {
        retval = SOLVER_INVALID_STATE_ERROR;
    }
    return retval;
}

void kinsolInterface::setConstraints()
{
    if (m_gds->hasConstraints()) {
        N_VConst(ZERO, consData);
        m_gds->getConstraints(NVECTOR_DATA(use_omp, consData), mode);
        KINSetConstraints(solverMem, consData);
    }
}

// function not in the class
// KINSOL C functions

int kinsolFunc(N_Vector state, N_Vector resid, void* user_data)
{
    auto* sd = static_cast<kinsolInterface*>(user_data);
    sd->funcCallCount++;
#if MEASURE_TIMINGS > 0
    auto start_t = std::chrono::high_resolution_clock::now();

    int ret = sd->m_gds->residualFunction(sd->solveTime,
                                          NVECTOR_DATA(sd->use_omp, state),
                                          nullptr,
                                          NVECTOR_DATA(sd->use_omp, resid),
                                          sd->mode);
    auto stop_t = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_t = stop_t - start_t;
    sd->residTime += elapsed_t.count();
#else

    int ret = sd->m_gds->residualFunction(sd->solveTime,
                                          NVECTOR_DATA(sd->use_omp, state),
                                          nullptr,
                                          NVECTOR_DATA(sd->use_omp, resid),
                                          sd->mode);
#endif
    if (sd->flags[print_residuals]) {
        long int val = 0;
        KINGetNumNonlinSolvIters(sd->solverMem, &val);
        double* residuals = NVECTOR_DATA(sd->use_omp, resid);
        double* values = NVECTOR_DATA(sd->use_omp, state);
        std::println("Residual for {} at time ={} iteration {}",
                     sd->getName(),
                     static_cast<double>(sd->solveTime),
                     val);
        for (int kk = 0; kk < static_cast<int>(sd->svsize); ++kk) {
            std::println("value[{}] = {}, resid[{}]={}", kk, values[kk], kk, residuals[kk]);
        }
        std::println("---------------------------------");
    }
    if (sd->flags[fileCapture_flag]) {
        if (!sd->stateFile.empty()) {
            writeVector(sd->solveTime,
                        STATE_INFORMATION,
                        sd->funcCallCount,
                        sd->mode.offsetIndex,
                        sd->svsize,
                        NVECTOR_DATA(sd->use_omp, state),
                        sd->stateFile,
                        (sd->funcCallCount != 1));
            writeVector(sd->solveTime,
                        RESIDUAL_INFORMATION,
                        sd->funcCallCount,
                        sd->mode.offsetIndex,
                        sd->svsize,
                        NVECTOR_DATA(sd->use_omp, resid),
                        sd->stateFile);
        }
    }
    return ret;
}

int kinsolJac(N_Vector state,
              N_Vector /*f*/,
              SUNMatrix J,
              void* user_data,
              N_Vector tmp1,
              N_Vector tmp2)
{
    auto* sd = static_cast<kinsolInterface*>(user_data);
    return sundialsJac(sd->solveTime, 0, state, nullptr, J, user_data, tmp1, tmp2);
}

}  // namespace griddyn::solvers

