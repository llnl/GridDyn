/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SolverInterface.h"

#include "../GridDynSimulation.h"
#include "BasicOdeSolver.h"
#include "BasicSolver.h"
#include "IdaInterface.h"
#include "KinsolInterface.h"
#include "core/CoreExceptions.h"
#include "core/FactoryTemplates.hpp"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringConversion.h"
#include <algorithm>
#include <charconv>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    std::vector<int> parseMaskElements(std::string_view text)
    {
        std::vector<int> values;
        std::size_t start = 0;
        while (start < text.size()) {
            const auto end = text.find_first_of(",;", start);
            const auto tokenEnd = (end == std::string_view::npos) ? text.size() : end;
            auto token = text.substr(start, tokenEnd - start);

            const auto first = token.find_first_not_of(" \t");
            if (first != std::string_view::npos) {
                const auto last = token.find_last_not_of(" \t");
                token = token.substr(first, last - first + 1);

                int parsed = 0;
                const auto result =
                    std::from_chars(token.data(), token.data() + token.size(), parsed);
                if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) {
                    values.push_back(parsed);
                }
            }

            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return values;
    }
}  // namespace

static ChildClassFactoryArg<solvers::BasicSolver, SolverInterface, solvers::BasicSolver::Mode>
    gBasicFactoryG(stringVec{"basic", "gauss"}, solvers::BasicSolver::Mode::gauss);
static ChildClassFactoryArg<solvers::BasicSolver, SolverInterface, solvers::BasicSolver::Mode>
    gBasicFactoryGs(stringVec{"gs", "gauss-seidel"}, solvers::BasicSolver::Mode::gauss_seidel);
#ifdef GRIDDYN_ENABLE_CVODE
static ChildClassFactory<solvers::BasicOdeSolver, SolverInterface>
    gBasicOdeFactory(stringVec{"basicode", "euler"});
#else
// if cvode is not available this becomes the default differential solver
static ChildClassFactory<solvers::BasicOdeSolver, SolverInterface>
    gBasicOdeFactory(stringVec{"basicode", "dyndiff", "differential"});

#endif

SolverInterface::SolverInterface(const std::string& objName): HelperObject(objName) {}
SolverInterface::SolverInterface(GridDynSimulation* gds, const SolverMode& sMode):
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
        if (flags[INITIALIZED_FLAG]) {
            si->initialize(0.0);
        }
        // copy the state data
        const double* sd = stateData();
        double* statecopy = si->stateData();
        if ((sd != nullptr) && (statecopy != nullptr)) {
            std::copy(sd, sd + svsize, statecopy);
        }

        // copy the derivative data
        const double* deriv = derivData();
        double* derivcopy = si->derivData();
        if ((deriv != nullptr) && (derivcopy != nullptr)) {
            std::copy(deriv, deriv + svsize, derivcopy);
        }
        // copy the type data
        const double* td = typeData();
        double* tcopy = si->typeData();
        if ((td != nullptr) && (tcopy != nullptr)) {
            std::copy(td, td + svsize, tcopy);
        }
        si->jacFile = jacFile;
        si->stateFile = stateFile;
    }
}

double* SolverInterface::stateData() noexcept
{
    return nullptr;
}
double* SolverInterface::derivData() noexcept
{
    return nullptr;
}
double* SolverInterface::typeData() noexcept
{
    return nullptr;
}
const double* SolverInterface::stateData() const noexcept
{
    return nullptr;
}
const double* SolverInterface::derivData() const noexcept
{
    return nullptr;
}
const double* SolverInterface::typeData() const noexcept
{
    return nullptr;
}
void SolverInterface::allocate(count_t /*stateSize*/, count_t numRoots)
{
    rootsfound.resize(numRoots);
}
void SolverInterface::initialize(CoreTime t0)
{
    solveTime = t0;
}
void SolverInterface::sparseReInit(SparseReinitMode /*mode*/) {}
void SolverInterface::setConstraints() {}
int SolverInterface::calcIC(CoreTime /*t0*/,
                            CoreTime /*tstep0*/,
                            IcModes /*mode*/,
                            bool /*constraints*/)
{
    return -101;
}
void SolverInterface::getCurrentData() {}
void SolverInterface::getRoots() {}
void SolverInterface::setRootFinding(index_t /*numRoots*/) {}
void SolverInterface::setSimulationData(const SolverMode& sMode)
{
    mode = sMode;
}
void SolverInterface::setSimulationData(GridDynSimulation* gds, const SolverMode& sMode)
{
    mode = sMode;
    if (gds != nullptr) {
        m_gds = gds;
    }
}

