/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "arkodeInterface.h"

#include "../gridDynSimulation.h"
#include "../simulation/gridDynSimulationFileOps.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <arkode/arkode.h>
#include <arkode/arkode_arkstep.h>

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <sunlinsol/sunlinsol_dense.h>
#include <vector>

namespace griddyn::solvers {
int arkodeFunc(realtype time, N_Vector state, N_Vector dstate_dt, void* user_data);
int arkodeJac(realtype time,
              N_Vector state,
              N_Vector dstate_dt,
              SUNMatrix J,
              void* user_data,
              N_Vector tmp1,
              N_Vector tmp2,
              N_Vector tmp3);

int arkodeRootFunc(realtype time, N_Vector state, realtype* gout, void* user_data);

arkodeInterface::arkodeInterface(const std::string& objName): sundialsInterface(objName)
{
    mode.dynamic = true;
    mode.differential = true;
    mode.algebraic = false;
    max_iterations = 1500;
}

arkodeInterface::arkodeInterface(gridDynSimulation* gds, const solverMode& sMode):
    sundialsInterface(gds, sMode)
{
    mode.dynamic = true;
    mode.differential = true;
    mode.algebraic = false;
    max_iterations = 1500;
}

arkodeInterface::~arkodeInterface()
{
    // clear variables for CVode to use
    if (flags[initialized_flag]) {
        ARKodeFree(&solverMem);
    }
}

std::unique_ptr<SolverInterface> arkodeInterface::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<arkodeInterface>();
    arkodeInterface::cloneTo(si.get(), fullCopy);
    return si;
}

void arkodeInterface::cloneTo(SolverInterface* si, bool fullCopy) const
{
    sundialsInterface::cloneTo(si, fullCopy);
    auto ai = dynamic_cast<arkodeInterface*>(si);
    if (ai == nullptr) {
        return;
    }
    ai->maxStep = maxStep;
    ai->minStep = minStep;
    ai->step = step;
}

void arkodeInterface::allocate(count_t stateCount, count_t numRoots)
{
    // load the vectors
    if (stateCount == svsize) {
        return;
    }
    flags.reset(initialized_flag);

    a1.setRowLimit(stateCount);
    a1.setColLimit(stateCount);

    // update the rootCount
    rootCount = numRoots;
    rootsfound.resize(numRoots);

    // allocate the solverMemory
    if (solverMem != nullptr) {
        ARKodeFree(&(solverMem));
    }
    solverMem = ARKodeCreate();
    check_flag(solverMem, "ARKodeCVodeCreate", 0);

    sundialsInterface::allocate(stateCount, numRoots);
}

void arkodeInterface::setMaxNonZeros(count_t nonZeroCount)
{
    maxNNZ = nonZeroCount;
    a1.reserve(nonZeroCount);
    a1.clear();
}

void arkodeInterface::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        sundialsInterface::set(param, val);
    }
}

void arkodeInterface::set(std::string_view param, double val)
{
    bool checkStepUpdate = false;
    if (param == "step") {
        if ((maxStep < 0) || (maxStep == step)) {
            maxStep = val;
        }
        if ((minStep < 0) || (minStep == step)) {
            minStep = val;
        }
        step = val;
        checkStepUpdate = true;
    } else if (param == "maxstep") {
        maxStep = val;
        checkStepUpdate = true;
    } else if (param == "minstep") {
        minStep = val;
        checkStepUpdate = true;
    } else if (param == "maxiterations") {
        max_iterations = static_cast<count_t>(val);
        int retval = ARKodeSetMaxNumSteps(solverMem, max_iterations);
        check_flag(&retval, "ARKodeSetMaxNumSteps", 1);
    } else {
        SolverInterface::set(param, val);
    }

    if (checkStepUpdate) {
        if (flags[initialized_flag]) {
            ARKodeSetMaxStep(solverMem, maxStep);
            ARKodeSetMinStep(solverMem, minStep);
            ARKodeSetInitStep(solverMem, step);
        }
    }
}

double arkodeInterface::get(std::string_view param) const
{
    int val = -1;
    if ((param == "resevals") || (param == "iterationcount")) {
        //    CVodeGetNumResEvals(solverMem, &val);
    } else if (param == "iccount") {
        val = icCount;
    } else if (param == "jac calls") {
#ifdef ENABLE_KLU
//    CVodeCVodeSlsGetNumJacEvals(solverMem, &val);
#else
        ARKDlsGetNumJacEvals(solverMem, &val);
#endif
    } else {
        return sundialsInterface::get(param);
    }

    return static_cast<double>(val);
}

