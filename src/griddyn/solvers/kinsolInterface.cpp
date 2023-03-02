/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "kinsolInterface.h"

#include "../gridDynSimulation.h"
#include "../simulation/gridDynSimulationFileOps.h"
#include "sundialsMatrixData.h"
//#include "matrixDataBoost.h"
#include "core/coreExceptions.h"
#include "fmtlib/include/fmt/format.h"
#include "utilities/matrixCreation.h"
#include <kinsol/kinsol.h>
#include <kinsol/kinsol_direct.h>
#include <sunlinsol/sunlinsol_dense.h>

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#if MEASURE_TIMINGS > 0
#    include <chrono>
#endif

#include <cassert>
#include <map>

namespace griddyn {
namespace solvers {
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

    kinsolInterface::kinsolInterface(const std::string& objName): sundialsInterface(objName)
    {
        tolerance = 1e-8;
        mode.algebraic = true;
        mode.differential = false;
        max_iterations = 50;
    }

    kinsolInterface::kinsolInterface(gridDynSimulation* gds, const solverMode& sMode):
        sundialsInterface(gds, sMode)
    {
        tolerance = 1e-8;
        mode.algebraic = true;
        mode.differential = false;
        max_iterations = 50;
    }

    kinsolInterface::~kinsolInterface()
    {
        // clear the memory,  the sundialsInterface destructor will clear the rest
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
        sundialsInterface::cloneTo(si, fullCopy);
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
        solverMem = KINCreate();
        check_flag(solverMem, "KINCreate", 0);

        sundialsInterface::allocate(stateCount, 0);
    }

    // output solver stats
    void kinsolInterface::logSolverStats(print_level logLevel, bool /*iconly*/) const
    {
        if (!flags[initialized_flag]) {
            return;
        }
        long int nni{0};
        long int nfe{0};
        long int nje{0};
        long int nfeD{0};
        int flag = KINGetNumNonlinSolvIters(solverMem, &nni);
        check_flag(&flag, "KINGetNumNonlinSolvIters", 1);
        flag = KINGetNumFuncEvals(solverMem, &nfe);
        check_flag(&flag, "KINGetNumFuncEvals", 1);

        flag = KINDlsGetNumJacEvals(solverMem, &nje);
        check_flag(&flag, "KINDlsGetNumJacEvals", 1);
        flag = KINDlsGetNumFuncEvals(solverMem, &nfeD);
        check_flag(&flag, "KINDlsGetNumFuncEvals", 1);

        std::string logstr = "Kinsoln Statistics: \n";
        logstr += "Number of nonlinear iterations    = " + std::to_string(nni) + '\n';
        logstr += "Number of function evaluations    = " + std::to_string(nfe) + '\n';
        logstr += "Number of Jacobian evaluations    = " + std::to_string(nje) + '\n';
        if (nfeD > 0) {
            logstr += "Number of Jacobian function calls = " + std::to_string(nfeD) + '\n';
        }

        if (m_gds != nullptr) {
            m_gds->log(m_gds, logLevel, logstr);
        } else {
            fmt::print("\n{}", logstr);
        }
    }

    static const std::map<int, std::string> kinRetCodes{
        {KIN_MEM_NULL, "Null solver Memory"},
        {KIN_ILL_INPUT, "Illegal Input"},
        {KIN_NO_MALLOC, "No memory allocation"},
        {KIN_MEM_FAIL, "Memory Allocation failed"},
        {KIN_LINESEARCH_NONCONV, "Linesearch failed to converge"},
        {KIN_MAXITER_REACHED, "Max iteration reached"},
        {KIN_MXNEWT_5X_EXCEEDED,
         "Five consecutive steps have been taken that satisfy a scaled step length test"},
        {KIN_LINESEARCH_BCFAIL,
         "The linesearch algorithm was unable to satisfy the beta -condition for nbcfails iterations"},
        {KIN_LINSOLV_NO_RECOVERY,
         "The user-supplied routine preconditioner solve function failed recoverably, but the "
         "preconditioner is already current"},
        {KIN_LINIT_FAIL, "The linear solver's initialization function failed"},
        {KIN_LSETUP_FAIL, "The linear solver's setup function failed in an unrecoverable manner"},
        {KIN_LSOLVE_FAIL, "The linear solver's solve function failed in an unrecoverable manner"},
        {KIN_SYSFUNC_FAIL, "The system function failed in an unrecoverable manner"},
        {KIN_FIRST_SYSFUNC_ERR, "The system function failed recoverably at the first call"},
        {KIN_REPTD_SYSFUNC_ERR, "The system function had repeated recoverable errors"},
    };

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
            int retval = KINSetInfoFile(solverMem, m_sundialsInfoFile);
            check_flag(&retval, "KINSetInfoFile", 1);
            retval = KINSetPrintLevel(solverMem, solverPrintLevel);
            check_flag(&retval, "KINSetPrintLevel", 1);
        }

        int retval = KINSetUserData(solverMem, this);
        check_flag(&retval, "KINSetUserData", 1);

        // retval = KINSetFuncNormTol (solverMem, 1.e-9);
        retval = KINSetFuncNormTol(solverMem, tolerance);
        check_flag(&retval, "KINSetFuncNormTol", 1);

