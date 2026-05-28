/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CvodeInterface.h"

#include "../GridDynSimulation.h"
#include "../simulation/GridDynSimulationFileOps.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cvode/cvode.h>
#include <cvode/cvode_ls.h>

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#include <cassert>
#include <cstdio>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <sunlinsol/sunlinsol_dense.h>
#include <vector>

namespace griddyn::solvers {
int cvodeFunc(sunrealtype time, N_Vector state, N_Vector dstateDt, void* userData);

int cvodeJac(sunrealtype time,
             N_Vector state,
             N_Vector dstateDt,
             SUNMatrix j,
             void* userData,
             N_Vector tmp1,
             N_Vector tmp2,
             N_Vector tmp3);

int cvodeRootFunc(sunrealtype time, N_Vector state, sunrealtype* gout, void* userData);

CvodeInterface::CvodeInterface(const std::string& objName): SundialsInterface(objName)
{
    mode.dynamic = true;
    mode.differential = true;
    mode.algebraic = false;
    max_iterations = 1500;
}

CvodeInterface::CvodeInterface(GridDynSimulation* gds, const SolverMode& sMode):
    SundialsInterface(gds, sMode)
{
    mode.dynamic = true;
    mode.differential = true;
    mode.algebraic = false;
    max_iterations = 1500;
}

CvodeInterface::~CvodeInterface()
{
    // clear variables for CVode to use
    if (flags[INITIALIZED_FLAG]) {
        CVodeFree(&solverMem);
    }
}

std::unique_ptr<SolverInterface> CvodeInterface::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<CvodeInterface>();
    CvodeInterface::cloneTo(si.get(), fullCopy);
    return si;
}

void CvodeInterface::cloneTo(SolverInterface* si, bool fullCopy) const
{
    SundialsInterface::cloneTo(si, fullCopy);
    auto ai = dynamic_cast<CvodeInterface*>(si);
    if (ai == nullptr) {
        return;
    }
    ai->maxStep = maxStep;
    ai->minStep = minStep;
    ai->step = step;
}

void CvodeInterface::allocate(count_t stateCount, count_t numRoots)
{
    // load the vectors
    if (stateCount == svsize) {
        return;
    }
    flags.reset(INITIALIZED_FLAG);
    a1.setRowLimit(stateCount);
    a1.setColLimit(stateCount);

    // update the rootCount
    rootCount = numRoots;
    rootsfound.resize(numRoots);

    // allocate the solverMemory
    if (solverMem != nullptr) {
        CVodeFree(&(solverMem));
    }
    solverMem = CVodeCreate(CV_ADAMS, sunctx);
    checkFlag(solverMem, "CVodeCreate", 0);

    SundialsInterface::allocate(stateCount, numRoots);
}

void CvodeInterface::setMaxNonZeros(count_t nonZeroCount)
{
    maxNNZ = nonZeroCount;
    a1.reserve(nonZeroCount);
    a1.clear();
}

void CvodeInterface::set(std::string_view param, std::string_view val)
{
    if (param.empty() || param[0] == '#') {
    } else {
        SolverInterface::set(param, val);
    }
}

void CvodeInterface::set(std::string_view param, double val)
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
        int retval = CVodeSetMaxNumSteps(solverMem, max_iterations);
        checkFlag(&retval, "CVodeSetMaxNumSteps", 1);
    } else {
        SolverInterface::set(param, val);
    }
    if (checkStepUpdate) {
        if (flags[INITIALIZED_FLAG]) {
            CVodeSetMaxStep(solverMem, maxStep);
            CVodeSetMinStep(solverMem, minStep);
            CVodeSetInitStep(solverMem, step);
        }
    }
}

double CvodeInterface::get(std::string_view param) const
{
    int val = -1;
    if ((param == "resevals") || (param == "iterationcount")) {
        //    CVodeGetNumResEvals(solverMem, &val);
    } else if (param == "iccount") {
        val = icCount;
    } else if (param == "jac calls") {
#ifdef GRIDDYN_ENABLE_KLU
//    CVodeCVodeSlsGetNumJacEvals(solverMem, &val);
#else
        CVodeGetNumJacEvals(solverMem, &val);
#endif
    } else {
        return SundialsInterface::get(param);
    }

    return static_cast<double>(val);
}

