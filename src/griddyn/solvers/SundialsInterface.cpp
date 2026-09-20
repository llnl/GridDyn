/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SundialsInterface.h"

#include "IdaInterface.h"
#include "KinsolInterface.h"
#include "griddyn/griddyn-config.h"
#ifdef GRIDDYN_ENABLE_CVODE
#    include "CvodeInterface.h"
#endif
#ifdef GRIDDYN_ENABLE_ARKODE
#    include "ArkodeInterface.h"
#endif

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#include "../GridDynSimulation.h"
#include "../simulation/Diagnostics.h"
#include "../simulation/GridDynSimulationFileOps.h"
#include "SundialsMatrixData.h"
#include "core/FactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixDataFilter.hpp"
#include "utilities/matrixCreation.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::solvers {
void ensureSundialsFactories()
{
    static ChildClassFactory<KinsolInterface, SolverInterface> kinFactory(
        stringVec{"kinsol", "algebraic"});
    static ChildClassFactory<IdaInterface, SolverInterface> idaFactory(
        stringVec{"ida", "dae", "dynamic"});
#ifdef GRIDDYN_ENABLE_CVODE
    static ChildClassFactory<CvodeInterface, SolverInterface> cvodeFactory(
        stringVec{"cvode", "dyndiff", "differential"});
#endif

#ifdef GRIDDYN_ENABLE_ARKODE
    static ChildClassFactory<ArkodeInterface, SolverInterface> arkodeFactory(stringVec{"arkode"});
#endif
}

SundialsInterface::SundialsInterface(const std::string& objName): SolverInterface(objName)
{
    ensureSundialsFactories();
    tolerance = 1e-8;
    int retval = SUNContext_Create(SUN_COMM_NULL, &sunctx);
    checkFlag(&retval, "SUNContext_Create", 1);
    registerErrorHandler();
}
SundialsInterface::SundialsInterface(GridDynSimulation* gds, const SolverMode& sMode):
    SolverInterface(gds, sMode)
{
    ensureSundialsFactories();
    tolerance = 1e-8;
    int retval = SUNContext_Create(SUN_COMM_NULL, &sunctx);
    checkFlag(&retval, "SUNContext_Create", 1);
    registerErrorHandler();
}

SundialsInterface::~SundialsInterface()
{
    // clear variables for IDA to use
    if (state != nullptr) {
        NVECTOR_DESTROY(use_omp, state);
    }
    if (dstate_dt != nullptr) {
        NVECTOR_DESTROY(use_omp, dstate_dt);
    }
    if (abstols != nullptr) {
        NVECTOR_DESTROY(use_omp, abstols);
    }
    if (consData != nullptr) {
        NVECTOR_DESTROY(use_omp, consData);
    }
    if (scale != nullptr) {
        NVECTOR_DESTROY(use_omp, scale);
    }
    if (types != nullptr) {
        NVECTOR_DESTROY(use_omp, types);
    }
    if (m_sundialsInfoFile != nullptr) {
        static_cast<void>(fclose(m_sundialsInfoFile));
    }
    freeLinearSolver();
    if (sunctx != nullptr) {
        SUNContext_Free(&sunctx);
    }
}

std::unique_ptr<SolverInterface> SundialsInterface::clone(bool fullCopy) const
{
    std::unique_ptr<SolverInterface> si = std::make_unique<SundialsInterface>();
    SundialsInterface::cloneTo(si.get(), fullCopy);
    return si;
}

void SundialsInterface::cloneTo(SolverInterface* si, bool fullCopy) const
{
    SolverInterface::cloneTo(si, fullCopy);
    auto ai = dynamic_cast<SundialsInterface*>(si);
    if (ai == nullptr) {
        return;
    }
    ai->maxNNZ = maxNNZ;
    if ((fullCopy) && (flags[ALLOCATED_FLAG])) {
        auto tols = nvecdata(use_omp, abstols);
        std::copy(tols, tols + svsize, nvecdata(use_omp, ai->abstols));
        auto cons = nvecdata(use_omp, consData);
        std::copy(cons, cons + svsize, nvecdata(use_omp, ai->consData));
        auto sc = nvecdata(use_omp, scale);
        std::copy(sc, sc + svsize, nvecdata(use_omp, ai->scale));
    }
}