void SolverInterface::setSimulationData(GridDynSimulation* gds)
{
    if (gds != nullptr) {
        m_gds = gds;
    }
}

double SolverInterface::get(std::string_view param) const
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
        return HelperObject::get(param);
    }
    return res;
}

void SolverInterface::set(std::string_view param, std::string_view val)
{
    using gmlc::utilities::convertToLowerCase;

    if ((param == "approx") || (param == "approximation")) {
        setApproximation(convertToLowerCase(val));
    } else if (param == "printlevel") {
        auto plevel = convertToLowerCase(val);
        if (plevel == "debug") {
            printLevel = SolverPrintLevel::DEBUG_PRINT;
        } else if ((plevel == "none") || (plevel == "trap")) {
            printLevel = SolverPrintLevel::ERROR_TRAP;
        } else if (plevel == "error") {
            printLevel = SolverPrintLevel::ERROR_LOG;
        } else {
            throw(InvalidParameterValue(plevel));
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
            throw(InvalidParameterValue(plevel));
        }
    } else if ((param == "pair") || (param == "pairedmode")) {
        if (m_gds != nullptr) {
            auto nsmode = m_gds->getSolverMode(std::string{val});
            mode.pairedOffsetIndex = nsmode.offsetIndex;
        }
    } else if (param == "mask") {
        maskElements = parseMaskElements(val);
    } else if (param == "mode") {
        setMultipleFlags(this, val);
    } else if ((param == "file") || (param == "logfile")) {
        solverLogFile = std::string{val};
    } else if (param == "jacfile") {
        jacFile = std::string{val};
    } else if (param == "statefile") {
        stateFile = std::string{val};
    } else if (param == "capturefile") {
        jacFile = std::string{val};
        stateFile = std::string{val};
    } else {
        HelperObject::set(param, val);
    }
}