// output solver stats
void CvodeInterface::logSolverStats(PrintLevel logLevel, bool /*iconly*/) const
{
    if (!flags[INITIALIZED_FLAG]) {
        return;
    }
    long nni = 0;
    int klast, kcur;
    long nst, nre, netf, ncfn, nge;
    sunrealtype tolsfac, hlast, hcur;

    int retval = CVodeGetNumRhsEvals(solverMem, &nre);
    checkFlag(&retval, "CVodeGetNumRhsEvals", 1);
    retval = CVodeGetNumNonlinSolvIters(solverMem, &nni);
    checkFlag(&retval, "CVodeGetNumNonlinSolvIters", 1);
    retval = CVodeGetNumNonlinSolvConvFails(solverMem, &ncfn);
    checkFlag(&retval, "CVodeGetNumNonlinSolvConvFails", 1);

    retval = CVodeGetNumSteps(solverMem, &nst);
    checkFlag(&retval, "CVodeGetNumSteps", 1);
    retval = CVodeGetNumErrTestFails(solverMem, &netf);
    checkFlag(&retval, "CVodeGetNumErrTestFails", 1);

    retval = CVodeGetNumGEvals(solverMem, &nge);
    checkFlag(&retval, "CVodeGetNumGEvals", 1);
    CVodeGetCurrentOrder(solverMem, &kcur);
    checkFlag(&retval, "VodeGetCurrentOrder", 1);
    CVodeGetCurrentStep(solverMem, &hcur);
    checkFlag(&retval, "CVodeGetCurrentStep", 1);
    CVodeGetLastOrder(solverMem, &klast);
    checkFlag(&retval, " CVodeGetLastOrder", 1);
    CVodeGetLastStep(solverMem, &hlast);
    checkFlag(&retval, "CVodeGetLastStep", 1);
    CVodeGetTolScaleFactor(solverMem, &tolsfac);
    checkFlag(&retval, "CVodeGetTolScaleFactor", 1);

    auto logstr = std::format("CVode Run Statistics: \n"
                              "Number of steps                    = {}\n"
                              "Number of residual evaluations     = {}\n"
                              "Number of Jacobian evaluations     = {}\n"
                              "Number of nonlinear iterations     = {}\n"
                              "Number of error test failures      = {}\n"
                              "Number of nonlinear conv. failures = {}\n"
                              "Number of root fn. evaluations     = {}\n"
                              "Current order used                 = {}\n"
                              "Current step                       = {}\n"
                              "Last order used                    = {}\n"
                              "Last step                          = {}\n"
                              "Tolerance scale factor             = {}\n",
                              nst,
                              nre,
                              jacCallCount,
                              nni,
                              netf,
                              ncfn,
                              nge,
                              kcur,
                              hcur,
                              klast,
                              hlast,
                              tolsfac);

    if (m_gds != nullptr) {
        logging::logTo(m_gds, m_gds, logLevel, logstr);
    } else {
        printf("\n%s", logstr.c_str());
    }
}

void CvodeInterface::logErrorWeights(PrintLevel logLevel) const
{
    N_Vector eweight = NVECTOR_NEW(use_omp, svsize);
    N_Vector ele = NVECTOR_NEW(use_omp, svsize);
    sunrealtype* eldata = NVECTOR_DATA(use_omp, ele);
    sunrealtype* ewdata = NVECTOR_DATA(use_omp, eweight);
    int retval = CVodeGetErrWeights(solverMem, eweight);
    checkFlag(&retval, "CVodeGetErrWeights", 1);
    retval = CVodeGetEstLocalErrors(solverMem, ele);
    checkFlag(&retval, "CVodeGetEstLocalErrors", 1);
    std::string logstr = "Error Weight\tEstimated Local Errors\n";
    for (index_t kk = 0; kk < svsize; ++kk) {
        std::format_to(std::back_inserter(logstr), "{}:{}\t{}\n", kk, ewdata[kk], eldata[kk]);
    }

    if (m_gds != nullptr) {
        logging::logTo(m_gds, m_gds, logLevel, logstr);
    } else {
        printf("\n%s", logstr.c_str());
    }
    NVECTOR_DESTROY(use_omp, eweight);
    NVECTOR_DESTROY(use_omp, ele);
}

