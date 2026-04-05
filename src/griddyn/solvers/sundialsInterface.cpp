/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sundialsInterface.h"

#include "griddyn/griddyn-config.h"
#include "idaInterface.h"
#include "kinsolInterface.h"
#ifdef GRIDDYN_ENABLE_CVODE
#    include "cvodeInterface.h"
#endif
#ifdef GRIDDYN_ENABLE_ARKODE
#    include "arkodeInterface.h"
#endif

#ifdef GRIDDYN_ENABLE_KLU
#    include <sunlinsol/sunlinsol_klu.h>
#endif

#include "../gridDynSimulation.h"
#include "../simulation/diagnostics.h"
#include "../simulation/gridDynSimulationFileOps.h"
#include "core/factoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "sundialsMatrixData.h"
#include "utilities/matrixCreation.h"
#include "utilities/matrixDataFilter.hpp"
#include <cassert>
#include <cstdio>

namespace griddyn {
namespace solvers {
    static childClassFactory<kinsolInterface, SolverInterface>
        kinFactory(stringVec{"kinsol", "algebraic"});
    static childClassFactory<idaInterface, SolverInterface>
        idaFactory(stringVec{"ida", "dae", "dynamic"});
#ifdef GRIDDYN_ENABLE_CVODE
    static childClassFactory<cvodeInterface, SolverInterface>
        cvodeFactory(stringVec{"cvode", "dyndiff", "differential"});
#endif

#ifdef GRIDDYN_ENABLE_ARKODE
    static childClassFactory<arkodeInterface, SolverInterface> arkodeFactory(stringVec{"arkode"});
#endif

    sundialsInterface::sundialsInterface(const std::string& objName): SolverInterface(objName)
    {
        tolerance = 1e-8;
        int retval = SUNContext_Create(nullptr, &sunctx);
        check_flag(&retval, "SUNContext_Create", 1);
    }
    sundialsInterface::sundialsInterface(gridDynSimulation* gds, const solverMode& sMode):
        SolverInterface(gds, sMode)
    {
        tolerance = 1e-8;
        int retval = SUNContext_Create(nullptr, &sunctx);
        check_flag(&retval, "SUNContext_Create", 1);
    }

    sundialsInterface::~sundialsInterface()
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
        if (flags[initialized_flag]) {
            if (m_sundialsInfoFile != nullptr) {
                fclose(m_sundialsInfoFile);
            }
            if (LS != nullptr) {
                SUNLinSolFree(LS);
            }
            if (J != nullptr) {
                SUNMatDestroy(J);
            }
        }
        if (sunctx != nullptr) {
            SUNContext_Free(&sunctx);
        }
    }

    std::unique_ptr<SolverInterface> sundialsInterface::clone(bool fullCopy) const
    {
        std::unique_ptr<SolverInterface> si = std::make_unique<sundialsInterface>();
        sundialsInterface::cloneTo(si.get(), fullCopy);
        return si;
    }

    void sundialsInterface::cloneTo(SolverInterface* si, bool fullCopy) const
    {
        SolverInterface::cloneTo(si, fullCopy);
        auto ai = dynamic_cast<sundialsInterface*>(si);
        if (ai == nullptr) {
            return;
        }
        ai->maxNNZ = maxNNZ;
        if ((fullCopy) && (flags[allocated_flag])) {
            auto tols = nvecdata(use_omp, abstols);
            std::copy(tols, tols + svsize, nvecdata(use_omp, ai->abstols));
            auto cons = nvecdata(use_omp, consData);
            std::copy(cons, cons + svsize, nvecdata(use_omp, ai->consData));
            auto sc = nvecdata(use_omp, scale);
            std::copy(sc, sc + svsize, nvecdata(use_omp, ai->scale));
        }
    }

    void sundialsInterface::allocate(count_t stateCount, count_t /*numRoots*/)
    {
        // load the vectors
        if (stateCount == svsize) {
            return;
        }

        bool prev_omp = use_omp;
        _unused(prev_omp);  // looks unused if OPENMP is not available
        use_omp = flags[use_omp_flag];
        flags.reset(initialized_flag);
        if (state != nullptr) {
            NVECTOR_DESTROY(prev_omp, state);
        }
        state = NVECTOR_NEW(use_omp, stateCount);
        check_flag(state, "NVECTOR_NEW", 0);

        if (hasDifferential(mode)) {
            if (dstate_dt != nullptr) {
                NVECTOR_DESTROY(prev_omp, dstate_dt);
            }
            dstate_dt = NVECTOR_NEW(use_omp, stateCount);
            check_flag(dstate_dt, "NVECTOR_NEW", 0);

            N_VConst(ZERO, dstate_dt);
        }
        if (abstols != nullptr) {
            NVECTOR_DESTROY(prev_omp, abstols);
        }
        abstols = NVECTOR_NEW(use_omp, stateCount);
        check_flag(abstols, "NVECTOR_NEW", 0);

        if (consData != nullptr) {
            NVECTOR_DESTROY(prev_omp, consData);
        }
        consData = NVECTOR_NEW(use_omp, stateCount);
        check_flag(consData, "NVECTOR_NEW", 0);

        if (scale != nullptr) {
            NVECTOR_DESTROY(prev_omp, scale);
        }
        scale = NVECTOR_NEW(use_omp, stateCount);
        check_flag(scale, "NVECTOR_NEW", 0);

        N_VConst(ONE, scale);

        if (isDAE(mode)) {
            if (types != nullptr) {
                NVECTOR_DESTROY(prev_omp, types);
            }
            types = NVECTOR_NEW(use_omp, stateCount);
            check_flag(types, "NVECTOR_NEW", 0);

            N_VConst(ONE, types);
        }

        svsize = stateCount;

        flags.set(allocated_flag);
    }