void SundialsInterface::allocate(count_t stateCount, count_t /*numRoots*/)
{
    // load the vectors
    if (stateCount == svsize) {
        return;
    }

    [[maybe_unused]] bool prevOmp = use_omp;  // looks unused if OPENMP is not available
    use_omp = flags[USE_OMP_FLAG];
    flags.reset(INITIALIZED_FLAG);
    freeLinearSolver();
    if (state != nullptr) {
        NVECTOR_DESTROY(prevOmp, state);
    }
    state = NVECTOR_NEW(use_omp, stateCount);
    checkFlag(state, "NVECTOR_NEW", 0);

    if (hasDifferential(mode)) {
        if (dstate_dt != nullptr) {
            NVECTOR_DESTROY(prevOmp, dstate_dt);
        }
        dstate_dt = NVECTOR_NEW(use_omp, stateCount);
        checkFlag(dstate_dt, "NVECTOR_NEW", 0);

        N_VConst(ZERO, dstate_dt);
    }
    if (abstols != nullptr) {
        NVECTOR_DESTROY(prevOmp, abstols);
    }
    abstols = NVECTOR_NEW(use_omp, stateCount);
    checkFlag(abstols, "NVECTOR_NEW", 0);

    if (consData != nullptr) {
        NVECTOR_DESTROY(prevOmp, consData);
    }
    consData = NVECTOR_NEW(use_omp, stateCount);
    checkFlag(consData, "NVECTOR_NEW", 0);

    if (scale != nullptr) {
        NVECTOR_DESTROY(prevOmp, scale);
    }
    scale = NVECTOR_NEW(use_omp, stateCount);
    checkFlag(scale, "NVECTOR_NEW", 0);

    N_VConst(ONE, scale);

    if (isDAE(mode)) {
        if (types != nullptr) {
            NVECTOR_DESTROY(prevOmp, types);
        }
        types = NVECTOR_NEW(use_omp, stateCount);
        checkFlag(types, "NVECTOR_NEW", 0);

        N_VConst(ONE, types);
    }

    svsize = stateCount;

    flags.set(ALLOCATED_FLAG);
}

void SundialsInterface::setMaxNonZeros(count_t nonZeroCount)
{
    maxNNZ = nonZeroCount;
    nnz = nonZeroCount;
    sparsePattern.clear();
}

double* SundialsInterface::stateData() noexcept
{
    return nvecdata(use_omp, state);
}
double* SundialsInterface::derivData() noexcept
{
    return nvecdata(use_omp, dstate_dt);
}

const double* SundialsInterface::stateData() const noexcept
{
    return nvecdata(use_omp, state);
}

const double* SundialsInterface::derivData() const noexcept
{
    return nvecdata(use_omp, dstate_dt);
}
// output solver stats

double* SundialsInterface::typeData() noexcept
{
    return nvecdata(use_omp, types);
}
const double* SundialsInterface::typeData() const noexcept
{
    return nvecdata(use_omp, types);
}

double SundialsInterface::get(std::string_view param) const
{
    if (param == "maxnnz") {
        return static_cast<double>(maxNNZ);
    } else if (param == "perftotal") {
        return performanceSolveTime;
    } else if (param == "perfresiduals") {
        return static_cast<double>(performanceResidualCalls);
    } else if (param == "perfresidualtime") {
        return performanceResidualTime;
    } else if (param == "perfjacobians") {
        return static_cast<double>(performanceJacobianCalls);
    } else if (param == "perfjacobiantime") {
        return performanceJacobianTime;
    } else if (param == "perfmodeljacobiantime") {
        return performanceModelJacobianTime;
    } else if (param == "perfrhs") {
        return static_cast<double>(performanceRhsCalls);
    } else if (param == "perfrhstime") {
        return performanceRhsTime;
    } else if (param == "perfalgebraic") {
        return static_cast<double>(performanceAlgebraicCalls);
    } else if (param == "perfalgebraictime") {
        return performanceAlgebraicTime;
    } else if (param == "perfderivative") {
        return static_cast<double>(performanceDerivativeCalls);
    } else if (param == "perfderivativetime") {
        return performanceDerivativeTime;
    }
    return SolverInterface::get(param);
}

