/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "solverInterface.h"

#include "../gridDynSimulation.h"
#include "basicOdeSolver.h"
#include "basicSolver.h"
#include "core/coreExceptions.h"
#include "core/factoryTemplates.hpp"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "idaInterface.h"
#include "kinsolInterface.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
namespace solvers {
    static childClassFactoryArg<basicSolver, SolverInterface, basicSolver::mode_t>
        basicFactoryG(stringVec{"basic", "gauss"}, basicSolver::mode_t::gauss);
    static childClassFactoryArg<basicSolver, SolverInterface, basicSolver::mode_t>
        basicFactoryGS(stringVec{"gs", "gauss-seidel"}, basicSolver::mode_t::gauss_seidel);
#ifdef GRIDYN_ENABLE_CVODE
    static childClassFactory<basicOdeSolver, SolverInterface>
        basicOdeFactory(stringVec{"basicode", "euler"});
#else
    // if cvode is not available this becomes the default differential solver
    static childClassFactory<basicOdeSolver, SolverInterface>
        basicOdeFactory(stringVec{"basicode", "dyndiff", "differential"});

#endif

}  // namespace solvers
SolverInterface::SolverInterface(const std::string& objName): helperObject(objName) {}
SolverInterface::SolverInterface(gridDynSimulation* gds, const solverMode& sMode):
    mode(sMode), m_gds(gds)
{
}

std::unique_ptr<SolverInterface> SolverInterface::clone(bool fullCopy) const
{
    auto si = std::make_unique<SolverInterface>();
    SolverInterface::cloneTo(si.get(), fullCopy);
    return si;
}

void SolverInterface::cloneTo(SolverInterface* si, bool fullCopy) const
{
    si->setName(getName());
    si->solverLogFile = solverLogFile;
    si->printLevel = printLevel;
    si->max_iterations = max_iterations;
    auto ind = si->mode.offsetIndex;
    si->mode = mode;
    if (ind != kNullLocation) {
        si->mode.offsetIndex = ind;
    }
    si->tolerance = tolerance;
    si->flags = flags;
    si->solverPrintLevel = solverPrintLevel;
    if (fullCopy) {
        si->maskElements = maskElements;
        si->m_gds = m_gds;
        si->allocate(svsize, rootCount);
        if (flags[initialized_flag]) {
            si->initialize(0.0);
        }
        // copy the state data
        const double* sd = state_data();
        double* statecopy = si->state_data();
        if ((sd != nullptr) && (statecopy != nullptr)) {
            std::copy(sd, sd + svsize, statecopy);
        }

        // copy the derivative data
        const double* deriv = deriv_data();
        double* derivcopy = si->deriv_data();
        if ((deriv != nullptr) && (derivcopy != nullptr)) {
            std::copy(deriv, deriv + svsize, derivcopy);
        }
        // copy the type data
        const double* td = type_data();
        double* tcopy = si->type_data();
        if ((td != nullptr) && (tcopy != nullptr)) {
            std::copy(td, td + svsize, tcopy);
        }
        si->jacFile = jacFile;
        si->stateFile = stateFile;
    }
}

double* SolverInterface::state_data() noexcept
{
    return nullptr;
}
double* SolverInterface::deriv_data() noexcept
{
    return nullptr;
}
double* SolverInterface::type_data() noexcept
{
    return nullptr;
}
const double* SolverInterface::state_data() const noexcept
{
    return nullptr;
}
const double* SolverInterface::deriv_data() const noexcept
{
    return nullptr;
}
const double* SolverInterface::type_data() const noexcept
{
    return nullptr;
}
void SolverInterface::allocate(count_t /*stateSize*/, count_t numRoots)
{
    rootsfound.resize(numRoots);
}
void SolverInterface::initialize(coreTime t0)
{
    solveTime = t0;
}
void SolverInterface::sparseReInit(sparse_reinit_modes /*mode*/) {}
void SolverInterface::setConstraints() {}
int SolverInterface::calcIC(coreTime /*t0*/,
                            coreTime /*tstep0*/,
                            ic_modes /*mode*/,
                            bool /*constraints*/)
{
    return -101;
}
void SolverInterface::getCurrentData() {}
void SolverInterface::getRoots() {}
void SolverInterface::setRootFinding(index_t /*numRoots*/) {}
void SolverInterface::setSimulationData(const solverMode& sMode)
{
    mode = sMode;
}
void SolverInterface::setSimulationData(gridDynSimulation* gds, const solverMode& sMode)
{
    mode = sMode;
    if (gds != nullptr) {
        m_gds = gds;
    }
}