void CvodeInterface::initialize(coreTime time0)
{
    if (!flags[ALLOCATED_FLAG]) {
        throw(InvalidSolverOperation());
    }

    auto jsize = m_gds->jacSize(mode);

    // dynInitializeB CVode - Sundials

    int retval = CVodeSetUserData(solverMem, this);
    checkFlag(&retval, "CVodeSetUserData", 1);

    // guessState an initial condition
    m_gds->guessState(time0, stateData(), derivData(), mode);

    retval = CVodeInit(solverMem, cvodeFunc, time0, state);
    checkFlag(&retval, "CVodeInit", 1);

    if (rootCount > 0) {
        rootsfound.resize(rootCount);
        retval = CVodeRootInit(solverMem, rootCount, cvodeRootFunc);
        checkFlag(&retval, "CVodeRootInit", 1);
    }

    N_VConst(tolerance, abstols);

    retval = CVodeSVtolerances(solverMem, tolerance / 100, abstols);
    checkFlag(&retval, "CVodeSVtolerances", 1);

    retval = CVodeSetMaxNumSteps(solverMem, max_iterations);
    checkFlag(&retval, "CVodeSetMaxNumSteps", 1);

#ifdef GRIDDYN_ENABLE_KLU
    if (flags[DENSE_FLAG]) {
        J = SUNDenseMatrix(svsize, svsize, sunctx);
        checkFlag(J, "SUNDenseMatrix", 0);
        /* Create KLU solver object */
        LS = SUNLinSol_Dense(state, J, sunctx);
        checkFlag(LS, "SUNLinSol_Dense", 0);
    } else {
        /* Create sparse SUNMatrix */
        J = SUNSparseMatrix(svsize, svsize, jsize, CSR_MAT, sunctx);
        checkFlag(J, "SUNSparseMatrix", 0);

        /* Create KLU solver object */
        LS = SUNLinSol_KLU(state, J, sunctx);
        checkFlag(LS, "SUNLinSol_KLU", 0);
    }
#else
    J = SUNDenseMatrix(svsize, svsize, sunctx);
    checkFlag(J, "SUNSparseMatrix", 0);
    /* Create KLU solver object */
    LS = SUNLinSol_Dense(state, J, sunctx);
    checkFlag(LS, "SUNLinSol_Dense", 0);
#endif

    retval = CVodeSetLinearSolver(solverMem, LS, J);

    checkFlag(&retval, "CVodeSetLinearSolver", 1);

    retval = CVodeSetJacFn(solverMem, cvodeJac);
    checkFlag(&retval, "CVodeSetJacFn", 1);

    retval = CVodeSetMaxNonlinIters(solverMem, 20);
    checkFlag(&retval, "CVodeSetMaxNonlinIters", 1);

    if (maxStep > 0.0) {
        retval = CVodeSetMaxStep(solverMem, maxStep);
        checkFlag(&retval, "CVodeSetMaxStep", 1);
    }
    if (minStep > 0.0) {
        retval = CVodeSetMinStep(solverMem, minStep);
        checkFlag(&retval, "CVodeSetMinStep", 1);
    }
    if (step > 0.0) {
        retval = CVodeSetInitStep(solverMem, step);
        checkFlag(&retval, "CVodeSetInitStep", 1);
    }
    setConstraints();

    flags.set(INITIALIZED_FLAG);
}

void CvodeInterface::sparseReInit(SparseReinitMode reInitMode)
{
    kluReInit(reInitMode);
}