void SundialsInterface::resetPerformanceStats() noexcept
{
    performanceResidualCalls = 0;
    performanceJacobianCalls = 0;
    performanceRhsCalls = 0;
    performanceAlgebraicCalls = 0;
    performanceDerivativeCalls = 0;
    performanceSolveTime = 0.0;
    performanceResidualTime = 0.0;
    performanceJacobianTime = 0.0;
    performanceModelJacobianTime = 0.0;
    performanceRhsTime = 0.0;
    performanceAlgebraicTime = 0.0;
    performanceDerivativeTime = 0.0;
}

void SundialsInterface::registerErrorHandler()
{
    if (sunctx == nullptr) {
        return;
    }
    int retval = SUNContext_PushErrHandler(sunctx, sundialsErrorHandlerFunc, this);
    checkFlag(&retval, "SUNContext_PushErrHandler", 1);
}

void SundialsInterface::freeLinearSolver()
{
    sparsePattern.clear();
    if (LS != nullptr) {
        SUNLinSolFree(LS);
        LS = nullptr;
    }
    if (J != nullptr) {
        SUNMatDestroy(J);
        J = nullptr;
    }
}

void SundialsInterface::kluReInit(SparseReinitMode sparseReInitModes, bool resetJacobian)
{
#ifdef GRIDDYN_ENABLE_KLU
    if (flags[DENSE_FLAG]) {
        return;
    }
    switch (sparseReInitModes) {
        case SparseReinitMode::REFACTOR: {
            int retval = SUNLinSol_KLUReInit(LS, J, maxNNZ, SUNKLU_REINIT_PARTIAL);
            checkFlag(&retval, "SUNLinSol_KLUReInit", 1);
        } break;
        case SparseReinitMode::RESIZE:
            /*there is a major bug in sundials with KLU on resize*/
            {
                if (maxNNZ > SM_NNZ_S(J)) {
                    SUNMatDestroy(J);
                    J = SUNSparseMatrix(svsize, svsize, maxNNZ, CSR_MAT, sunctx);
                    int retval = SUNLinSol_KLUReInit(LS, J, maxNNZ, SUNKLU_REINIT_PARTIAL);
                    checkFlag(&retval, "SUNLinSol_KLUReInit", 1);
                } else {
                    int retval = SUNLinSol_KLUReInit(LS, J, maxNNZ, SUNKLU_REINIT_PARTIAL);
                    checkFlag(&retval, "SUNLinSol_KLUReInit", 1);
                }
            }
            break;
    }
    if (resetJacobian) {
        jacCallCount = 0;
    }
#endif
}

bool isSUNMatrixSetup(SUNMatrix j)
{
    int id = SUNMatGetID(j);
    if (id == SUNMATRIX_SPARSE) {
        auto m = SM_CONTENT_S(j);
        if ((m->indexptrs[0] != 0) || (m->indexptrs[0] > m->NNZ)) {
            return false;
        }
        if ((m->indexptrs[m->N] <= 0) || (m->indexptrs[m->N] > m->NNZ)) {
            return false;
        }
    }
    return true;
}