void SolverInterface::setSimulationData(gridDynSimulation* gds)
{
    if (gds != nullptr) {
        m_gds = gds;
    }
}

double SolverInterface::get(const std::string& param) const
{
    double res;
    if (param == "solvercount") {
        res = static_cast<double>(solverCallCount);
    } else if (param == "jaccallcount") {
        res = static_cast<double>(jacCallCount);
    } else if ((param == "rootcallcount") || (param == "roottestcount")) {
        res = static_cast<double>(rootCallCount);
    } else if (param == "funccallcount") {
        res = static_cast<double>(funcCallCount);
    } else if (param == "approx") {
        res = static_cast<double>(getLinkApprox(mode));
    } else if (param == "printlevel") {
        res = static_cast<double>(printLevel);
    } else if (param == "tolerance") {
        res = tolerance;
    } else {
        return helperObject::get(param);
    }
    return res;
}

void SolverInterface::set(const std::string& param, const std::string& val)
{
    using gmlc::utilities::convertToLowerCase;

    if ((param == "approx") || (param == "approximation")) {
        setApproximation(convertToLowerCase(val));
    } else if (param == "printlevel") {
        auto plevel = convertToLowerCase(val);
        if (plevel == "debug") {
            printLevel = solver_print_level::s_debug_print;
        } else if ((plevel == "none") || (plevel == "trap")) {
            printLevel = solver_print_level::s_error_trap;
        } else if (plevel == "error") {
            printLevel = solver_print_level::s_error_log;
        } else {
            throw(invalidParameterValue(plevel));
        }
    } else if (param == "solverprintlevel") {
        auto plevel = convertToLowerCase(val);
        if (plevel == "trace") {
            solverPrintLevel = 3;
        } else if (plevel == "debug") {
            solverPrintLevel = 2;
        } else if (plevel == "log") {
            solverPrintLevel = 1;
        } else if (plevel == "none") {
            solverPrintLevel = 0;
        } else {
            throw(invalidParameterValue(plevel));
        }
    } else if ((param == "pair") || (param == "pairedmode")) {
        if (m_gds != nullptr) {
            auto nsmode = m_gds->getSolverMode(val);
            mode.pairedOffsetIndex = nsmode.offsetIndex;
        }
    } else if (param == "mask") {
        auto sep = gmlc::utilities::str2vector<int>(val, -1, ",;");
        maskElements.resize(sep.size());
        for (size_t kk = 0; kk < sep.size(); ++kk) {
            maskElements[kk] = sep[kk];
        }
    } else if (param == "mode") {
        setMultipleFlags(this, val);
    } else if ((param == "file") || (param == "logfile")) {
        solverLogFile = val;
    } else if (param == "jacfile") {
        jacFile = val;
    } else if (param == "statefile") {
        stateFile = val;
    } else if (param == "capturefile") {
        jacFile = val;
        stateFile = val;
    } else {
        helperObject::set(param, val);
    }
}

void SolverInterface::set(const std::string& param, double val)
{
    if ((param == "pair") || (param == "pairedmode")) {
        mode.pairedOffsetIndex = static_cast<index_t>(val);
    } else if (param == "tolerance") {
        tolerance = val;
    } else if (param == "printlevel") {
        switch (static_cast<int>(val)) {
            case 0:
                printLevel = solver_print_level::s_error_trap;
                break;
            case 1:
                printLevel = solver_print_level::s_error_log;
                break;
            case 2:
                printLevel = solver_print_level::s_debug_print;
                break;
            default:
                throw(invalidParameterValue(param));
        }
    } else if (param == "solverprintlevel") {
        auto lv = static_cast<int>(val);
        if ((lv >= 0) && (lv <= 3)) {
            solverPrintLevel = lv;
        } else {
            throw(invalidParameterValue(param));
        }
    } else if (param == "maskElement") {
        addMaskElement(static_cast<index_t>(val));
    } else if (param == "index") {
        mode.offsetIndex = static_cast<index_t>(val);
    } else if (param == "maxiterations") {
        max_iterations = static_cast<index_t>(val);
    } else {
        helperObject::set(param, val);
    }
}