// output solver stats
void arkodeInterface::logSolverStats(print_level logLevel, bool /*iconly*/) const
{
    if (!flags[initialized_flag]) {
        return;
    }
    int nni = 0;
    int nst, nre, nfi, netf, ncfn, nge;
    realtype tolsfac, hlast, hcur;

    int retval = ARKodeGetNumRhsEvals(solverMem, &nre, &nfi);
    check_flag(&retval, "ARKodeGetNumResEvals", 1);

    retval = ARKodeGetNumNonlinSolvIters(solverMem, &nni);
    check_flag(&retval, "ARKodeGetNumNonlinSolvIters", 1);
    retval = ARKodeGetNumNonlinSolvConvFails(solverMem, &ncfn);
    check_flag(&retval, "ARKodeGetNumNonlinSolvConvFails", 1);

    retval = ARKodeGetNumSteps(solverMem, &nst);
    check_flag(&retval, "ARKodeGetNumSteps", 1);
    retval = ARKodeGetNumErrTestFails(solverMem, &netf);
    check_flag(&retval, "ARKodeGetNumErrTestFails", 1);

    retval = ARKodeGetNumGEvals(solverMem, &nge);
    check_flag(&retval, "ARKodeGetNumGEvals", 1);
    ARKodeGetCurrentStep(solverMem, &hcur);

    ARKodeGetLastStep(solverMem, &hlast);
    ARKodeGetTolScaleFactor(solverMem, &tolsfac);

    std::string logstr = "Arkode Run Statistics: \n";

    logstr += "Number of steps                    = " + std::to_string(nst) + '\n';
    logstr += "Number of rhs evaluations     = " + std::to_string(nre) + std::to_string(nfi) + '\n';
    logstr += "Number of Jacobian evaluations     = " + std::to_string(jacCallCount) + '\n';
    logstr += "Number of nonlinear iterations     = " + std::to_string(nni) + '\n';
    logstr += "Number of error test failures      = " + std::to_string(netf) + '\n';
    logstr += "Number of nonlinear conv. failures = " + std::to_string(ncfn) + '\n';
    logstr += "Number of root fn. evaluations     = " + std::to_string(nge) + '\n';

    logstr += "Current step                       = " + std::to_string(hcur) + '\n';
    logstr += "Last step                          = " + std::to_string(hlast) + '\n';
    logstr += "Tolerance scale factor             = " + std::to_string(tolsfac) + '\n';

    if (m_gds != nullptr) {
        m_gds->log(m_gds, logLevel, logstr);
    } else {
        printf("\n%s", logstr.c_str());
    }
}

void arkodeInterface::logErrorWeights(print_level logLevel) const
{
    N_Vector eweight = NVECTOR_NEW(use_omp, svsize);
    N_Vector ele = NVECTOR_NEW(use_omp, svsize);

    realtype* eldata = NVECTOR_DATA(use_omp, ele);
    realtype* ewdata = NVECTOR_DATA(use_omp, eweight);
    int retval = ARKodeGetErrWeights(solverMem, eweight);
    check_flag(&retval, "ARKodeGetErrWeights", 1);
    retval = ARKodeGetEstLocalErrors(solverMem, ele);
    check_flag(&retval, "ARKodeGetEstLocalErrors ", 1);
    std::string logstr = "Error Weight\tEstimated Local Errors\n";
    for (index_t kk = 0; kk < svsize; ++kk) {
        logstr += std::to_string(kk) + ':' + std::to_string(ewdata[kk]) + '\t' +
            std::to_string(eldata[kk]) + '\n';
    }

    if (m_gds != nullptr) {
        m_gds->log(m_gds, logLevel, logstr);
    } else {
        printf("\n%s", logstr.c_str());
    }
    NVECTOR_DESTROY(use_omp, eweight);
    NVECTOR_DESTROY(use_omp, ele);
}

