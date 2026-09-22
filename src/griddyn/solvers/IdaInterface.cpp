/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "IdaInterface.h"

#include "../GridDynSimulation.h"
#include "../simulation/GridDynSimulationFileOps.h"
#include "SundialsMatrixData.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixDataFilter.hpp"
#include "utilities/matrixCreation.h"
#include <ida/ida.h>
#include <ida/ida_ls.h>
#include <sundials/sundials_math.h>

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <print>
#include <string>
#include <sunlinsol/sunlinsol_dense.h>
#include <vector>

namespace griddyn::solvers {
int idaFunc(sunrealtype time, N_Vector state, N_Vector dstateDt, N_Vector resid, void* userData);

int idaJac(sunrealtype time,
           sunrealtype cj,
           N_Vector state,
           N_Vector dstateDt,
           N_Vector resid,
           SUNMatrix j,
           void* userData,
           N_Vector tmp1,
           N_Vector tmp2,
           N_Vector tmp3);

int idaRootFunc(sunrealtype time,
                N_Vector state,
                N_Vector dstateDt,
                sunrealtype* gout,
                void* userData);

IdaInterface::IdaInterface(const std::string& objName): SundialsInterface(objName)
{
    max_iterations = 1500;
}

IdaInterface::IdaInterface(GridDynSimulation* gds, const SolverMode& sMode):
    SundialsInterface(gds, sMode)
{
    max_iterations = 1500;
}

IdaInterface::~IdaInterface()
{
    // clear variables for IDA to use
    if (flags[INITIALIZED_FLAG]) {
        IDAFree(&solverMem);
    }
}

std::unique_ptr<SolverInterface> IdaInterface::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<IdaInterface>();
    IdaInterface::cloneTo(si.get(), fullCopy);
    return si;
}

void IdaInterface::cloneTo(SolverInterface* si, bool fullCopy) const
{
    SundialsInterface::cloneTo(si, fullCopy);
    auto ai = dynamic_cast<IdaInterface*>(si);
    if (ai == nullptr) {
        return;
    }
}

void IdaInterface::allocate(count_t stateCount, count_t numRoots)
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
        IDAFree(&(solverMem));
    }
    solverMem = IDACreate(sunctx);
    checkFlag(solverMem, "IDACreate", 0);

    SundialsInterface::allocate(stateCount, numRoots);
}

void IdaInterface::setMaxNonZeros(count_t nonZeros)
{
    maxNNZ = nonZeros;
    jacCallCount = 0;
    a1.reserve(nonZeros);
    a1.clear();
}

void IdaInterface::set(std::string_view param, double val)
{
    if (param == "maxiterations") {
        max_iterations = static_cast<count_t>(val);
        // Solver parameters may be loaded from the simulation file before IDA's
        // memory block is allocated.  Keep the value for initialize(), where it
        // is applied unconditionally, instead of calling into IDA with nullptr.
        if (solverMem != nullptr) {
            int retval = IDASetMaxNumSteps(solverMem, max_iterations);
            checkFlag(&retval, "IDASetMaxNumSteps", 1);
        }
    } else {
        SundialsInterface::set(param, val);
    }
}

double IdaInterface::get(std::string_view param) const
{
    long int val = -1;
    if ((param == "resevals") || (param == "iterationcount")) {
        IDAGetNumResEvals(solverMem, &val);
    } else if (param == "iccount") {
        val = icCount;
    } else if (param == "nliterations") {
        IDAGetNumNonlinSolvIters(solverMem, &val);
    } else if (param == "jac calls") {
        IDAGetNumJacEvals(solverMem, &val);
    } else {
        return SundialsInterface::get(param);
    }

    return static_cast<double>(val);
}