static const std::map<std::string, int> solverFlagMap{{"filecapture", fileCapture_flag},
                                                      {"directlogging", directLogging_flag},
                                                      {"solver_log", directLogging_flag},
                                                      {"dense", dense_flag},
                                                      {"sparse", -dense_flag},
                                                      {"parallel", parallel_flag},
                                                      {"serial", -parallel_flag},
                                                      {"mask", useMask_flag},
                                                      {"constantjacobian", constantJacobian_flag},
                                                      {"omp", use_omp_flag},
                                                      {"useomp", use_omp_flag},
                                                      {"bdf", use_bdf_flag},
                                                      {"adams", -use_bdf_flag},
                                                      {"functional", -use_newton_flag},
                                                      {"newton", use_newton_flag},
                                                      {"print_resid", print_residuals},
                                                      {"print_residuals", print_residuals},
                                                      {"block_mode_only", block_mode_only}};

void SolverInterface::setFlag(const std::string& flag, bool val)
{
    auto flgInd = mapFind(solverFlagMap, flag, -60);
    if (flgInd > -32) {
        if (flgInd > 0) {
            flags.set(flgInd, val);
        } else {
            flags.set(-flgInd, !val);
        }
        return;
    }

    if (flag == "dc") {
        mode.approx.set(dc, val);
    } else if (flag == "ac") {
        mode.approx.set(dc, !val);
    } else if (flag == "dynamic") {
        mode.dynamic = val;
    } else if (flag == "powerflow") {
        mode.dynamic = false;
        mode.differential = false;
        mode.dynamic = false;
        mode.algebraic = true;
    } else if (flag == "differential") {
        if (val) {
            mode.differential = true;
            mode.dynamic = true;
        } else {
            mode.differential = false;
        }
    } else if (flag == "algebraic") {
        mode.algebraic = val;
    } else if (flag == "local") {
        mode.local = val;
    } else if (flag == "dae") {
        if (val) {
            mode.differential = true;
            mode.dynamic = true;
            mode.algebraic = true;
        } else {
            // PT:: what does dae false mean?  probably not do anything
        }
    } else if (flag == "extended") {
        mode.extended_state = val;
    } else if (flag == "primary") {
        mode.extended_state = !val;
    } else if (flag == "debug") {
        printLevel = solver_print_level::s_debug_print;
    } else if (flag == "trap") {
        printLevel = solver_print_level::s_error_trap;
    } else if (flag == "error") {
        printLevel = solver_print_level::s_error_log;
    } else {
        if (val) {
            setApproximation(flag);
        } else {
            throw(unrecognizedParameter(flag));
        }
    }
}

void SolverInterface::setApproximation(const std::string& approx)
{
    if ((approx == "normal") || (approx == "none")) {
        setLinkApprox(mode, approxKeyMask::none);
    } else if ((approx == "simple") || (approx == "simplified")) {
        setLinkApprox(mode, approxKeyMask::simplified);
    } else if (approx == "small_angle") {
        setLinkApprox(mode, approxKeyMask::sm_angle);
    } else if (approx == "small_angle_decoupled") {
        setLinkApprox(mode, approxKeyMask::sm_angle_decoupled);
    } else if (approx == "simplified_decoupled") {
        setLinkApprox(mode, approxKeyMask::simplified_decoupled);
    } else if ((approx == "small_angle_simplified") || (approx == "simplified_small_angle")) {
        setLinkApprox(mode, approxKeyMask::simplified_sm_angle);
    } else if ((approx == "r") || (approx == "small_r")) {
        setLinkApprox(mode, linear, false);
        setLinkApprox(mode, small_r);
    } else if (approx == "angle") {
        setLinkApprox(mode, linear, false);
        setLinkApprox(mode, small_angle);
    } else if (approx == "coupling") {
        setLinkApprox(mode, linear, false);
        setLinkApprox(mode, decoupled);
    } else if (approx == "decoupled") {
        setLinkApprox(mode, approxKeyMask::decoupled);
    } else if (approx == "linear") {
        setLinkApprox(mode, approxKeyMask::linear);
    } else if ((approx == "fast_decoupled") || (approx == "fdpf")) {
        setLinkApprox(mode, approxKeyMask::fast_decoupled);
    } else {
        throw(invalidParameterValue(approx));
    }
}

bool SolverInterface::getFlag(const std::string& flag) const
{
    auto flgInd = mapFind(solverFlagMap, flag, -60);
    if (flgInd > -32) {
        if (flgInd > 0) {
            return flags[flgInd];
        }
        return !flags[-flgInd];
    }
    return false;
}
void SolverInterface::setMaskElements(std::vector<index_t> msk)
{
    maskElements = std::move(msk);
}
void SolverInterface::addMaskElement(index_t newMaskElement)
{
    maskElements.push_back(newMaskElement);
}
void SolverInterface::addMaskElements(const std::vector<index_t>& newMsk)
{
    for (auto& nme : newMsk) {
        maskElements.push_back(nme);
    }
}