static const std::map<int, std::string> arkodeRetCodes{
    {ARK_MEM_NULL, "The solver memory argument was NULL"},
    {ARK_ILL_INPUT, "One of the function inputs is illegal"},
    {ARK_NO_MALLOC, "The solver memory was not allocated by a call to CVodeMalloc"},
    {ARK_TOO_MUCH_WORK, "The solver took the maximum internal steps but could not reach tout"},
    {ARK_TOO_MUCH_ACC,
     "The solver could not satisfy the accuracy demanded by the user for some internal step"},
    {ARK_TOO_CLOSE, "t0 and tout are too close and user didn't specify a step size"},
    {ARK_LINIT_FAIL, "The linear solver's initialization function failed"},
    {ARK_LSETUP_FAIL, "The linear solver's setup function failed in an unrecoverable manner"},
    {ARK_LSOLVE_FAIL, "The linear solver's solve function failed in an unrecoverable manner"},
    {ARK_ERR_FAILURE, "The error test occurred too many times"},
    {ARK_MEM_FAIL, "A memory allocation failed"},
    {ARK_CONV_FAILURE, "convergence test failed too many times"},
    {ARK_BAD_T, "The time t is outside the last step taken"},
    {ARK_FIRST_RHSFUNC_ERR,
     "The user - provided derivative function failed recoverably on the first call"},
    {ARK_REPTD_RHSFUNC_ERR,
     "convergence test failed with repeated recoverable errors in the derivative function"},
    {ARK_RTFUNC_FAIL, "The rootfinding function failed in an unrecoverable manner"},
    {ARK_UNREC_RHSFUNC_ERR,
     "The user-provided right hand side function repeatedly returned a recoverable error "
     "flag, but the solver was unable to recover"},
    {ARK_BAD_K, "Bad K"},
    {ARK_BAD_DKY, "Bad DKY"},

};

void arkodeInterface::initialize(coreTime time0)
{
    if (!flags[allocated_flag]) {
        throw(InvalidSolverOperation());
    }
    auto jsize = m_gds->jacSize(mode);

    // dynInitializeB CVode - Sundials

    int retval = ARKodeSetUserData(solverMem, this);
    check_flag(&retval, "ARKodeSetUserData", 1);

    // guessState an initial condition
    m_gds->guessState(time0, state_data(), deriv_data(), mode);

    retval = ARKodeInit(solverMem, arkodeFunc, arkodeFunc, time0, state);
    check_flag(&retval, "ARKodeInit", 1);

    if (rootCount > 0) {
        rootsfound.resize(rootCount);
        retval = ARKodeRootInit(solverMem, rootCount, arkodeRootFunc);
        check_flag(&retval, "ARKodeRootInit", 1);
    }

    N_VConst(tolerance, abstols);

    retval = ARKodeSVtolerances(solverMem, tolerance / 100, abstols);
    check_flag(&retval, "ARKodeSVtolerances", 1);

    retval = ARKodeSetMaxNumSteps(solverMem, max_iterations);
    check_flag(&retval, "ARKodeSetMaxNumSteps", 1);

#ifdef ENABLE_KLU
    if (flags[dense_flag]) {
        J = SUNDenseMatrix(svsize, svsize);
        check_flag(J, "SUNDenseMatrix", 0);
        /* Create KLU solver object */
        LS = SUNDenseLinearSolver(state, J);
        check_flag(LS, "SUNDenseLinearSolver", 0);
    } else {
        /* Create sparse SUNMatrix */
        J = SUNSparseMatrix(svsize, svsize, jsize, CSR_MAT);
        check_flag(J, "SUNSparseMatrix", 0);

        /* Create KLU solver object */
        LS = SUNKLU(state, J);
        check_flag(LS, "SUNKLU", 0);
    }
#else
    J = SUNDenseMatrix(svsize, svsize);
    check_flag(J, "SUNSparseMatrix", 0);
    /* Create KLU solver object */
    LS = SUNDenseLinearSolver(state, J);
    check_flag(LS, "SUNDenseLinearSolver", 0);
#endif

    retval = ARKDlsSetLinearSolver(solverMem, LS, J);

    check_flag(&retval, "IDADlsSetLinearSolver", 1);

    retval = ARKDlsSetJacFn(solverMem, arkodeJac);
    check_flag(&retval, "IDADlsSetJacFn", 1);

    retval = ARKodeSetMaxNonlinIters(solverMem, 20);
    check_flag(&retval, "ARKodeSetMaxNonlinIters", 1);

    retval = ARKodeSetErrHandlerFn(solverMem, sundialsErrorHandlerFunc, this);
    check_flag(&retval, "ARKodeSetErrHandlerFn", 1);

    if (maxStep > 0.0) {
        retval = ARKodeSetMaxStep(solverMem, maxStep);
        check_flag(&retval, "ARKodeSetMaxStep", 1);
    }
    if (minStep > 0.0) {
        retval = ARKodeSetMinStep(solverMem, minStep);
        check_flag(&retval, "ARKodeSetMinStep", 1);
    }
    if (step > 0.0) {
        retval = ARKodeSetInitStep(solverMem, step);
        check_flag(&retval, "ARKodeSetInitStep", 1);
    }
    setConstraints();
    flags.set(initialized_flag);
}