        // retval = KINSetScaledStepTol (solverMem, 1.e-9);
        retval = KINSetScaledStepTol(solverMem, tolerance / 100);
        check_flag(&retval, "KINSetScaledStepTol", 1);

        retval = KINSetNoInitSetup(solverMem, SUNTRUE);
        check_flag(&retval, "KINSetNoInitSetup", 1);

        retval = KINInit(solverMem, kinsolFunc, state);

        check_flag(&retval, "KINInit", 1);

#ifdef GRIDDYN_ENABLE_KLU
        if (flags[dense_flag]) {
            J = SUNDenseMatrix(svsize, svsize);
            check_flag(J, "SUNDenseMatrix", 0);
            /* Create KLU solver object */
            LS = SUNDenseLinearSolver(state, J);
            check_flag(LS, "SUNDenseLinearSolver", 0);
        } else {
            /* Create sparse SUNMatrix */
            J = SUNSparseMatrix(svsize, svsize, maxNNZ, CSR_MAT);
            check_flag(J, "SUNSparseMatrix", 0);

            /* Create KLU solver object */
            LS = SUNKLU(state, J);
            check_flag(LS, "SUNKLU", 0);

            retval = SUNKLUSetOrdering(LS, 0);
            check_flag(&retval, "SUNKLUSetOrdering", 1);
        }
#else
        J = SUNDenseMatrix(svsize, svsize);
        check_flag(J, "SUNSparseMatrix", 0);
        /* Create KLU solver object */
        LS = SUNDenseLinearSolver(state, J);
        check_flag(LS, "SUNDenseLinearSolver", 0);
#endif

        retval = KINDlsSetLinearSolver(solverMem, LS, J);

        check_flag(&retval, "KINDlsSetLinearSolver", 1);

        retval = KINDlsSetJacFn(solverMem, kinsolJac);
        check_flag(&retval, "KINDlsSetJacFn", 1);

        retval = KINSetMaxSetupCalls(solverMem, 1);  // exact Newton
        check_flag(&retval, "KINSetMaxSetupCalls", 1);

        retval = KINSetMaxSubSetupCalls(solverMem, 2);  // residual calls
        check_flag(&retval, "KINSetMaxSubSetupCalls", 1);

        retval = KINSetNumMaxIters(solverMem, max_iterations);  // residual calls
        check_flag(&retval, "KINSetNumMaxIters", 1);

        retval = KINSetErrHandlerFn(solverMem, sundialsErrorHandlerFunc, this);
        check_flag(&retval, "KINSetErrHandlerFn", 1);

        flags.set(initialized_flag);
    }

    void kinsolInterface::sparseReInit(sparse_reinit_modes sparseReinitMode)
    {
        KLUReInit(sparseReinitMode);
    }

    void kinsolInterface::set(const std::string& param, const std::string& val)
    {
        if (param.empty()) {
        } else {
            sundialsInterface::set(param, val);
        }
    }

    void kinsolInterface::set(const std::string& param, double val)
    {
        if (param.empty()) {
        } else if (param == "maxiterations") {
            max_iterations = static_cast<count_t>(val);
            int retval = KINSetNumMaxIters(solverMem, max_iterations);
            check_flag(&retval, "KINSetNumMaxIters", 1);
        } else {
            sundialsInterface::set(param, val);
        }
    }

    double kinsolInterface::get(const std::string& param) const
    {
        long int val = -1;
        if (param == "jac calls") {
            KINDlsGetNumJacEvals(solverMem, &val);
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
            return sundialsInterface::get(param);
        }
        return static_cast<double>(val);
    }

#define SHOW_MISSING_ELEMENTS 0

    //#define KIN_NONE       0
    //#define KIN_LINESEARCH 1
    //#define KIN_PICARD     2
    //#define KIN_FP         3
    int kinsolInterface::solve(coreTime tStop, coreTime& tReturn, step_mode /*mode*/)
    {
        // check if the multiple data sets are in use and if we should toggle the data to use
        solveTime = tStop;
#if MEASURE_TIMINGS > 0
        auto start_t = std::chrono::high_resolution_clock::now();

        int retval = KINSol(solverMem, state, KIN_NONE, scale, scale);
        auto stop_t = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_t = stop_t - start_t;
        kinTime += elapsed_t.count();
        fmt::print("total solve time {}, {:5.3f}% in resid {:5.3f}%  in Jacobian\n",
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
                    fmt::format("state[{}]{} following state {} is singular\n",
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
        if (sd->flags[print_residuals] || true ) {
            long int val = 0;
            KINGetNumNonlinSolvIters(sd->solverMem, &val);
            double* residuals = NVECTOR_DATA(sd->use_omp, resid);
            double* values = NVECTOR_DATA(sd->use_omp,state);
            fmt::print("Residual for {} at time ={} iteration %{}\n",
                       sd->getName(),
                       static_cast<double>(sd->solveTime),
                       val);
            for (int kk = 0; kk < static_cast<int>(sd->svsize); ++kk) {
                fmt::print("value[{}] = {}, resid[{}]={}\n", kk, values[kk],kk, residuals[kk]);
            }
            fmt::print("---------------------------------\n");
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

}  // namespace solvers
}  // namespace griddyn