// output solver stats
void IdaInterface::logSolverStats(PrintLevel logLevel, bool iconly) const
{
    if (!flags[INITIALIZED_FLAG]) {
        return;
    }
    long int nni = 0, nje = 0;
    int klast, kcur;
    long int nst, nre, nreLS, netf, ncfn, nge;
    sunrealtype tolsfac, hlast, hcur;

    std::string logstr;

    int retval = IDAGetNumResEvals(solverMem, &nre);
    checkFlag(&retval, "IDAGetNumResEvals", 1);
    retval = IDAGetNumJacEvals(solverMem, &nje);
    checkFlag(&retval, "IDAGetNumJacEvals", 1);
    retval = IDAGetNumNonlinSolvIters(solverMem, &nni);
    checkFlag(&retval, "IDAGetNumNonlinSolvIters", 1);
    retval = IDAGetNumNonlinSolvConvFails(solverMem, &ncfn);
    checkFlag(&retval, "IDAGetNumNonlinSolvConvFails", 1);
    if (!iconly) {
        retval = IDAGetNumSteps(solverMem, &nst);
        checkFlag(&retval, "IDAGetNumSteps", 1);
        retval = IDAGetNumErrTestFails(solverMem, &netf);
        checkFlag(&retval, "IDAGetNumErrTestFails", 1);
        retval = IDAGetNumLinResEvals(solverMem, &nreLS);
        checkFlag(&retval, "IDAGetNumLinResEvals", 1);
        retval = IDAGetNumGEvals(solverMem, &nge);
        checkFlag(&retval, "IDAGetNumGEvals", 1);
        retval = IDAGetCurrentOrder(solverMem, &kcur);
        checkFlag(&retval, "IDAGetCurrentOrder", 1);
        retval = IDAGetCurrentStep(solverMem, &hcur);
        checkFlag(&retval, "IDAGetCurrentStep", 1);
        retval = IDAGetLastOrder(solverMem, &klast);
        checkFlag(&retval, "IDAGetLastOrder", 1);
        retval = IDAGetLastStep(solverMem, &hlast);
        checkFlag(&retval, "IDAGetLastStep", 1);
        retval = IDAGetTolScaleFactor(solverMem, &tolsfac);
        checkFlag(&retval, "IDAGetTolScaleFactor", 1);
        logstr = std::format("IDA Run Statistics: \n"
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
                             nje,
                             nni,
                             netf,
                             ncfn,
                             nge,
                             kcur,
                             hcur,
                             klast,
                             hlast,
                             tolsfac);
    } else {
        logstr = std::format("IDACalcIC Statistics: \n"
                             "Number of residual evaluations     = {}\n"
                             "Number of Jacobian evaluations     = {}\n"
                             "Number of nonlinear iterations     = {}\n"
                             "Number of nonlinear conv. failures = {}\n",
                             nre,
                             nje,
                             nni,
                             ncfn);
    }

    if (m_gds != nullptr) {
        logging::logTo(m_gds, m_gds, logLevel, logstr);
    } else {
        printf("\n%s", logstr.c_str());
    }
}