void arkodeInterface::sparseReInit(sparse_reinit_modes sparseReinitMode)
{
    KLUReInit(sparseReinitMode);
}

void arkodeInterface::setRootFinding(count_t numRoots)
{
    if (numRoots != static_cast<count_t>(rootsfound.size())) {
        rootsfound.resize(numRoots);
    }
    rootCount = numRoots;
    int retval = ARKodeRootInit(solverMem, numRoots, arkodeRootFunc);
    check_flag(&retval, "ARKodeRootInit", 1);
}

void arkodeInterface::getCurrentData()
{
    /*
int retval = CVodeGetConsistentIC(solverMem, state, deriv);
if (check_flag(&retval, "CVodeGetConsistentIC", 1))
{
return(retval);
}
*/
}

int arkodeInterface::solve(coreTime tStop, coreTime& tReturn, step_mode stepMode)
{
    assert(rootCount == m_gds->rootSize(mode));
    ++solverCallCount;
    icCount = 0;
    double tret;
    int retval = ARKode(solverMem,
                        tStop,
                        state,
                        &tret,
                        (stepMode == step_mode::normal) ? ARK_NORMAL : ARK_ONE_STEP);
    tReturn = tret;
    check_flag(&retval, "ARKodeSolve", 1, false);

    if (retval == ARK_ROOT_RETURN) {
        retval = SOLVER_ROOT_FOUND;
    }
    return retval;
}

void arkodeInterface::getRoots()
{
    int ret = ARKodeGetRootInfo(solverMem, rootsfound.data());
    check_flag(&ret, "ARKodeGetRootInfo", 1);
}

void arkodeInterface::loadMaskElements()
{
    std::vector<double> mStates(svsize, 0.0);
    m_gds->getVoltageStates(mStates.data(), mode);
    m_gds->getAngleStates(mStates.data(), mode);
    maskElements = vecFindgt<double, index_t>(mStates, 0.5);
    tempState.resize(svsize);
    double* lstate = NV_DATA_S(state);
    for (auto& v : maskElements) {
        tempState[v] = lstate[v];
    }
}

// CVode C Functions
int arkodeFunc(realtype time, N_Vector state, N_Vector dstate_dt, void* user_data)
{
    auto sd = reinterpret_cast<arkodeInterface*>(user_data);
    sd->funcCallCount++;
    if (sd->mode.pairedOffsetIndex != kNullLocation) {
        int ret = sd->m_gds->dynAlgebraicSolve(time,
                                               NVECTOR_DATA(sd->use_omp, state),
                                               NVECTOR_DATA(sd->use_omp, dstate_dt),
                                               sd->mode);
        if (ret < FUNCTION_EXECUTION_SUCCESS) {
            return ret;
        }
    }
    int ret = sd->m_gds->derivativeFunction(time,
                                            NVECTOR_DATA(sd->use_omp, state),
                                            NVECTOR_DATA(sd->use_omp, dstate_dt),
                                            sd->mode);

    if (sd->flags[fileCapture_flag]) {
        if (!sd->stateFile.empty()) {
            writeVector(time,
                        STATE_INFORMATION,
                        sd->funcCallCount,
                        sd->mode.offsetIndex,
                        sd->svsize,
                        NVECTOR_DATA(sd->use_omp, state),
                        sd->stateFile,
                        (sd->funcCallCount != 1));
            writeVector(time,
                        DERIVATIVE_INFORMATION,
                        sd->funcCallCount,
                        sd->mode.offsetIndex,
                        sd->svsize,
                        NVECTOR_DATA(sd->use_omp, dstate_dt),
                        sd->stateFile);
        }
    }
    return ret;
}

int arkodeRootFunc(realtype time, N_Vector state, realtype* gout, void* user_data)
{
    auto sd = reinterpret_cast<arkodeInterface*>(user_data);
    sd->m_gds->rootFindingFunction(
        time, NVECTOR_DATA(sd->use_omp, state), sd->deriv_data(), gout, sd->mode);

    return FUNCTION_EXECUTION_SUCCESS;
}

int arkodeJac(realtype time,
              N_Vector state,
              N_Vector dstate_dt,
              SUNMatrix J,
              void* user_data,
              N_Vector tmp1,
              N_Vector tmp2,
              N_Vector /*tmp3*/)
{
    return sundialsJac(time, 0.0, state, dstate_dt, J, user_data, tmp1, tmp2);
}

}  // namespace griddyn::solvers