namespace {
using SparsePattern = std::vector<sunindextype>;

SparsePattern sparsePatternFromMatrix(SUNMatrix j, count_t stateCount)
{
    auto* matrix = SM_CONTENT_S(j);
    const auto used = matrix->indexptrs[stateCount];
    SparsePattern pattern(static_cast<size_t>(stateCount) + 1 + used);
    std::copy_n(matrix->indexptrs, static_cast<size_t>(stateCount) + 1, pattern.data());
    std::copy_n(matrix->indexvals,
                static_cast<size_t>(used),
                pattern.data() + static_cast<size_t>(stateCount) + 1);
    return pattern;
}

SparsePattern sparsePatternFromData(MatrixData<double>& matrixData, count_t stateCount)
{
    matrixData.compact();
    std::vector<std::pair<sunindextype, sunindextype>> entries;
    entries.reserve(matrixData.size());
    matrixData.start();
    while (matrixData.moreData()) {
        const auto element = matrixData.next();
        if ((element.row < 0) || (element.row >= stateCount) || (element.col < 0) ||
            (element.col >= stateCount)) {
            continue;
        }
        entries.emplace_back(static_cast<sunindextype>(element.row),
                             static_cast<sunindextype>(element.col));
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

    SparsePattern pattern(static_cast<size_t>(stateCount) + 1, 0);
    pattern.reserve(static_cast<size_t>(stateCount) + 1 + entries.size());
    size_t entryIndex = 0;
    for (sunindextype row = 0; row < stateCount; ++row) {
        while ((entryIndex < entries.size()) && (entries[entryIndex].first == row)) {
            ++entryIndex;
        }
        pattern[row + 1] = static_cast<sunindextype>(entryIndex);
    }
    const auto oldSize = pattern.size();
    pattern.resize(oldSize + entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        pattern[oldSize + index] = entries[index].second;
    }
    return pattern;
}

bool sparsePatternContains(const SparsePattern& base,
                           const SparsePattern& candidate,
                           count_t stateCount)
{
    if ((base.size() < static_cast<size_t>(stateCount) + 1) ||
        (candidate.size() < static_cast<size_t>(stateCount) + 1)) {
        return false;
    }
    const auto baseColumns = base.data() + stateCount + 1;
    const auto candidateColumns = candidate.data() + stateCount + 1;
    for (sunindextype row = 0; row < stateCount; ++row) {
        const auto baseBegin = baseColumns + base[row];
        const auto baseEnd = baseColumns + base[row + 1];
        const auto candidateBegin = candidateColumns + candidate[row];
        const auto candidateEnd = candidateColumns + candidate[row + 1];
        if (!std::includes(baseBegin, baseEnd, candidateBegin, candidateEnd)) {
            return false;
        }
    }
    return true;
}

SparsePattern sparsePatternUnion(const SparsePattern& first,
                                 const SparsePattern& second,
                                 count_t stateCount)
{
    std::vector<std::vector<sunindextype>> columns(stateCount);
    const auto addPattern = [&](const SparsePattern& pattern) {
        if (pattern.size() < static_cast<size_t>(stateCount) + 1) {
            return;
        }
        const auto patternColumns = pattern.data() + stateCount + 1;
        for (sunindextype row = 0; row < stateCount; ++row) {
            const auto begin = patternColumns + pattern[row];
            const auto end = patternColumns + pattern[row + 1];
            columns[row].insert(columns[row].end(), begin, end);
        }
    };
    addPattern(first);
    addPattern(second);

    SparsePattern result(static_cast<size_t>(stateCount) + 1, 0);
    for (sunindextype row = 0; row < stateCount; ++row) {
        auto& rowColumns = columns[row];
        std::sort(rowColumns.begin(), rowColumns.end());
        rowColumns.erase(std::unique(rowColumns.begin(), rowColumns.end()), rowColumns.end());
        result[row + 1] = result[row] + static_cast<sunindextype>(rowColumns.size());
    }
    const auto oldSize = result.size();
    result.resize(oldSize + result.back());
    sunindextype entryIndex = 0;
    for (const auto& rowColumns : columns) {
        for (const auto column : rowColumns) {
            result[oldSize + entryIndex] = column;
            ++entryIndex;
        }
    }
    return result;
}

bool writeFixedSparseMatrix(SUNMatrix j,
                            const SparsePattern& pattern,
                            MatrixData<double>& matrixData,
                            count_t stateCount)
{
    auto* matrix = SM_CONTENT_S(j);
    const auto used = pattern.back();
    if (used > matrix->NNZ) {
        const auto retval = SUNSparseMatrix_Reallocate(j, used);
        if (retval < 0) {
            return false;
        }
        matrix = SM_CONTENT_S(j);
    }

    std::copy_n(pattern.data(), static_cast<size_t>(stateCount) + 1, matrix->indexptrs);
    std::copy_n(pattern.data() + static_cast<size_t>(stateCount) + 1,
                static_cast<size_t>(used),
                matrix->indexvals);
    std::fill_n(matrix->data, static_cast<size_t>(used), 0.0);

    matrixData.start();
    while (matrixData.moreData()) {
        const auto element = matrixData.next();
        if ((element.row < 0) || (element.row >= stateCount)) {
            continue;
        }
        const auto row = static_cast<sunindextype>(element.row);
        const auto column = static_cast<sunindextype>(element.col);
        const auto begin = matrix->indexvals + matrix->indexptrs[row];
        const auto end = matrix->indexvals + matrix->indexptrs[row + 1];
        const auto found = std::lower_bound(begin, end, column);
        if ((found != end) && (*found == column)) {
            matrix->data[found - matrix->indexvals] += element.data;
        }
    }
    return true;
}
}  // namespace

void matrixDataToSUNMatrix(MatrixData<double>& md, SUNMatrix j, count_t svsize)
{
    int id = SUNMatGetID(j);
    if (id == SUNMATRIX_SPARSE) {
        auto m = SM_CONTENT_S(j);
        count_t indval = 0;
        m->indexptrs[0] = indval;

        md.compact();
        assert(m->NNZ >= static_cast<int>(md.size()));
        auto sz = static_cast<int>(md.size());
        /*
  auto itel = md.begin();
  for (int kk = 0; kk < sz; ++kk)
  {
      auto tp = *itel;
      //      printf("kk: %d  dataval: %f  rowind: %d   colind: %d \n ", kk, a1->val(kk),
  a1->rowIndex(kk), a1->colIndex(kk)); if (tp.col > colval)
      {
          colval++;
          J->colptrs[colval] = kk;
      }

      J->data[kk] = tp.data;
      J->rowvals[kk] = tp.row;
      ++itel;
  }
*/
        // SlsSetToZero(J);

        md.start();
        for (int kk = 0; kk < sz; ++kk) {
            auto tp = md.next();
            //      printf("kk: %d  dataval: %f  rowind: %d   colind: %d \n ", kk, a1->val(kk),
            //      a1->rowIndex(kk),
            // a1->colIndex(kk));
            if (tp.row > indval) {
                indval++;
                m->indexptrs[indval] = kk;
                assert(tp.row == indval);
            }

            m->data[kk] = tp.data;
            m->indexvals[kk] = tp.col;
        }

        if (indval + 1 != svsize) {
            std::println("sz={}, svsize={}, colval+1={}", sz, svsize, indval + 1);
        }
        assert(indval + 1 == svsize);
        m->indexptrs[indval + 1] = sz;
    } else if (id == SUNMATRIX_DENSE) {
        auto denseMatrix = SundialsMatrixDataDense(j);
        denseMatrix.clear();
        md.start();
        while (md.moreData()) {
            const auto element = md.next();
            denseMatrix.assign(element.row, element.col, element.data);
        }
    }
}

// Error handling function for Sundials
void sundialsErrorHandlerFunc(int line,
                              const char* function,
                              const char* file,
                              const char* msg,
                              SUNErrCode errorCode,
                              void* userData,
                              SUNContext /*sunctx*/)
{
    if (errorCode == 0) {
        return;
    }
    auto sd = reinterpret_cast<SolverInterface*>(userData);
    auto message =
        std::format("SUNDIALS ERROR({}) in {} [{}:{}]::{}", errorCode, function, file, line, msg);
    sd->logMessage(errorCode, message);
}

bool matrixNeedsSetup(count_t callCount, SUNMatrix j)
{
    switch (SUNMatGetID(j)) {
        case SUNMATRIX_DENSE:
            return false;
        case SUNMATRIX_SPARSE:
            return ((callCount == 0) || (!isSUNMatrixSetup(j)));
        default:
            return false;
    }
}
#define CHECK_JACOBIAN 0

int sundialsJac(sunrealtype time,
                sunrealtype cj,
                N_Vector state,
                N_Vector dstateDt,
                SUNMatrix j,
                void* userData,
                N_Vector /*tmp1*/,
                N_Vector /*tmp2*/)
{
    auto sd = reinterpret_cast<SundialsInterface*>(userData);
    const bool performance = (sd->m_gds != nullptr) &&
        sd->m_gds->isFlagSet(PARTITIONED_DIAGNOSTICS_FLAG) &&
        (sd->mode.pairedOffsetIndex != kNullLocation);
    const auto jacobianStart = performance ? std::chrono::steady_clock::now() :
                                             std::chrono::steady_clock::time_point{};
    const auto finishPerformance = [&]() {
        if (performance) {
            ++sd->performanceJacobianCalls;
            sd->performanceJacobianTime +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - jacobianStart)
                    .count();
        }
    };
    auto* stateData = nvecdata(sd->use_omp, state);
    auto* dstateData = nvecdata(sd->use_omp, dstateDt);
    const bool partitionedSparse = (SUNMatGetID(j) == SUNMATRIX_SPARSE) &&
        (sd->mode.pairedOffsetIndex != kNullLocation);

