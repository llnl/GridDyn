/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../paradae/math/SMultiVector.h"
#include "../paradae/math/Vector.h"
#include "../paradae/problems/ODEProblem.h"
#include "braid.h"
#include <list>
#include <map>

enum BdfStrat {
    NO_BDF,
    USUAL,
    USUAL_C,
    UNI0,
    UNI0_C,
    UNI1,
    UNI1_C,
    INJECT,
    INJECT_C,
    EXTRAP,
    EXTRAP_C
};

/*!< Basic Vector structure for braid driver
 */
typedef struct _braid_Vector_struct {
    // Shell part
    griddyn::paradae::SMultiVector tprev;
    griddyn::paradae::SVector state;  // JBS:  This "state" is I believe related either only to BDF
                                      // or to root-finding.  Not a well named variable.

    // Vector part (this part contains actual "state" information)
    griddyn::paradae::SMultiVector xprev; /*!< This is only a list of Vectors when multistepping,
                                             otherwise it's just one vector */
    griddyn::paradae::SVector dxprev;
} my_Vector;

/*!< Basic Application structure for braid driver
 * Simply wrap the ODEProblem class
 */
typedef struct _braid_App_struct {
    griddyn::paradae::ODEProblem* ode; /*!< Pointer to the main class ODEProblem */
    int nb_multisteps;
    int size_x; /*!< Size of the problem */
    int size_state;
    BdfStrat bdf_strat;
    // bool do_bdf_uniform;
    // bool do_lowerorder; /*!< Do we lower the order of BDF method ? */
    int lowered_by_level; /*!< Lowered by how much each level ? */
    int min_order;
    // std::string do_interp;
    int nb_initial;
    griddyn::paradae::Real* grid_initial;
    griddyn::paradae::Real* braid_grid_initial;
    int prevlvl;
    std::map<griddyn::paradae::Real, my_Vector> initial_vector;
    griddyn::paradae::DATA_Struct alloc_data;
    my_Vector* solution_tfinal;

    _braid_App_struct(griddyn::paradae::ODEProblem* ode_);
    void setAllToDataStruct(braid_Vector u);
    void setLastToDataStruct(braid_Vector u);
    void setAllFromDataStruct(braid_Vector u);
    void setLastFromDataStruct(braid_Vector u);
    void dumpDataStruct();
} Braid_App;

namespace griddyn::braid {

paradae::Real IntegrationLoop(braid_App app,
                              std::list<paradae::Real>& tprev,
                              std::list<paradae::Vector>& xprev,
                              const std::list<paradae::Real>& tstops,
                              std::list<paradae::Vector>& xstops,
                              paradae::Vector& dxprev);
void braidStepOnAllPoints(braid_App app,
                          braid_Vector ustop,
                          braid_Vector fstop,
                          braid_Vector u,
                          braid_StepStatus status,
                          int level);
void braidStepOnOnePoint(braid_App app,
                         braid_Vector ustop,
                         braid_Vector fstop,
                         braid_Vector u,
                         braid_StepStatus status,
                         int level);
int braidStep(braid_App app,
              braid_Vector ustop,
              braid_Vector fstop,
              braid_Vector u,
              braid_StepStatus status);
int braidSpatialRefine(braid_App app,
                       braid_Vector cu,
                       braid_Vector* fu_ptr,
                       braid_CoarsenRefStatus status);
int braidSpatialCoarsen(braid_App app,
                        braid_Vector fu,
                        braid_Vector* cu_ptr,
                        braid_CoarsenRefStatus status);
int braidInit(braid_App app, paradae::Real t, braid_Vector* u_ptr);
int braidInitShell(braid_App app, paradae::Real t, braid_Vector* u_ptr);
int braidClone(braid_App app, braid_Vector u, braid_Vector* v_ptr);
int braidCloneShell(braid_App app, braid_Vector u, braid_Vector* v_ptr);
int braidFree(braid_App app, braid_Vector u);
int braidFreeShell(braid_App app, braid_Vector u);
int braidPropagateShell(braid_App app, braid_Vector x, braid_Vector y);
int braidSum(braid_App app,
             paradae::Real alpha,
             braid_Vector x,
             paradae::Real beta,
             braid_Vector y);
int braidSpatialNorm(braid_App app, braid_Vector u, paradae::Real* norm_ptr);
int braidAccess(braid_App app, braid_Vector u, braid_AccessStatus astatus);
int braidBufSize(braid_App app, int* size_ptr, braid_BufferStatus bstatus);
int braidBufPack(braid_App app, braid_Vector u, void* buffer, braid_BufferStatus bstatus);
int braidBufUnpack(braid_App app, void* buffer, braid_Vector* u_ptr, braid_BufferStatus bstatus);
int braidTimeGrid(braid_App app, braid_Real* ta, braid_Int* ilower, braid_Int* iupper);

}  // namespace griddyn::braid