void IdaInterface::logErrorWeights(PrintLevel logLevel) const
{
    N_Vector eweight = NVECTOR_NEW(use_omp, svsize);
    N_Vector ele = NVECTOR_NEW(use_omp, svsize);

    sunrealtype* eldata = NVECTOR_DATA(use_omp, ele);
    sunrealtype* ewdata = NVECTOR_DATA(use_omp, eweight);
    IDAGetErrWeights(solverMem, eweight);
    IDAGetEstLocalErrors(solverMem, ele);
    std::string logstr = "Error Weight\tEstimated Local Errors\n";
    for (count_t kk = 0; kk < svsize; ++kk) {
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

void IdaInterface::initialize(CoreTime t0)
{
    if (!flags[ALLOCATED_FLAG]) {
        throw(InvalidSolverOperation());
    }
    auto jsize = m_gds->jacSize(mode);

    // dynInitializeB IDA - Sundials

    int retval = IDASetUserData(solverMem, this);
    checkFlag(&retval, "IDASetUserData", 1);

    // guessState an initial condition
    m_gds->guessState(t0, stateData(), derivData(), mode);
    integrationReferenceState.clear();

    retval = IDAInit(solverMem, idaFunc, t0, state, dstate_dt);
    checkFlag(&retval, "IDAInit", 1);

    if (rootCount > 0) {
        rootsfound.resize(rootCount);
        retval = IDARootInit(solverMem, rootCount, idaRootFunc);
        checkFlag(&retval, "IDARootInit", 1);
    }

    N_VConst(tolerance, abstols);

    retval = IDASVtolerances(solverMem, tolerance / 100, abstols);
    checkFlag(&retval, "IDASVtolerances", 1);

    if (flags[IDA_INTEGRATION_DIAGNOSTICS]) {
        integrationFailureLogged = false;
        m_gds->getRootObjectNames(rootNames, mode);
        logging::logTo(m_gds,
                       m_gds,
                       PrintLevel::SUMMARY,
                       "IDA integration diagnostics: states={}, roots={}, configured_tolerance={}, "
                       "effective_relative_tolerance={}, absolute_tolerance={}, max_steps={}",
                       svsize,
                       rootCount,
                       tolerance,
                       tolerance / 100.0,
                       tolerance,
                       max_iterations);
    }

    retval = IDASetMaxNumSteps(solverMem, max_iterations);
    checkFlag(&retval, "IDASetMaxNumSteps", 1);

    freeLinearSolver();
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

        retval = SUNLinSol_KLUSetOrdering(LS, 0);
        checkFlag(&retval, "SUNLinSol_KLUSetOrdering", 1);
        SUNLinSol_KLUGetCommon(LS)->halt_if_singular = 1;
    }
#else
    J = SUNDenseMatrix(svsize, svsize, sunctx);
    checkFlag(J, "SUNSparseMatrix", 0);
    /* Create KLU solver object */
    LS = SUNLinSol_Dense(state, J, sunctx);
    checkFlag(LS, "SUNLinSol_Dense", 0);
#endif

    retval = IDASetLinearSolver(solverMem, LS, J);

    checkFlag(&retval, "IDASetLinearSolver", 1);

    retval = IDASetJacFn(solverMem, idaJac);
    checkFlag(&retval, "IDASetJacFn", 1);

    retval = IDASetMaxNonlinIters(solverMem, 20);
    checkFlag(&retval, "IDASetMaxNonlinIters", 1);

    m_gds->getVariableType(typeData(), mode);

    retval = IDASetId(solverMem, types);
    checkFlag(&retval, "IDASetId", 1);

    setConstraints();
    solveTime = t0;
    flags.set(INITIALIZED_FLAG);
}

void IdaInterface::sparseReInit(SparseReinitMode sparseReInitMode)
{
    kluReInit(sparseReInitMode);
}

void IdaInterface::setRootFinding(count_t numRoots)
{
    if (numRoots != static_cast<count_t>(rootsfound.size())) {
        rootsfound.resize(numRoots);
    }
    rootCount = numRoots;
    int retval = IDARootInit(solverMem, numRoots, idaRootFunc);
    checkFlag(&retval, "IDARootInit", 1);
}

#define SHOW_MISSING_ELEMENTS 0

namespace {
    std::string_view idaIcReturnFlagName(int retval)
    {
        switch (retval) {
            case IDA_SUCCESS:
                return "IDA_SUCCESS";
            case IDA_LSETUP_FAIL:
                return "IDA_LSETUP_FAIL";
            case IDA_LSOLVE_FAIL:
                return "IDA_LSOLVE_FAIL";
            case IDA_NO_RECOVERY:
                return "IDA_NO_RECOVERY";
            case IDA_LINESEARCH_FAIL:
                return "IDA_LINESEARCH_FAIL";
            case IDA_CONV_FAIL:
                return "IDA_CONV_FAIL";
            case IDA_REP_RES_ERR:
                return "IDA_REP_RES_ERR";
            case IDA_RES_FAIL:
                return "IDA_RES_FAIL";
            default:
                return "IDA_UNKNOWN";
        }
    }
}  // namespace

void IdaInterface::logInitialConditionDiagnostics(
    CoreTime t0,
    CoreTime tstep0,
    IcModes initCondMode,
    int retval,
    const std::vector<double>* initialState,
    const std::vector<double>* initialDerivative) const
{
    if ((m_gds == nullptr) || (svsize == 0)) {
        return;
    }

    const auto diagnosticLevel = (retval == IDA_SUCCESS) ? PrintLevel::SUMMARY : PrintLevel::ERROR;
    logSolverStats(diagnosticLevel, true);

    std::vector<double> residual(svsize, 0.0);
    const int residualStatus =
        m_gds->residualFunction(t0, stateData(), derivData(), residual.data(), mode);
    const double* currentState = stateData();
    const double* currentDerivative = derivData();

    std::vector<double> variableType(svsize, 1.0);
    m_gds->getVariableType(variableType.data(), mode);

    struct ResidualEntry {
        double magnitude;
        index_t index;
    };
    std::vector<ResidualEntry> entries;
    std::vector<ResidualEntry> algebraicResidualEntries;
    std::vector<ResidualEntry> differentialResidualEntries;
    entries.reserve(svsize);
    algebraicResidualEntries.reserve(svsize);
    differentialResidualEntries.reserve(svsize);

    count_t nonFiniteResiduals = 0;
    count_t algebraicEntries = 0;
    count_t differentialEntries = 0;
    double maxResidual = 0.0;
    double maxAlgebraicResidual = 0.0;
    double maxDifferentialResidual = 0.0;

    for (index_t index = 0; index < svsize; ++index) {
        const double value = residual[index];
        const bool finite = std::isfinite(value);
        const double magnitude = finite ? std::abs(value) : std::numeric_limits<double>::infinity();
        if (!finite) {
            ++nonFiniteResiduals;
        }
        maxResidual = (std::max)(maxResidual, magnitude);
        if (variableType[index] == 1.0) {
            ++differentialEntries;
            maxDifferentialResidual = (std::max)(maxDifferentialResidual, magnitude);
            differentialResidualEntries.push_back({magnitude, index});
        } else {
            ++algebraicEntries;
            maxAlgebraicResidual = (std::max)(maxAlgebraicResidual, magnitude);
            algebraicResidualEntries.push_back({magnitude, index});
        }
        entries.push_back({magnitude, index});
    }

    const auto entryOrder = [](const ResidualEntry& lhs, const ResidualEntry& rhs) {
        return lhs.magnitude > rhs.magnitude;
    };
    const auto entryCount = (std::min)(static_cast<count_t>(8), svsize);
    std::partial_sort(entries.begin(), entries.begin() + entryCount, entries.end(), entryOrder);
    const auto algebraicResidualEntryCount = (std::min)(size_t{8}, algebraicResidualEntries.size());
    std::partial_sort(algebraicResidualEntries.begin(),
                      algebraicResidualEntries.begin() + algebraicResidualEntryCount,
                      algebraicResidualEntries.end(),
                      entryOrder);
    const auto differentialResidualEntryCount =
        (std::min)(size_t{8}, differentialResidualEntries.size());
    std::partial_sort(differentialResidualEntries.begin(),
                      differentialResidualEntries.begin() + differentialResidualEntryCount,
                      differentialResidualEntries.end(),
                      entryOrder);

    stringVec stateNames;
    m_gds->getStateName(stateNames, mode);

    // A small residual after FIXED_DIFF initial-condition correction only
    // establishes DAE consistency: IDA is allowed to solve for y'.  A
    // no-disturbance stability case additionally needs those differential
    // derivatives to be small.  Report the largest ones separately so an IC
    // trace can distinguish a true equilibrium from a consistent trajectory
    // that immediately leaves its initialized state.
    std::vector<ResidualEntry> derivativeEntries;
    derivativeEntries.reserve(differentialEntries);
    count_t nonFiniteDerivatives = 0;
    double maxDifferentialDerivative = 0.0;
    for (index_t index = 0; index < svsize; ++index) {
        if (variableType[index] != 1.0) {
            continue;
        }
        const double value = currentDerivative[index];
        const bool finite = std::isfinite(value);
        const double magnitude = finite ? std::abs(value) : std::numeric_limits<double>::infinity();
        if (!finite) {
            ++nonFiniteDerivatives;
        }
        maxDifferentialDerivative = (std::max)(maxDifferentialDerivative, magnitude);
        derivativeEntries.push_back({magnitude, index});
    }
    const auto derivativeEntryCount = (std::min)(size_t{8}, derivativeEntries.size());
    std::partial_sort(derivativeEntries.begin(),
                      derivativeEntries.begin() + derivativeEntryCount,
                      derivativeEntries.end(),
                      entryOrder);

    logging::logTo(m_gds,
                   m_gds,
                   diagnosticLevel,
                   "IDA initial-condition diagnostics: return={} ({}), ic_mode={}, "
                   "state_size={}, residual_status={}, max_residual={}, "
                   "max_algebraic_residual={}, max_differential_residual={}, "
                   "nonfinite_residuals={}, algebraic_entries={}, differential_entries={}",
                   retval,
                   idaIcReturnFlagName(retval),
                   (initCondMode == IcModes::FIXED_DIFF) ? "FIXED_DIFF" : "FIXED_MASKED_AND_DERIV",
                   svsize,
                   residualStatus,
                   maxResidual,
                   maxAlgebraicResidual,
                   maxDifferentialResidual,
                   nonFiniteResiduals,
                   algebraicEntries,
                   differentialEntries);

    for (count_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        const auto& entry = entries[entryIndex];
        const auto stateName = (static_cast<size_t>(entry.index) < stateNames.size()) ?
            stateNames[entry.index] :
            std::string{"<unnamed>"};
        const bool hasInitialSnapshot = (initialState != nullptr) &&
            (initialDerivative != nullptr) &&
            (initialState->size() == static_cast<size_t>(svsize)) &&
            (initialDerivative->size() == static_cast<size_t>(svsize));
        logging::logTo(m_gds,
                       m_gds,
                       diagnosticLevel,
                       "IDA IC residual[{}] {} = {}, y={}, yp={}, type={}{}",
                       entry.index,
                       stateName,
                       residual[entry.index],
                       currentState[entry.index],
                       currentDerivative[entry.index],
                       variableType[entry.index],
                       hasInitialSnapshot ? std::format(", initial_y={}, initial_yp={}",
                                                        (*initialState)[entry.index],
                                                        (*initialDerivative)[entry.index]) :
                                            std::string{});
    }

    logging::logTo(m_gds,
                   m_gds,
                   diagnosticLevel,
                   "IDA IC algebraic-residual probe: max_abs_residual={}, reporting={}",
                   maxAlgebraicResidual,
                   algebraicResidualEntryCount);
    for (size_t entryIndex = 0; entryIndex < algebraicResidualEntryCount; ++entryIndex) {
        const auto& entry = algebraicResidualEntries[entryIndex];
        const auto stateName = (static_cast<size_t>(entry.index) < stateNames.size()) ?
            stateNames[entry.index] :
            std::string{"<unnamed>"};
        logging::logTo(m_gds,
                       m_gds,
                       diagnosticLevel,
                       "IDA IC algebraic residual[{}] {} = {}, y={}",
                       entry.index,
                       stateName,
                       residual[entry.index],
                       currentState[entry.index]);
    }

    logging::logTo(m_gds,
                   m_gds,
                   diagnosticLevel,
                   "IDA IC differential-residual probe: max_abs_residual={}, reporting={}",
                   maxDifferentialResidual,
                   differentialResidualEntryCount);
    for (size_t entryIndex = 0; entryIndex < differentialResidualEntryCount; ++entryIndex) {
        const auto& entry = differentialResidualEntries[entryIndex];
        const auto stateName = (static_cast<size_t>(entry.index) < stateNames.size()) ?
            stateNames[entry.index] :
            std::string{"<unnamed>"};
        logging::logTo(m_gds,
                       m_gds,
                       diagnosticLevel,
                       "IDA IC differential residual[{}] {} = {}, y={}, yp={}",
                       entry.index,
                       stateName,
                       residual[entry.index],
                       currentState[entry.index],
                       currentDerivative[entry.index]);
    }

    logging::logTo(m_gds,
                   m_gds,
                   diagnosticLevel,
                   "IDA IC differential-derivative probe: max_abs_yp={}, nonfinite_yp={}, "
                   "reporting={}",
                   maxDifferentialDerivative,
                   nonFiniteDerivatives,
                   derivativeEntryCount);
    for (size_t entryIndex = 0; entryIndex < derivativeEntryCount; ++entryIndex) {
        const auto& entry = derivativeEntries[entryIndex];
        const auto stateName = (static_cast<size_t>(entry.index) < stateNames.size()) ?
            stateNames[entry.index] :
            std::string{"<unnamed>"};
        logging::logTo(m_gds,
                       m_gds,
                       diagnosticLevel,
                       "IDA IC differential yp[{}] {} = {}, y={}",
                       entry.index,
                       stateName,
                       currentDerivative[entry.index],
                       currentState[entry.index]);
    }

    MatrixDataSparse<double> jacobian;
    jacobian.reserve(m_gds->jacSize(mode));
    const double step = static_cast<double>(tstep0);
    const double cj = (step > 0.0) ? 1.0 / step : 1.0;
    const int jacobianStatus =
        m_gds->jacobianFunction(t0, stateData(), derivData(), jacobian, cj, mode);
    jacobian.compact();

    std::vector<bool> rowPresent(svsize, false);
    std::vector<bool> columnPresent(svsize, false);
    std::vector<bool> diagonalPresent(svsize, false);
    count_t nonFiniteJacobian = 0;
    count_t zeroDiagonal = 0;
    for (const auto& entry : jacobian) {
        if ((entry.row < 0) || (entry.col < 0) || (entry.row >= static_cast<index_t>(svsize)) ||
            (entry.col >= static_cast<index_t>(svsize))) {
            continue;
        }
        rowPresent[entry.row] = true;
        columnPresent[entry.col] = true;
        if (!std::isfinite(entry.data)) {
            ++nonFiniteJacobian;
        }
        if (entry.row == entry.col) {
            diagonalPresent[entry.row] = true;
            if (entry.data == 0.0) {
                ++zeroDiagonal;
            }
        }
    }

    const auto missingRows =
        static_cast<count_t>(std::count(rowPresent.begin(), rowPresent.end(), false));
    const auto missingColumns =
        static_cast<count_t>(std::count(columnPresent.begin(), columnPresent.end(), false));
    const auto missingDiagonals =
        static_cast<count_t>(std::count(diagonalPresent.begin(), diagonalPresent.end(), false));
    logging::logTo(m_gds,
                   m_gds,
                   diagnosticLevel,
                   "IDA IC Jacobian probe: status={}, cj={}, nonzeros={}, nonfinite={}, "
                   "empty_rows={}, empty_columns={}, missing_diagonals={}, zero_diagonals={}",
                   jacobianStatus,
                   cj,
                   jacobian.size(),
                   nonFiniteJacobian,
                   missingRows,
                   missingColumns,
                   missingDiagonals,
                   zeroDiagonal);
}

void IdaInterface::logIntegrationFailureDiagnostics(CoreTime time, int retval) const
{
    if ((m_gds == nullptr) || (svsize == 0)) {
        return;
    }

    std::vector<double> residual(svsize, 0.0);
    const int residualStatus =
        m_gds->residualFunction(time, stateData(), derivData(), residual.data(), mode);
    stringVec stateNames;
    m_gds->getStateName(stateNames, mode);

    struct ResidualEntry {
        double magnitude;
        index_t index;
    };
    std::vector<ResidualEntry> entries;
    entries.reserve(svsize);
    double maxResidual = 0.0;
    count_t nonFiniteResiduals = 0;
    for (index_t index = 0; index < svsize; ++index) {
        const double value = residual[index];
        const bool finite = std::isfinite(value);
        const double magnitude = finite ? std::abs(value) : std::numeric_limits<double>::infinity();
        if (!finite) {
            ++nonFiniteResiduals;
        }
        maxResidual = (std::max)(maxResidual, magnitude);
        entries.push_back({magnitude, index});
    }
    const auto entryOrder = [](const ResidualEntry& lhs, const ResidualEntry& rhs) {
        return lhs.magnitude > rhs.magnitude;
    };
    const auto entryCount = (std::min)(static_cast<count_t>(8), svsize);
    std::partial_sort(entries.begin(), entries.begin() + entryCount, entries.end(), entryOrder);

    logging::logTo(m_gds,
                   m_gds,
                   PrintLevel::ERROR,
                   "IDA integration failure diagnostics: time={}, return={}, "
                   "residual_status={}, max_residual={}, nonfinite_residuals={}",
                   static_cast<double>(time),
                   retval,
                   residualStatus,
                   maxResidual,
                   nonFiniteResiduals);
    for (count_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        const auto& entry = entries[entryIndex];
        const auto stateName = (static_cast<size_t>(entry.index) < stateNames.size()) ?
            stateNames[entry.index] :
            std::string{"<unnamed>"};
        logging::logTo(m_gds,
                       m_gds,
                       PrintLevel::ERROR,
                       "IDA integration residual[{}] {} = {}, y={}, yp={}",
                       entry.index,
                       stateName,
                       residual[entry.index],
                       stateData()[entry.index],
                       derivData()[entry.index]);
    }
}

void IdaInterface::logIntegrationStateDrift(CoreTime time) const
{
    if ((m_gds == nullptr) || (integrationReferenceState.size() != static_cast<size_t>(svsize))) {
        return;
    }

    struct StateDeltaEntry {
        double magnitude;
        index_t index;
    };
    std::vector<StateDeltaEntry> entries;
    entries.reserve(svsize);
    count_t nonFiniteDeltas = 0;
    double maxDelta = 0.0;
    const double* currentState = stateData();
    for (index_t index = 0; index < svsize; ++index) {
        const double delta = currentState[index] - integrationReferenceState[index];
        const bool finite = std::isfinite(delta);
        const double magnitude = finite ? std::abs(delta) : std::numeric_limits<double>::infinity();
        if (!finite) {
            ++nonFiniteDeltas;
        }
        maxDelta = (std::max)(maxDelta, magnitude);
        entries.push_back({magnitude, index});
    }

    const auto entryOrder = [](const StateDeltaEntry& lhs, const StateDeltaEntry& rhs) {
        return lhs.magnitude > rhs.magnitude;
    };
    const auto entryCount = (std::min)(size_t{8}, entries.size());
    std::partial_sort(entries.begin(), entries.begin() + entryCount, entries.end(), entryOrder);

    stringVec stateNames;
    m_gds->getStateName(stateNames, mode);
    logging::logTo(m_gds,
                   m_gds,
                   PrintLevel::SUMMARY,
                   "IDA integration state drift: initial_time={}, current_time={}, "
                   "max_abs_delta={}, nonfinite_deltas={}",
                   static_cast<double>(integrationReferenceTime),
                   static_cast<double>(time),
                   maxDelta,
                   nonFiniteDeltas);
    for (size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        const auto& entry = entries[entryIndex];
        const auto stateName = (static_cast<size_t>(entry.index) < stateNames.size()) ?
            stateNames[entry.index] :
            std::string{"<unnamed>"};
        logging::logTo(m_gds,
                       m_gds,
                       PrintLevel::SUMMARY,
                       "IDA integration state delta[{}] {}: initial={}, current={}, delta={}",
                       entry.index,
                       stateName,
                       integrationReferenceState[entry.index],
                       currentState[entry.index],
                       currentState[entry.index] - integrationReferenceState[entry.index]);
    }
}

int IdaInterface::calcIC(CoreTime t0, CoreTime tstep0, IcModes initCondMode, bool constraints)
{
    int retval;
    ++icCount;
    assert(icCount < 200);
    std::vector<double> initialState;
    std::vector<double> initialDerivative;
    if (flags[IDA_IC_DIAGNOSTICS]) {
        initialState.assign(stateData(), stateData() + svsize);
        initialDerivative.assign(derivData(), derivData() + svsize);
    }
    if (initCondMode ==
        IcModes::FIXED_MASKED_AND_DERIV)  // mainly for use upon startup from steady state
    {
        // do a series of steps to ensure the original algebraic states are fixed and the
        // derivatives are fixed
        flags.set(USE_MASK_FLAG);
        loadMaskElements();
        if (!flags[DENSE_FLAG]) {
            sparseReInit(SparseReinitMode::REFACTOR);
        }
        retval = IDACalcIC(solverMem, IDA_Y_INIT, t0 + tstep0);  // IDA_Y_INIT
        if ((retval != IDA_SUCCESS) && flags[IDA_IC_DIAGNOSTICS]) {
            logInitialConditionDiagnostics(
                t0, tstep0, initCondMode, retval, &initialState, &initialDerivative);
        }

        // retval = IDACalcIC (solverMem, IDA_YA_YDP_INIT, t0 + tstep0); //IDA_YA_YDP_INIT
        //   getCurrentData();
        //  printStates(true);
        if (retval != 0) {
            // if the solver failed with error code -14 then we probably have a singular matrix
            // then locate the singular elements and fix them so the problem is valid
            if (retval == IDA_NO_RECOVERY) {
                auto mvec = findMissing(a1);
                if (!mvec.empty()) {
                    double* lstate = NV_DATA_S(state);
                    for (auto& me : mvec) {
                        maskElements.push_back(me);
                        tempState[me] = lstate[me];
                    }

                    if (!flags[DENSE_FLAG]) {
                        sparseReInit(SparseReinitMode::REFACTOR);
                    }
                    retval = IDACalcIC(solverMem, IDA_Y_INIT, t0 + tstep0);  // IDA_Y_INIT
                    if ((retval != IDA_SUCCESS) && flags[IDA_IC_DIAGNOSTICS]) {
                        logInitialConditionDiagnostics(
                            t0, tstep0, initCondMode, retval, &initialState, &initialDerivative);
                    }
                    if (retval == IDA_SUCCESS) {
                        flags.reset(USE_MASK_FLAG);
                        getCurrentData();
                        return FUNCTION_EXECUTION_SUCCESS;
                    }

                    flags.reset(USE_MASK_FLAG);
                    return SOLVER_INVALID_STATE_ERROR;
                }
            } else {
                flags.reset(USE_MASK_FLAG);
                switch (retval) {
                    case IDA_SUCCESS:  // no error
                        break;
                    case IDA_REP_RES_ERR:
                    case IDA_NO_RECOVERY:
                        retval = SOLVER_INVALID_STATE_ERROR;
                        break;
                    default:
                        break;
                }
                return retval;
            }
        }
        flags.reset(USE_MASK_FLAG);
        if (!flags[DENSE_FLAG]) {
            sparseReInit(SparseReinitMode::REFACTOR);
        }
        getCurrentData();
        if (flags[IDA_IC_DIAGNOSTICS]) {
            logInitialConditionDiagnostics(
                t0, tstep0, initCondMode, IDA_SUCCESS, &initialState, &initialDerivative);
        }
    } else if (initCondMode == IcModes::FIXED_DIFF) {
        retval = IDAReInit(solverMem, t0, state, dstate_dt);

        if (retval < 0) {
            return retval;
        }
        if (constraints) {
            setConstraints();
        }
        //  printStates();
        retval = IDACalcIC(solverMem, IDA_YA_YDP_INIT, t0 + tstep0);  // IDA_YA_YDP_INIT
        if ((retval != IDA_SUCCESS) && flags[IDA_IC_DIAGNOSTICS]) {
            logInitialConditionDiagnostics(
                t0, tstep0, initCondMode, retval, &initialState, &initialDerivative);
        }
        if (retval < 0) {
#if SHOW_MISSING_ELEMENTS > 0
            auto mvec = findMissing(&a1);
            if (mvec.size() > 0) {
                std::println("missing rows in Jacobian from calcIC mode 1");
            }
#endif
            switch (retval) {
                case IDA_REP_RES_ERR:
                case IDA_NO_RECOVERY:
                    retval = SOLVER_INVALID_STATE_ERROR;
                    break;
                default:
                    break;
            }
            return retval;
        }
        getCurrentData();
        if (flags[IDA_IC_DIAGNOSTICS]) {
            logInitialConditionDiagnostics(
                t0, tstep0, initCondMode, IDA_SUCCESS, &initialState, &initialDerivative);
        }
        //  printStates();
    }
    return FUNCTION_EXECUTION_SUCCESS;
}

void IdaInterface::getCurrentData()
{
    int retval = IDAGetConsistentIC(solverMem, state, dstate_dt);
    checkFlag(&retval, "IDAGetConsistentIC", 1);
}

int IdaInterface::solve(CoreTime tStop, CoreTime& tReturn, StepMode stepMode)
{
    // A diagnostic run can disable root finding at the simulation level while the
    // model retains its normal, nonzero root size.
    assert((rootCount == 0) || (rootCount == m_gds->rootSize(mode)));
    ++solverCallCount;
    icCount = 0;
    if (flags[IDA_INTEGRATION_DIAGNOSTICS]) {
        if (integrationReferenceState.empty()) {
            integrationReferenceState.assign(stateData(), stateData() + svsize);
            integrationReferenceTime = solveTime;
        }
        logging::logTo(m_gds,
                       m_gds,
                       PrintLevel::SUMMARY,
                       "IDA solve begin: call={}, from={}, target={}, residual_evaluations={}",
                       solverCallCount,
                       static_cast<double>(solveTime),
                       static_cast<double>(tStop),
                       funcCallCount);
    }
    double tret;
    int retval = IDASolve(solverMem,
                          tStop,
                          &tret,
                          state,
                          dstate_dt,
                          (stepMode == StepMode::NORMAL) ? IDA_NORMAL : IDA_ONE_STEP);
    tReturn = tret;
    if (flags[IDA_INTEGRATION_DIAGNOSTICS]) {
        logSolverStats(PrintLevel::SUMMARY);
        logging::logTo(m_gds,
                       m_gds,
                       PrintLevel::SUMMARY,
                       "IDA solve end: call={}, return={}, reached={}, residual_evaluations={}",
                       solverCallCount,
                       retval,
                       tret,
                       funcCallCount);
    }
    if ((retval < 0) && flags[IDA_INTEGRATION_DIAGNOSTICS] && !integrationFailureLogged) {
        logIntegrationFailureDiagnostics(tret, retval);
        integrationFailureLogged = true;
    }
    if ((retval == IDA_SUCCESS) || (retval == IDA_ROOT_RETURN)) {
        // IDASolve returns the state at the requested output time, but the
        // derivative vector can retain the last internal-step value for a
        // non-grid output time.  Refresh it from IDA's interpolation so the
        // state/derivative pair handed to GridDyn remains consistent.
        int dkyRet = IDAGetDky(solverMem, tret, 1, dstate_dt);
        checkFlag(&dkyRet, "IDAGetDky", 1);
    }
    if (flags[IDA_INTEGRATION_DIAGNOSTICS]) {
        logIntegrationStateDrift(tret);
    }
    solveTime = tret;
    switch (retval) {
        case IDA_SUCCESS:  // no error
            break;
        case IDA_ROOT_RETURN:
            retval = SOLVER_ROOT_FOUND;
            break;
        case IDA_TOO_MUCH_WORK:
            // IDA can return this after reaching an event boundary with a
            // valid forward-time state.  GridDyn's dynamic loop can restart
            // from that state, so keep it distinct from a hard convergence
            // failure while preserving the negative solver status.
            retval = SOLVER_STEP_LIMIT_REACHED;
            break;
        case IDA_REP_RES_ERR:
            retval = SOLVER_INVALID_STATE_ERROR;
            break;
        default:
            break;
    }
    return retval;
}

void IdaInterface::getRoots()
{
    int ret = IDAGetRootInfo(solverMem, rootsfound.data());
    checkFlag(&ret, "IDAGetRootInfo", 1);
    if (flags[IDA_INTEGRATION_DIAGNOSTICS]) {
        std::string activeRoots;
        for (size_t kk = 0; kk < rootsfound.size(); ++kk) {
            if (rootsfound[kk] != 0) {
                if (!activeRoots.empty()) {
                    activeRoots += ',';
                }
                activeRoots += std::format("{}(direction={})", kk, rootsfound[kk]);
                if ((kk < rootNames.size()) && !rootNames[kk].empty()) {
                    activeRoots += "=" + rootNames[kk];
                }
            }
        }
        logging::logTo(m_gds,
                       m_gds,
                       PrintLevel::SUMMARY,
                       "IDA root return: time={}, active_root_indices=[{}]",
                       static_cast<double>(solveTime),
                       activeRoots);
    }
}

void IdaInterface::setConstraints()
{
    if (m_gds->hasConstraints()) {
        N_VConst(ZERO, consData);
        m_gds->getConstraints(NVECTOR_DATA(use_omp, consData), mode);
        IDASetConstraints(solverMem, consData);
    }
}

void IdaInterface::loadMaskElements()
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

// IDA C Functions
int idaFunc(sunrealtype time, N_Vector state, N_Vector dstateDt, N_Vector resid, void* userData)
{
    auto sd = reinterpret_cast<IdaInterface*>(userData);
    ++sd->funcCallCount;
    if (sd->flags[IDA_INTEGRATION_DIAGNOSTICS] && ((sd->funcCallCount % 1000) == 0)) {
        logging::logTo(sd->m_gds,
                       sd->m_gds,
                       PrintLevel::SUMMARY,
                       "IDA residual progress: evaluations={}, internal_time={}",
                       sd->funcCallCount,
                       static_cast<double>(time));
    }
    // printf("time=%f\n", time);
    int ret = sd->m_gds->residualFunction(time,
                                          NVECTOR_DATA(sd->use_omp, state),
                                          NVECTOR_DATA(sd->use_omp, dstateDt),
                                          NVECTOR_DATA(sd->use_omp, resid),
                                          sd->mode);
    if (sd->flags[USE_MASK_FLAG]) {
        auto lstate = NVECTOR_DATA(sd->use_omp, state);
        auto lresid = NVECTOR_DATA(sd->use_omp, resid);
        for (auto& v : sd->maskElements) {
            lresid[v] = 100.0 * (lstate[v] - sd->tempState[v]);
        }
    }
    if (sd->flags[FILE_CAPTURE_FLAG]) {
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
                        DERIVATIVE_INFORMATION,
                        sd->funcCallCount,
                        sd->mode.offsetIndex,
                        sd->svsize,
                        NVECTOR_DATA(sd->use_omp, dstateDt),
                        sd->stateFile);
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

int idaRootFunc(sunrealtype time,
                N_Vector state,
                N_Vector dstateDt,
                sunrealtype* gout,
                void* userData)
{
    auto sd = reinterpret_cast<IdaInterface*>(userData);
    sd->m_gds->rootFindingFunction(time,
                                   NVECTOR_DATA(sd->use_omp, state),
                                   NVECTOR_DATA(sd->use_omp, dstateDt),
                                   gout,
                                   sd->mode);

    return FUNCTION_EXECUTION_SUCCESS;
}

int idaJac(sunrealtype time,
           sunrealtype cj,
           N_Vector state,
           N_Vector dstateDt,
           N_Vector /*resid*/,
           SUNMatrix j,
           void* userData,
           N_Vector tmp1,
           N_Vector tmp2,
           N_Vector /*tmp3*/)
{
    return sundialsJac(time, cj, state, dstateDt, j, userData, tmp1, tmp2);
}

}  // namespace griddyn::solvers