    if (matrixNeedsSetup(sd->jacCallCount, j)) {
        auto a1 = makeSparseMatrix(sd->svsize, sd->maxNNZ);

        a1->setRowLimit(sd->svsize);
        a1->setColLimit(sd->svsize);

        if (sd->flags[USE_MASK_FLAG]) {
            MatrixDataFilter<double> filterAd(*(a1));
            filterAd.addFilter(sd->maskElements);
            const auto modelJacobianStart = performance ? std::chrono::steady_clock::now() :
                                                          std::chrono::steady_clock::time_point{};
            sd->m_gds->jacobianFunction(time, stateData, dstateData, filterAd, cj, sd->mode);
            if (performance) {
                sd->performanceModelJacobianTime +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                   modelJacobianStart)
                        .count();
            }
            for (auto& v : sd->maskElements) {
                a1->assign(v, v, 1.0);
            }
        } else {
            const auto modelJacobianStart = performance ? std::chrono::steady_clock::now() :
                                                          std::chrono::steady_clock::time_point{};
            sd->m_gds->jacobianFunction(time, stateData, dstateData, *a1, cj, sd->mode);
            if (performance) {
                sd->performanceModelJacobianTime +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                   modelJacobianStart)
                        .count();
            }
        }

        ++sd->jacCallCount;