void SolverInterface::set(std::string_view param, double val)
{
    if ((param == "pair") || (param == "pairedmode")) {
        mode.pairedOffsetIndex = static_cast<index_t>(val);
    } else if (param == "tolerance") {
        tolerance = val;
    } else if (param == "printlevel") {
        switch (static_cast<int>(val)) {
            case 0:
                printLevel = SolverPrintLevel::ERROR_TRAP;
                break;
            case 1:
                printLevel = SolverPrintLevel::ERROR_LOG;
                break;
            case 2:
                printLevel = SolverPrintLevel::DEBUG_PRINT;
                break;
            default:
                throw(InvalidParameterValue(param));
        }
    } else if (param == "solverprintlevel") {
        auto lv = static_cast<int>(val);
        if ((lv >= 0) && (lv <= 3)) {
            solverPrintLevel = lv;
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (param == "maskElement") {
        addMaskElement(static_cast<index_t>(val));
    } else if (param == "index") {
        mode.offsetIndex = static_cast<index_t>(val);
    } else if (param == "maxiterations") {
        max_iterations = static_cast<index_t>(val);
    } else {
        HelperObject::set(param, val);
    }
}

static const std::map<std::string_view, int, std::less<std::string_view>> SOLVER_FLAG_MAP{
    {"filecapture", FILE_CAPTURE_FLAG},
    {"directlogging", DIRECT_LOGGING_FLAG},
    {"solver_log", DIRECT_LOGGING_FLAG},
    {"dense", DENSE_FLAG},
    {"sparse", -DENSE_FLAG},
    {"parallel", PARALLEL_FLAG},
    {"serial", -PARALLEL_FLAG},
    {"mask", USE_MASK_FLAG},
    {"constantjacobian", CONSTANT_JACOBIAN_FLAG},
    {"omp", USE_OMP_FLAG},
    {"useomp", USE_OMP_FLAG},
    {"bdf", USE_BDF_FLAG},
    {"adams", -USE_BDF_FLAG},
    {"functional", -USE_NEWTON_FLAG},
    {"newton", USE_NEWTON_FLAG},
    {"print_resid", PRINT_RESIDUALS},
    {"print_residuals", PRINT_RESIDUALS},
    {"block_mode_only", BLOCK_MODE_ONLY}};

void SolverInterface::setFlag(std::string_view flag, bool val)
{
    const auto foundFlag = SOLVER_FLAG_MAP.find(flag);
    const int flgInd = (foundFlag != SOLVER_FLAG_MAP.end()) ? foundFlag->second : -60;
    if (flgInd > -32) {
        if (flgInd > 0) {
            flags.set(flgInd, val);
        } else {
            flags.set(-flgInd, !val);
        }
        return;
    }

    if (flag == "dc") {
        mode.approx.set(DC, val);
    } else if (flag == "ac") {
        mode.approx.set(DC, !val);
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
        printLevel = SolverPrintLevel::DEBUG_PRINT;
    } else if (flag == "trap") {
        printLevel = SolverPrintLevel::ERROR_TRAP;
    } else if (flag == "error") {
        printLevel = SolverPrintLevel::ERROR_LOG;
    } else {
        if (val) {
            setApproximation(flag);
        } else {
            throw(UnrecognizedParameter(flag));
        }
    }
}

void SolverInterface::setApproximation(std::string_view approx)
{
    if ((approx == "normal") || (approx == "none")) {
        setLinkApprox(mode, ApproxKeyMask::NONE);
    } else if ((approx == "simple") || (approx == "simplified")) {
        setLinkApprox(mode, ApproxKeyMask::SIMPLIFIED);
    } else if (approx == "small_angle") {
        setLinkApprox(mode, ApproxKeyMask::SM_ANGLE);
    } else if (approx == "small_angle_decoupled") {
        setLinkApprox(mode, ApproxKeyMask::SM_ANGLE_DECOUPLED);
    } else if (approx == "simplified_decoupled") {
        setLinkApprox(mode, ApproxKeyMask::SIMPLIFIED_DECOUPLED);
    } else if ((approx == "small_angle_simplified") || (approx == "simplified_small_angle")) {
        setLinkApprox(mode, ApproxKeyMask::SIMPLIFIED_SM_ANGLE);
    } else if ((approx == "r") || (approx == "small_r")) {
        setLinkApprox(mode, LINEAR, false);
        setLinkApprox(mode, SMALL_R);
    } else if (approx == "angle") {
        setLinkApprox(mode, LINEAR, false);
        setLinkApprox(mode, SMALL_ANGLE);
    } else if (approx == "coupling") {
        setLinkApprox(mode, LINEAR, false);
        setLinkApprox(mode, DECOUPLED);
    } else if (approx == "decoupled") {
        setLinkApprox(mode, ApproxKeyMask::DECOUPLED);
    } else if (approx == "linear") {
        setLinkApprox(mode, ApproxKeyMask::LINEAR);
    } else if ((approx == "fast_decoupled") || (approx == "fdpf")) {
        setLinkApprox(mode, ApproxKeyMask::FAST_DECOUPLED);
    } else {
        throw(InvalidParameterValue(approx));
    }
}

bool SolverInterface::getFlag(std::string_view flag) const
{
    const auto foundFlag = SOLVER_FLAG_MAP.find(flag);
    const int flgInd = (foundFlag != SOLVER_FLAG_MAP.end()) ? foundFlag->second : -60;
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
    auto* state = stateData();
    auto* dstate = derivData();
    auto* type = typeData();
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

void SolverInterface::checkFlag(void* flagvalue,
                                std::string_view funcname,
                                int opt,
                                bool printError) const
{
    // TODO(phlpt): Delete either this or optimizerInterface::checkFlag.
    // Check if SUNDIALS function returned nullptr pointer - no memory allocated
    if (opt == 0 && flagvalue == nullptr) {
        if (printError) {
            logging::logTo(
                m_gds, m_gds, PrintLevel::ERROR, "{} failed - returned nullptr pointer", funcname);
        }
        throw(std::bad_alloc());
    }
    if (opt == 1) {
        // Check if flag < 0
        auto* errflag = reinterpret_cast<int*>(flagvalue);
        if (*errflag < 0) {
            if (printError) {
                logging::logTo(m_gds,
                               m_gds,
                               PrintLevel::ERROR,
                               "{} failed with flag = {}",
                               funcname,
                               *errflag);
            }
            throw(SolverException(*errflag));
        }
    }
    // TODO(phlpt): Handle the missing opt == 2 / nullptr case if needed.
}

int SolverInterface::solve(CoreTime /*tStop*/, CoreTime& /*tReturn*/, StepMode /* stepMode */)
{
    return -101;
}
void SolverInterface::logSolverStats(PrintLevel /*logLevel*/, bool /*iconly*/) const {}
void SolverInterface::logErrorWeights(PrintLevel /*logLevel*/) const {}
void SolverInterface::logMessage(int errorCode, std::string_view message)
{
    if ((errorCode > 0) && (printLevel == SolverPrintLevel::DEBUG_PRINT)) {
        logging::logTo(m_gds, m_gds, PrintLevel::DEBUG, message);
    }
    if (errorCode != 0) {
        lastErrorCode = errorCode;
        lastErrorString = message;
        if (printLevel == SolverPrintLevel::ERROR_LOG) {
            logging::logTo(m_gds, m_gds, PrintLevel::WARNING, message);
        }
    }
}

void SolverInterface::setMaxNonZeros(count_t nonZeroCount)
{
    nnz = nonZeroCount;
}

// TODO(phlpt): Change this so the defaults can be something other than sundials solvers.
std::unique_ptr<SolverInterface> makeSolver(GridDynSimulation* gds, const SolverMode& sMode)
{
    std::unique_ptr<SolverInterface> sd = nullptr;
    if (isLocal(sMode)) {
        sd = std::make_unique<SolverInterface>(gds, sMode);
    } else if ((isAlgebraicOnly(sMode)) || (!isDynamic(sMode))) {
        sd = std::make_unique<solvers::KinsolInterface>(gds, sMode);
        if (sMode.offsetIndex == POWER_FLOW) {
            sd->setName("powerflow");
        } else if (sMode.offsetIndex == DYNAMIC_ALGEBRAIC) {
            sd->setName("algebraic");
        }
    } else if (isDAE(sMode)) {
        sd = std::make_unique<solvers::IdaInterface>(gds, sMode);
        if (sMode.offsetIndex == DAE) {
            sd->setName("dynamic");
        }
    } else if (isDifferentialOnly(sMode)) {
        sd = CoreClassFactory<SolverInterface>::instance()->createObject("differential");
        sd->setSimulationData(gds, sMode);
        if (sMode.offsetIndex == DYNAMIC_DIFFERENTIAL) {
            sd->setName("differential");
        }
    }

    return sd;
}

std::unique_ptr<SolverInterface> makeSolver(std::string_view type, const std::string& name)
{
    if (name.empty()) {
        return CoreClassFactory<SolverInterface>::instance()->createObject(type);
    }

    return CoreClassFactory<SolverInterface>::instance()->createObject(type, name);
}

}  // namespace griddyn