    void sundialsInterface::setMaxNonZeros(count_t nonZeroCount)
    {
        maxNNZ = nonZeroCount;
        nnz = nonZeroCount;
    }

    double* sundialsInterface::state_data() noexcept
    {
        return nvecdata(use_omp, state);
    }
    double* sundialsInterface::deriv_data() noexcept
    {
        return nvecdata(use_omp, dstate_dt);
    }

    const double* sundialsInterface::state_data() const noexcept
    {
        return nvecdata(use_omp, state);
    }

    const double* sundialsInterface::deriv_data() const noexcept
    {
        return nvecdata(use_omp, dstate_dt);
    }
    // output solver stats

    double* sundialsInterface::type_data() noexcept
    {
        return nvecdata(use_omp, types);
    }
    const double* sundialsInterface::type_data() const noexcept
    {
        return nvecdata(use_omp, types);
    }

    double sundialsInterface::get(const std::string& param) const
    {
        if (param == "maxnnz") {
            return static_cast<double>(maxNNZ);
        }
        return SolverInterface::get(param);
    }

    void sundialsInterface::KLUReInit(sparse_reinit_modes sparseReInitModes)
    {
#ifdef GRIDDYN_ENABLE_KLU
        if (flags[dense_flag]) {
            return;
        }
        switch (sparseReInitModes) {
            case sparse_reinit_modes::refactor: {
                int retval = SUNLinSol_KLUReInit(LS, J, maxNNZ, SUNKLU_REINIT_PARTIAL);
                check_flag(&retval, "SUNLinSol_KLUReInit", 1);
            } break;
            case sparse_reinit_modes::resize:
                /*there is a major bug in sundials with KLU on resize*/
                {
                    if (maxNNZ > SM_NNZ_S(J)) {
                        SUNMatDestroy(J);
                        J = SUNSparseMatrix(svsize, svsize, maxNNZ, CSR_MAT, sunctx);
                        int retval = SUNLinSol_KLUReInit(LS, J, maxNNZ, SUNKLU_REINIT_PARTIAL);
                        check_flag(&retval, "SUNLinSol_KLUReInit", 1);
                    } else {
                        int retval = SUNLinSol_KLUReInit(LS, J, maxNNZ, SUNKLU_REINIT_PARTIAL);
                        check_flag(&retval, "SUNLinSol_KLUReInit", 1);
                    }
                }
                break;
        }
        jacCallCount = 0;
#endif
    }

    bool isSUNMatrixSetup(SUNMatrix J)
    {
        int id = SUNMatGetID(J);
        if (id == SUNMATRIX_SPARSE) {
            auto M = SM_CONTENT_S(J);
            if ((M->indexptrs[0] != 0) || (M->indexptrs[0] > M->NNZ)) {
                return false;
            }
            if ((M->indexptrs[M->N] <= 0) || (M->indexptrs[M->N] >= M->NNZ)) {
                return false;
            }
        }
        return true;
    }

    void matrixDataToSUNMatrix(matrixData<double>& md, SUNMatrix J, count_t svsize)
    {
        int id = SUNMatGetID(J);
        if (id == SUNMATRIX_SPARSE) {
            auto M = SM_CONTENT_S(J);
            count_t indval = 0;
            M->indexptrs[0] = indval;

            md.compact();
            assert(M->NNZ >= static_cast<int>(md.size()));
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
                    M->indexptrs[indval] = kk;
                    assert(tp.row == indval);
                }

                M->data[kk] = tp.data;
                M->indexvals[kk] = tp.col;
            }