#ifdef _DEBUG
        if (SM_CONTENT_S(j)->NNZ < static_cast<int>(a1->size())) {
            a1->compact();
            if (SM_CONTENT_S(j)->NNZ < static_cast<int>(a1->size())) {
                jacobianAnalysis(*a1, sd->m_gds, sd->mode, 5);
            }
        }
#endif
        matrixDataToSUNMatrix(*a1, j, sd->svsize);
        sd->nnz = a1->size();
        if (SUNMatGetID(j) == SUNMATRIX_SPARSE) {
            const auto pattern = sparsePatternFromMatrix(j, sd->svsize);
            const bool structureChanged =
                !sd->sparsePattern.empty() && (sd->sparsePattern != pattern);
            sd->sparsePattern = pattern;
            if (structureChanged) {
                // KLU caches a symbolic factorization of the compressed
                // pattern. A changed pattern requires a fresh factorization;
                // refactoring it as though it were unchanged is unsafe.
                // The current matrix has already been rebuilt here.  Keep the
                // Jacobian-call count so subsequent partitioned callbacks use
                // the fixed-union path instead of restarting as first setup.
                sd->kluReInit(SolverInterface::SparseReinitMode::REFACTOR, false);
                if (sd->m_gds->isFlagSet(PARTITIONED_DIAGNOSTICS_FLAG) &&
                    (sd->mode.pairedOffsetIndex != kNullLocation)) {
                    sd->m_gds->partitionedDiagnostic(
                        "Partitioned sparse Jacobian pattern changed; KLU symbolic factorization reset");
                }
            }
        }
        if ((SUNMatGetID(j) == SUNMATRIX_SPARSE) &&
            sd->m_gds->isFlagSet(PARTITIONED_DIAGNOSTICS_FLAG) &&
            (sd->mode.pairedOffsetIndex != kNullLocation) && (sd->jacCallCount <= 8)) {
            auto* matrix = SM_CONTENT_S(j);
            const auto used = matrix->indexptrs[sd->svsize];
            bool valid = (matrix->indexptrs[0] == 0) && (used >= 0) && (used <= matrix->NNZ);
            std::uint64_t structureHash = 1469598103934665603ULL;
            for (index_t row = 0; valid && row < sd->svsize; ++row) {
                const auto begin = matrix->indexptrs[row];
                const auto end = matrix->indexptrs[row + 1];
                valid = (begin >= 0) && (begin <= end) && (end <= used);
                structureHash =
                    (structureHash ^ static_cast<std::uint64_t>(begin)) * 1099511628211ULL;
            }
            for (sunindextype index = 0; valid && index < used; ++index) {
                const auto column = matrix->indexvals[index];
                valid = (column >= 0) && (column < sd->svsize);
                structureHash =
                    (structureHash ^ static_cast<std::uint64_t>(column)) * 1099511628211ULL;
            }
            sd->m_gds->partitionedDiagnostic(std::format(
                "Partitioned sparse Jacobian {}: used_nnz={} capacity={} valid={} structure_hash={:016X}",
                sd->jacCallCount,
                used,
                matrix->NNZ,
                valid,
                structureHash));
            if (!valid) {
                finishPerformance();
                return FUNCTION_EXECUTION_FAILURE;
            }
        }
        if (sd->flags[FILE_CAPTURE_FLAG]) {
            if (!sd->jacFile.empty()) {
                auto val = static_cast<std::uint32_t>(sd->get("nliterations"));
                writeArray(time, 1, val, sd->mode.offsetIndex, *a1, sd->jacFile);
            }
        }
    } else if (partitionedSparse) {
        // Partitioned Jacobians can change their active entries as the trial
        // state moves.  Build the current pattern into a temporary matrix,
        // then add any newly observed entries to a fixed union pattern.  The
        // union is written back into the same SUNMatrix, so KINSOL/CVODE keep
        // the matrix handle they received from SetLinearSolver.
        auto a1 = makeSparseMatrix(sd->svsize, sd->maxNNZ);
        a1->setRowLimit(sd->svsize);
        a1->setColLimit(sd->svsize);
        if (sd->flags[USE_MASK_FLAG]) {
            MatrixDataFilter<double> filterAd(*a1);
            filterAd.addFilter(sd->maskElements);
            const auto modelJacobianStart = performance ? std::chrono::steady_clock::now() :
                                                          std::chrono::steady_clock::time_point{};
            sd->m_gds->jacobianFunction(time, stateData, dstateData, filterAd, cj, sd->mode);
            if (performance) {
                sd->performanceModelJacobianTime +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                   modelJacobianStart)
                        .count();
            }
            for (auto& v : sd->maskElements) {
                a1->assign(v, v, 1.0);
            }
        } else {
            const auto modelJacobianStart = performance ? std::chrono::steady_clock::now() :
                                                          std::chrono::steady_clock::time_point{};
            sd->m_gds->jacobianFunction(time, stateData, dstateData, *a1, cj, sd->mode);
            if (performance) {
                sd->performanceModelJacobianTime +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                   modelJacobianStart)
                        .count();
            }
        }

        const auto currentPattern = sparsePatternFromData(*a1, sd->svsize);
        if (sd->sparsePattern.empty()) {
            sd->sparsePattern = currentPattern;
        }
        const bool expandsPattern = !sparsePatternContains(
            sd->sparsePattern, currentPattern, sd->svsize);
        if (expandsPattern) {
            const auto previousNnz = sd->sparsePattern.back();
            const auto unionPattern =
                sparsePatternUnion(sd->sparsePattern, currentPattern, sd->svsize);
            if (!writeFixedSparseMatrix(j, unionPattern, *a1, sd->svsize)) {
                finishPerformance();
                return FUNCTION_EXECUTION_FAILURE;
            }
            sd->sparsePattern = unionPattern;
            sd->maxNNZ = (std::max)(sd->maxNNZ, static_cast<count_t>(unionPattern.back()));
            // The matrix structure has grown, so discard KLU's symbolic and
            // numeric factors.  The Jacobian has already been populated and
            // remains valid, therefore do not force another GridDyn Jacobian
            // callback by resetting jacCallCount.
            sd->kluReInit(SolverInterface::SparseReinitMode::REFACTOR, false);
            if (sd->m_gds->isFlagSet(PARTITIONED_DIAGNOSTICS_FLAG)) {
                sd->m_gds->partitionedDiagnostic(std::format(
                    "Partitioned sparse Jacobian union expanded: previous_nnz={} current_nnz={} union_nnz={}"
                    " KLU symbolic factorization reset",
                    previousNnz,
                    currentPattern.back(),
                    unionPattern.back()));
            }
        } else if (!writeFixedSparseMatrix(j, sd->sparsePattern, *a1, sd->svsize)) {
            finishPerformance();
            return FUNCTION_EXECUTION_FAILURE;
        }
        ++sd->jacCallCount;
        sd->nnz = a1->size();
        if (sd->flags[FILE_CAPTURE_FLAG] && !sd->jacFile.empty()) {
            writeArray(time, 1, sd->jacCallCount, sd->mode.offsetIndex, *a1, sd->jacFile);
        }
    } else {
        // if it isn't the first we can use the SUNDIALS arraySparse object
        auto a1 = makeSundialsMatrixData(j);
        a1->clear();
        if (sd->flags[USE_MASK_FLAG]) {
            MatrixDataFilter<double> filterAd(*a1);
            filterAd.addFilter(sd->maskElements);
            const auto modelJacobianStart = performance ? std::chrono::steady_clock::now() :
                                                          std::chrono::steady_clock::time_point{};
            sd->m_gds->jacobianFunction(time, stateData, dstateData, filterAd, cj, sd->mode);
            if (performance) {
                sd->performanceModelJacobianTime +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                   modelJacobianStart)
                        .count();
            }
            for (auto& v : sd->maskElements) {
                a1->assign(v, v, 1.0);
            }
        } else {
            const auto modelJacobianStart = performance ? std::chrono::steady_clock::now() :
                                                          std::chrono::steady_clock::time_point{};
            sd->m_gds->jacobianFunction(time, stateData, dstateData, *a1, cj, sd->mode);
            if (performance) {
                sd->performanceModelJacobianTime +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                   modelJacobianStart)
                        .count();
            }
        }

        sd->jacCallCount++;
        if (sd->flags[FILE_CAPTURE_FLAG]) {
            if (!sd->jacFile.empty()) {
                writeArray(time, 1, sd->jacCallCount, sd->mode.offsetIndex, *a1, sd->jacFile);
            }
        }
    }