void SolverInterface::printStates(bool getNames)
{
    auto* state = state_data();
    auto* dstate = deriv_data();
    auto* type = type_data();
    stringVec stName;
    if (getNames) {
        m_gds->getStateName(stName, mode);
    }
    for (index_t ii = 0; ii < svsize; ++ii) {
        if (type != nullptr) {
            std::cout << ((type[ii] == 1) ? 'D' : 'A') << '-';
        }
        if (getNames) {
            std::cout << '[' << ii << "]:" << stName[ii] << '=';
        } else {
            std::cout << "state[" << ii << "]=";
        }
        std::cout << state[ii];
        if (dstate != nullptr) {
            std::cout << "               ds/dt=" << dstate[ii];
        }
        std::cout << '\n';
    }
}

void SolverInterface::check_flag(void* flagvalue,
                                 const std::string& funcname,
                                 int opt,
                                 bool printError) const
{
    // TODO(phlpt): Delete either this or optimizerInterface::check_flag.
    // Check if SUNDIALS function returned nullptr pointer - no memory allocated
    if (opt == 0 && flagvalue == nullptr) {
        if (printError) {
            m_gds->log(m_gds, print_level::error, funcname + " failed - returned nullptr pointer");
        }
        throw(std::bad_alloc());
    }
    if (opt == 1) {
        // Check if flag < 0
        auto* errflag = reinterpret_cast<int*>(flagvalue);
        if (*errflag < 0) {
            if (printError) {
                m_gds->log(m_gds,
                           print_level::error,
                           funcname + " failed with flag = " + std::to_string(*errflag));
            }
            throw(solverException(*errflag));
        }
    }
    // TODO(phlpt): Handle the missing opt == 2 / nullptr case if needed.
}

int SolverInterface::solve(coreTime /*tStop*/, coreTime& /*tReturn*/, step_mode /* stepMode */)
{
    return -101;
}
void SolverInterface::logSolverStats(print_level /*logLevel*/, bool /*iconly*/) const {}
void SolverInterface::logErrorWeights(print_level /*logLevel*/) const {}
void SolverInterface::logMessage(int errorCode, const std::string& message)
{
    if ((errorCode > 0) && (printLevel == solver_print_level::s_debug_print)) {
        m_gds->log(m_gds, print_level::debug, message);
    }
    if (errorCode != 0) {
        lastErrorCode = errorCode;
        lastErrorString = message;
        if (printLevel == solver_print_level::s_error_log) {
            m_gds->log(m_gds, print_level::warning, message);
        }
    }
}

void SolverInterface::setMaxNonZeros(count_t nonZeroCount)
{
    nnz = nonZeroCount;
}

// TODO(phlpt): Change this so the defaults can be something other than sundials solvers.
std::unique_ptr<SolverInterface> makeSolver(gridDynSimulation* gds, const solverMode& sMode)
{
    std::unique_ptr<SolverInterface> sd = nullptr;
    if (isLocal(sMode)) {
        sd = std::make_unique<SolverInterface>(gds, sMode);
    } else if ((isAlgebraicOnly(sMode)) || (!isDynamic(sMode))) {
        sd = std::make_unique<solvers::kinsolInterface>(gds, sMode);
        if (sMode.offsetIndex == power_flow) {
            sd->setName("powerflow");
        } else if (sMode.offsetIndex == dynamic_algebraic) {
            sd->setName("algebraic");
        }
    } else if (isDAE(sMode)) {
        sd = std::make_unique<solvers::idaInterface>(gds, sMode);
        if (sMode.offsetIndex == dae) {
            sd->setName("dynamic");
        }
    } else if (isDifferentialOnly(sMode)) {
        sd = coreClassFactory<SolverInterface>::instance()->createObject("differential");
        sd->setSimulationData(gds, sMode);
        if (sMode.offsetIndex == dynamic_differential) {
            sd->setName("differential");
        }
    }

    return sd;
}

std::unique_ptr<SolverInterface> makeSolver(const std::string& type, const std::string& name)
{
    if (name.empty()) {
        return coreClassFactory<SolverInterface>::instance()->createObject(type);
    }

    return coreClassFactory<SolverInterface>::instance()->createObject(type, name);
}

}  // namespace griddyn
