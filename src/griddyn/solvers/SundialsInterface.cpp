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
#include <cstdio>
#include <format>
#include <memory>
#include <print>
#include <string>

namespace griddyn::solvers {
static void ensureSundialsFactories()
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
    }
    return SolverInterface::get(param);
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
    if (LS != nullptr) {
        SUNLinSolFree(LS);
        LS = nullptr;
    }
    if (J != nullptr) {
        SUNMatDestroy(J);
        J = nullptr;
    }
}

void SundialsInterface::kluReInit(SparseReinitMode sparseReInitModes)
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
    jacCallCount = 0;
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
        if ((m->indexptrs[m->N] <= 0) || (m->indexptrs[m->N] >= m->NNZ)) {
            return false;
        }
    }
    return true;
}

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
    auto* stateData = nvecdata(sd->use_omp, state);
    auto* dstateData = nvecdata(sd->use_omp, dstateDt);

    if (matrixNeedsSetup(sd->jacCallCount, j)) {
        auto a1 = makeSparseMatrix(sd->svsize, sd->maxNNZ);

        a1->setRowLimit(sd->svsize);
        a1->setColLimit(sd->svsize);

        if (sd->flags[USE_MASK_FLAG]) {
            MatrixDataFilter<double> filterAd(*(a1));
            filterAd.addFilter(sd->maskElements);
            sd->m_gds->jacobianFunction(time,
                                        stateData,
                                        dstateData,
                                        filterAd,
                                        cj,
                                        sd->mode);
            for (auto& v : sd->maskElements) {
                a1->assign(v, v, 1.0);
            }
        } else {
            sd->m_gds->jacobianFunction(time,
                                        stateData,
                                        dstateData,
                                        *a1,
                                        cj,
                                        sd->mode);
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
        if (sd->flags[FILE_CAPTURE_FLAG]) {
            if (!sd->jacFile.empty()) {
                auto val = static_cast<std::uint32_t>(sd->get("nliterations"));
                writeArray(time, 1, val, sd->mode.offsetIndex, *a1, sd->jacFile);
            }
        }
    } else {
        // if it isn't the first we can use the SUNDIALS arraySparse object
        auto a1 = makeSundialsMatrixData(j);
        if (sd->flags[USE_MASK_FLAG]) {
            MatrixDataFilter<double> filterAd(*a1);
            filterAd.addFilter(sd->maskElements);
            sd->m_gds->jacobianFunction(time,
                                        stateData,
                                        dstateData,
                                        filterAd,
                                        cj,
                                        sd->mode);
            for (auto& v : sd->maskElements) {
                a1->assign(v, v, 1.0);
            }
        } else {
            sd->m_gds->jacobianFunction(time,
                                        stateData,
                                        dstateData,
                                        *a1,
                                        cj,
                                        sd->mode);
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
    return FUNCTION_EXECUTION_SUCCESS;
}

}  // namespace griddyn::solvers