            if (indval + 1 != svsize) {
                printf("sz=%d, svsize=%d, colval+1=%d\n", sz, svsize, indval + 1);
            }
            assert(indval + 1 == svsize);
            M->indexptrs[indval + 1] = sz;
        } else if (id == SUNMATRIX_DENSE) {
        }
    }

    // Error handling function for Sundials
    void sundialsErrorHandlerFunc(int error_code,
                                  const char* module,
                                  const char* function,
                                  char* msg,
                                  void* user_data)
    {
        if (error_code == 0) {
            return;
        }
        auto sd = reinterpret_cast<SolverInterface*>(user_data);
        std::string message = "SUNDIALS ERROR(" + std::to_string(error_code) + ") in Module (" +
            std::string(module) + ") function " + std::string(function) + "::" + std::string(msg);
        sd->logMessage(error_code, message);
    }

    bool MatrixNeedsSetup(count_t callCount, SUNMatrix J)
    {
        switch (SUNMatGetID(J)) {
            case SUNMATRIX_DENSE:
                return false;
            case SUNMATRIX_SPARSE:
                return ((callCount == 0) || (!isSUNMatrixSetup(J)));
            default:
                return false;
        }
    }
#define CHECK_JACOBIAN 0

    int sundialsJac(realtype time,
                    realtype cj,
                    N_Vector state,
                    N_Vector dstate_dt,
                    SUNMatrix J,
                    void* user_data,
                    N_Vector /*tmp1*/,
                    N_Vector /*tmp2*/)
    {
        auto sd = reinterpret_cast<sundialsInterface*>(user_data);

        if (MatrixNeedsSetup(sd->jacCallCount, J)) {
            auto a1 = makeSparseMatrix(sd->svsize, sd->maxNNZ);

            a1->setRowLimit(sd->svsize);
            a1->setColLimit(sd->svsize);

            if (sd->flags[useMask_flag]) {
                matrixDataFilter<double> filterAd(*(a1));
                filterAd.addFilter(sd->maskElements);
                sd->m_gds->jacobianFunction(time,
                                            nvecdata(sd->use_omp, state),
                                            nvecdata(sd->use_omp, dstate_dt),
                                            filterAd,
                                            cj,
                                            sd->mode);
                for (auto& v : sd->maskElements) {
                    a1->assign(v, v, 1.0);
                }
            } else {
                sd->m_gds->jacobianFunction(time,
                                            nvecdata(sd->use_omp, state),
                                            nvecdata(sd->use_omp, dstate_dt),
                                            *a1,
                                            cj,
                                            sd->mode);
            }

            ++sd->jacCallCount;
#ifdef _DEBUG
            if (SM_CONTENT_S(J)->NNZ < static_cast<int>(a1->size())) {
                a1->compact();
                if (SM_CONTENT_S(J)->NNZ < static_cast<int>(a1->size())) {
                    jacobianAnalysis(*a1, sd->m_gds, sd->mode, 5);
                }
            }
#endif
            matrixDataToSUNMatrix(*a1, J, sd->svsize);
            sd->nnz = a1->size();
            if (sd->flags[fileCapture_flag]) {
                if (!sd->jacFile.empty()) {
                    auto val = static_cast<std::uint32_t>(sd->get("nliterations"));
                    writeArray(time, 1, val, sd->mode.offsetIndex, *a1, sd->jacFile);
                }
            }
        } else {
            // if it isn't the first we can use the SUNDIALS arraySparse object
            auto a1 = makeSundialsMatrixData(J);
            if (sd->flags[useMask_flag]) {
                matrixDataFilter<double> filterAd(*a1);
                filterAd.addFilter(sd->maskElements);
                sd->m_gds->jacobianFunction(time,
                                            nvecdata(sd->use_omp, state),
                                            nvecdata(sd->use_omp, dstate_dt),
                                            filterAd,
                                            cj,
                                            sd->mode);
                for (auto& v : sd->maskElements) {
                    a1->assign(v, v, 1.0);
                }
            } else {
                sd->m_gds->jacobianFunction(time,
                                            nvecdata(sd->use_omp, state),
                                            nvecdata(sd->use_omp, dstate_dt),
                                            *a1,
                                            cj,
                                            sd->mode);
            }

            sd->jacCallCount++;
            if (sd->flags[fileCapture_flag]) {
                if (!sd->jacFile.empty()) {
                    writeArray(time, 1, sd->jacCallCount, sd->mode.offsetIndex, *a1, sd->jacFile);
                }
            }
        }
/*
matrixDataSparse<double> &a1 = sd->a1;

sd->m_gds->jacobianFunction (time, nvecdata(sd->use_omp, state), nvecdata(sd->use_omp, dstate_dt),
a1,cj, sd->mode); a1.sortIndexCol (); if (sd->flags[useMask_flag])
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

if (sd->flags[fileCapture_flag])
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
            printf("no entries for element %d\n", me);
        }
#endif
        return FUNCTION_EXECUTION_SUCCESS;
    }

}  // namespace solvers
}  // namespace griddyn