void CvodeInterface::setRootFinding(count_t numRoots)
{
    if (numRoots != static_cast<index_t>(rootsfound.size())) {
        rootsfound.resize(numRoots);
    }
    rootCount = numRoots;
    int retval = CVodeRootInit(solverMem, numRoots, cvodeRootFunc);
    checkFlag(&retval, "CVodeRootInit", 1);
}

void CvodeInterface::getCurrentData()
{
    /*
int retval = CVodeGetConsistentIC(solverMem, state, deriv);
if (checkFlag(&retval, "CVodeGetConsistentIC", 1))
{
        return(retval);
}
*/
}

int CvodeInterface::solve(coreTime tStop, coreTime& tReturn, StepMode stepMode)
{
    assert(rootCount == m_gds->rootSize(mode));
    ++solverCallCount;
    icCount = 0;

    double tret;
    int retval = CVode(
        solverMem, tStop, state, &tret, (stepMode == StepMode::NORMAL) ? CV_NORMAL : CV_ONE_STEP);
    tReturn = tret;
    checkFlag(&retval, "CVodeSolve", 1, false);
    if (retval == CV_ROOT_RETURN) {
        retval = SOLVER_ROOT_FOUND;
    }
    if (retval >= 0) {
        // get the derivative information
        CVodeGetDky(solverMem, tStop, 1, dstate_dt);
    }
    return retval;
}

void CvodeInterface::getRoots()
{
    int ret = CVodeGetRootInfo(solverMem, rootsfound.data());
    checkFlag(&ret, "CVodeGetRootInfo", 1);
}

void CvodeInterface::loadMaskElements()
{
    std::vector<double> mStates(svsize, 0.0);
    m_gds->getVoltageStates(mStates.data(), mode);
    m_gds->getAngleStates(mStates.data(), mode);
    maskElements = gmlc::utilities::vecFindgt<double, index_t>(mStates, 0.5);
    tempState.resize(svsize);
    double* lstate = NV_DATA_S(state);
    for (auto& v : maskElements) {
        tempState[v] = lstate[v];
    }
}

// CVode C Functions
int cvodeFunc(sunrealtype time, N_Vector state, N_Vector dstateDt, void* userData)
{
    auto sd = reinterpret_cast<CvodeInterface*>(userData);
    sd->funcCallCount++;
    if (sd->mode.pairedOffsetIndex != kNullLocation) {
        int ret = sd->m_gds->dynAlgebraicSolve(time,
                                               NVECTOR_DATA(sd->use_omp, state),
                                               NVECTOR_DATA(sd->use_omp, dstateDt),
                                               sd->mode);
        if (ret < FUNCTION_EXECUTION_SUCCESS) {
            return ret;
        }
    }
    int ret = sd->m_gds->derivativeFunction(time,
                                            NVECTOR_DATA(sd->use_omp, state),
                                            NVECTOR_DATA(sd->use_omp, dstateDt),
                                            sd->mode);

    if (sd->flags[FILE_CAPTURE_FLAG]) {
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
                        NVECTOR_DATA(sd->use_omp, dstateDt),
                        sd->stateFile);
        }
    }

    return ret;
}

int cvodeRootFunc(sunrealtype time, N_Vector state, sunrealtype* gout, void* userData)
{
    auto sd = reinterpret_cast<CvodeInterface*>(userData);
    sd->m_gds->rootFindingFunction(
        time, NVECTOR_DATA(sd->use_omp, state), sd->derivData(), gout, sd->mode);

    return FUNCTION_EXECUTION_SUCCESS;
}

int cvodeJac(sunrealtype time,
             N_Vector state,
             N_Vector dstateDt,
             SUNMatrix j,
             void* userData,
             N_Vector tmp1,
             N_Vector tmp2,
             N_Vector /*tmp3*/)
{
    return sundialsJac(time, 0.0, state, dstateDt, j, userData, tmp1, tmp2);
}

}  // namespace griddyn::solvers