/*
MatrixDataSparse<double> &a1 = sd->a1;

sd->m_gds->jacobianFunction (time, nvecdata(sd->use_omp, state), nvecdata(sd->use_omp, dstate_dt),
a1,cj, sd->mode); a1.sortIndexCol (); if (sd->flags[USE_MASK_FLAG])
{
for (auto &v : sd->maskElements)
{
a1.translateRow (v,kNullLocation);
a1.assign (v, v,1);
}
a1.filter ();
a1.sortIndexCol ();
}
a1.compact ();

SlsSetToZero (J);

count_t colval = 0;
J->colptrs[0] = colval;
for (index_t kk = 0; kk < a1.size (); ++kk)
{
//    printf("kk: %d  dataval: %f  rowind: %d   colind: %d \n ", kk, a1->val(kk), a1->rowIndex(kk),
a1->colIndex(kk));
if (a1.colIndex (kk) > colval)
{
colval++;
J->colptrs[colval] = static_cast<int> (kk);
}
J->data[kk] = a1.val (kk);
J->rowvals[kk] = a1.rowIndex (kk);
}
J->colptrs[colval + 1] = static_cast<int> (a1.size ());

if (sd->flags[FILE_CAPTURE_FLAG])
{
if (!sd->jacFile.empty())
{
long int val = 0;
IDAGetNumNonlinSolvIters(sd->solverMem, &val);
writeArray(sd->solveTime, 1, val, sd->mode.offsetIndex, a1, sd->jacFile);
}
}
*/
#if (CHECK_JACOBIAN > 0)
    auto mv = findMissing(a1);
    for (auto& me : mv) {
        std::println("no entries for element {}", me);
    }
#endif
    finishPerformance();
    return FUNCTION_EXECUTION_SUCCESS;
}

}  // namespace griddyn::solvers
