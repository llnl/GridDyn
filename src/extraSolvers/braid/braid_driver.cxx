/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "braid_driver.h"

#include "../paradae/common/Timer.h"
#include "../paradae/math/IVander.h"
#include "../paradae/math/IVanderExt.h"
#include "../paradae/timeintegrators/BackwardDiff.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#ifdef STATS_NEWTON
#    include "solvers/NewtonStats.h"
#endif

using griddyn::paradae::BackwardDiff;
using griddyn::paradae::DATA_Struct;
using griddyn::paradae::ODE;
using griddyn::paradae::PVector;
using griddyn::paradae::RCODE;
using griddyn::paradae::Real;
using griddyn::paradae::SMultiVector;
using griddyn::paradae::SVector;
using std::abort;
using std::abs;
using std::ceil;
using std::cerr;
using std::cout;
using std::endl;
using std::map;
using std::max;
using std::memcpy;
using std::min;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::round;
using std::setprecision;

_braid_App_struct::_braid_App_struct(ODEProblem* ode_):
    ode(ode_), nb_multisteps(ode->GetTI()->GetType() == BDF ? ode->GetTI()->GetOrder() : 1),
    size_x(ode->GetEq()->GetM()), size_state(ode->GetEq()->GetNState()), prevlvl(-1),
    solution_tfinal(nullptr),
    alloc_data(size_x, nb_multisteps, ode->GetEq()->GetNURoots(), size_state)
{
}

void _braid_App_struct::SetAllToDataStruct(braid_Vector u)
{
    PVector x0_u;
    u->xprev.GetPVector(0, x0_u);
    alloc_data.t = u->tprev(0);
    alloc_data.tprev.CopyData(u->tprev);
    alloc_data.xprev.CopyData(u->xprev);
    alloc_data.dxprev.CopyData(u->dxprev);
    if (ode->GetEq()->HasEvents())
        ode->GetEq()->root_functions(alloc_data.t, x0_u, u->dxprev, u->state, alloc_data.gprev);
    alloc_data.sprev.CopyData(u->state);
}

void _braid_App_struct::SetLastToDataStruct(braid_Vector u)
{
    int ns = u->tprev.GetSSize();
    PVector xl_u, x0_d;
    u->xprev.GetPVector(ns - 1, xl_u);
    alloc_data.xprev.GetPVector(0, x0_d);

    alloc_data.t = u->tprev(ns - 1);
    alloc_data.tprev(0) = u->tprev(ns - 1);
    x0_d.CopyData(xl_u);
    alloc_data.dxprev.CopyData(u->dxprev);
    if (ode->GetEq()->HasEvents())
        ode->GetEq()->root_functions(alloc_data.t, xl_u, u->dxprev, u->state, alloc_data.gprev);
    alloc_data.sprev.CopyData(u->state);
}

void _braid_App_struct::SetAllFromDataStruct(braid_Vector u)
{
    u->tprev.CopyData(alloc_data.tprev);
    u->xprev.CopyData(alloc_data.xprev);
    u->dxprev.CopyData(alloc_data.dxprev);
    u->state.CopyData(alloc_data.sprev);
}

void _braid_App_struct::SetLastFromDataStruct(braid_Vector u)
{
    int ns = u->tprev.GetSSize();
    PVector xl_u, x0_d;
    u->xprev.GetPVector(ns - 1, xl_u);
    alloc_data.xprev.GetPVector(0, x0_d);

    u->tprev(ns - 1) = alloc_data.t;
    xl_u.CopyData(x0_d);
    u->dxprev.CopyData(alloc_data.dxprev);
    u->state.CopyData(alloc_data.sprev);
}

void _braid_App_struct::DumpDataStruct()
{
    cout << "# t       = " << alloc_data.t << endl;
    cout << "# used_dt = " << alloc_data.used_dt << endl;
    cout << "# next_dt = " << alloc_data.next_dt << endl;
    cout << "# tprev   = " << alloc_data.tprev << endl;
    cout << "# xprev   = " << alloc_data.xprev << endl;
    cout << "# dxprev  = " << alloc_data.dxprev << endl;
    if (ode->GetEq()->HasEvents()) cout << "# gprev   = " << alloc_data.gprev << endl;
    cout << "# sprev   = " << alloc_data.sprev << endl;
}

namespace griddyn::braid {

void my_Step_OnOnePoint(braid_App app,
                        braid_Vector ustop,
                        braid_Vector fstop,
                        braid_Vector u,
                        braid_StepStatus status,
                        int level)
{
    Real tstart; /* current time */
    Real tstop; /* evolve to this time*/
    braid_StepStatusGetTstartTstop(status, &tstart, &tstop);

#ifdef STATS_NEWTON
    int nrefine, iter;
    braid_StepStatusGetNRefine(status, &nrefine);
    braid_StepStatusGetIter(status, &iter);
    newton_stats.SetIL(iter + nrefine, level);
#endif

    /* Set the correct order of the BDF method. It is BDF */
    int ns = app->nb_multisteps;
    BackwardDiff* bdf = nullptr;
    bdf = static_cast<BackwardDiff*>(app->ode->GetTI());
    if (ns != 1) bdf->SetOrder(1);

    /* Check for dt values and potential initial guess */
    bool have_a_ustop = false;
    SMultiVector ustop_tprev(ns, 1);
    if (ustop == u || ustop->tprev.GetM() == 0) {
        ustop_tprev.CopyData(u->tprev);
        for (int i = 0; i < ns; i++)
            ustop_tprev(i) += tstop - tstart;
    } else {
        ustop_tprev.CopyData(ustop->tprev);
        if (ustop->xprev.GetM() > 0) have_a_ustop = true;
    }

    /* Set the data structure */
    app->SetLastToDataStruct(u);

    /* Do the integration */
    Real t;
    Real refinement = 1;
    RCODE return_code;
    PVector xi;
    for (int i = 0; i < 1; i++) {
        t = ustop_tprev(ns - 1 - i);
        app->alloc_data.next_dt = t - app->alloc_data.t;

        /* Set up initial guess for arriving point */
        if (have_a_ustop) {
            ustop->xprev.GetPVector(ns - 1 - i, xi);
            app->alloc_data.xnext.CopyData(xi);
            if (i == 0) {
                app->alloc_data.dxnext.CopyData(ustop->dxprev);
            }
        } else {
            if (i == 0) {
                app->alloc_data.xprev.GetPVector(0, xi);
                app->alloc_data.xnext.CopyData(xi);
                app->alloc_data.dxnext.CopyData(app->alloc_data.dxprev);
            }
        }

        return_code = app->ode->GetTI()->AdvanceStep(app->alloc_data);

        if (t <= app->ode->GetEq()->GetTmax())
            refinement = max(refinement, app->alloc_data.used_dt / app->alloc_data.next_dt);

        if (return_code == WARN_ROOT && level == 0)
            cerr << "Warning, we passed a root during integration on level 0" << endl;

        app->alloc_data.Rotate();
    }

    /* Save new values in u */
    app->SetLastFromDataStruct(u);

    /* Reset the BDF order if it was modified */
    if (bdf->GetOrder() != ns) bdf->SetOrder(ns);

    /* Set the refinement if needed */
    if (level == 0 && app->ode->GetTI()->DoVarstep()) {
        if (refinement < 1.001) refinement = 1;
        braid_StepStatusSetRFactor(status, ceil(refinement));
    }
}

void my_Step_OnAllPoints(braid_App app,
                         braid_Vector ustop,
                         braid_Vector fstop,
                         braid_Vector u,
                         braid_StepStatus status,
                         int level)
{
    Real tstart; /* current time */
    Real tstop; /* evolve to this time*/
    braid_StepStatusGetTstartTstop(status, &tstart, &tstop);

#ifdef STATS_NEWTON
    int nrefine, iter;
    braid_StepStatusGetNRefine(status, &nrefine);
    braid_StepStatusGetIter(status, &iter);
    newton_stats.SetIL(iter + nrefine, level);
#endif

    /* Set the correct order of the BDF method if it is BDF */
    int ns = app->nb_multisteps;
    BackwardDiff* bdf = nullptr;
    if (app->bdf_strat != nobdf && level > 0) {
        int newq;
        if (app->bdf_strat == usual || app->bdf_strat == usual_c || app->bdf_strat == inject ||
            app->bdf_strat == inject_c || app->bdf_strat == extrap || app->bdf_strat == extrap_c)
            newq = max(app->min_order, ns - level * app->lowered_by_level);
        else
            newq = 1;
        bdf = static_cast<BackwardDiff*>(app->ode->GetTI());
        if (newq != ns) bdf->SetOrder(newq);
    }

    /* Check for dt values and potential initial guess */
    bool have_a_ustop = false;
    SMultiVector ustop_tprev(ns, 1);
    if (ustop == u || ustop->tprev.GetM() == 0) {
        ustop_tprev.CopyData(u->tprev);
        for (int i = 0; i < ns; i++)
            ustop_tprev(i) += tstop - tstart;
    } else {
        ustop_tprev.CopyData(ustop->tprev);
        if (ustop->xprev.GetM() > 0) have_a_ustop = true;
    }

    /* Set the data structure */
    app->SetAllToDataStruct(u);

    /* Do the integration (ns == 1 for RK) */
    Real t;
    Real refinement = 1;
    RCODE return_code;
    PVector xi;
    for (int i = 0; i < ns; i++) {
        t = ustop_tprev(ns - 1 - i);
        app->alloc_data.next_dt = t - app->alloc_data.t;

        /* Set up initial guess for arriving point */
        if (have_a_ustop) {
            ustop->xprev.GetPVector(ns - 1 - i, xi);
            app->alloc_data.xnext.CopyData(xi);
            if (i == 0) {
                app->alloc_data.dxnext.CopyData(ustop->dxprev);
            }
        } else {
            if (i == 0) {
                app->alloc_data.xprev.GetPVector(0, xi);
                app->alloc_data.xnext.CopyData(xi);
                app->alloc_data.dxnext.CopyData(app->alloc_data.dxprev);
            }
        }

        /* Take Step */
        return_code = app->ode->GetTI()->AdvanceStep(app->alloc_data);

        if (t <= app->ode->GetEq()->GetTmax())
            refinement = max(refinement, app->alloc_data.used_dt / app->alloc_data.next_dt);

        if (return_code == WARN_ROOT && level == 0) {
            const auto& roots = app->ode->GetEq()->GetRoots();
            const auto dist =
                min(app->alloc_data.troot - tstart, tstop - app->alloc_data.troot);
            if (dist > roots.tol) {
                app->alloc_data.SetNextAtRoot();
                app->alloc_data.Rotate();
                app->alloc_data.next_dt = tstop - app->alloc_data.t;
                if (app->ode->GetEq()->HasEvents()) {
                    app->ode->GetEq()->root_functions(app->alloc_data.t,
                                                      app->alloc_data.xprev,
                                                      app->alloc_data.dxprev,
                                                      app->alloc_data.sprev,
                                                      app->alloc_data.gprev);
                }
                return_code = app->ode->GetTI()->AdvanceStep(app->alloc_data);
                if (return_code == WARN_ROOT) {
                    const auto dist2 =
                        min(app->alloc_data.troot - app->alloc_data.t,
                            app->alloc_data.t + app->alloc_data.used_dt - app->alloc_data.troot);
                    if (dist2 > roots.tol) {
                        refinement = max(refinement, 2.0);
                    }
                }
            } else {
                refinement = min(refinement, app->ode->GetTI()->GetMaxRFactor());
            }
        }

        app->alloc_data.Rotate();
    }

    /* Save new values in u */
    app->SetAllFromDataStruct(u);

    /* Reset the BDF order if it was modified */
    if (app->bdf_strat != nobdf && level > 0) {
        if (bdf->GetOrder() != ns) bdf->SetOrder(ns);
    }

    /* Set the refinement if needed */
    if (level == 0 && app->ode->GetTI()->DoVarstep()) {
        if (refinement < 1.001) refinement = 1;
        braid_StepStatusSetRFactor(status, ceil(refinement));
    }
}

int my_Step(braid_App app,
            braid_Vector ustop,
            braid_Vector fstop,
            braid_Vector u,
            braid_StepStatus status)
{
#ifdef TIMER_BRAID
    global_timer.Start("bstep", "Braid Step", "brun");
#endif

    if (fstop != nullptr) {
        cout << "Non null fstop !" << endl;
    }

    int level;
    braid_StepStatusGetLevel(status, &level);

#ifdef DEBUG_STEP
    cout << "###### Debug int = " << debug_int << " ######" << endl;
    cout << "Case u=" << u << ", and ustop[" << ustop->tprev.GetM() << "]=" << ustop
         << ". LEVEL = " << level << endl;
    cout << " tprev = " << u->tprev << endl;
    cout << " state = " << u->state << endl;
    cout << " xprev = " << u->xprev << endl;
    cout << " dxprev= " << u->dxprev << endl;
    debug_int++;
#endif

    if (level == 0 || app->bdf_strat == nobdf || app->bdf_strat == usual ||
        app->bdf_strat == usual_c || app->bdf_strat == inject || app->bdf_strat == inject_c ||
        app->bdf_strat == extrap || app->bdf_strat == extrap_c ||
        ((app->bdf_strat == uni1 || app->bdf_strat == uni1_c) && level == 1)) {
        my_Step_OnAllPoints(app, ustop, fstop, u, status, level);
    } else {
        my_Step_OnOnePoint(app, ustop, fstop, u, status, level);
    }

#ifdef DEBUG_STEP
    cout << " tnext = " << u->tprev << endl;
    cout << " state = " << u->state << endl;
    cout << " xnext = " << u->xprev << endl;
    cout << " dxnext = " << u->dxprev << endl;
#endif

#ifdef TIMER_BRAID
    global_timer.Stop("bstep");
#endif
    return 0;
}

int my_SpatialRefine(braid_App app,
                     braid_Vector cu,
                     braid_Vector* fu_ptr,
                     braid_CoarsenRefStatus status)
{
    my_Clone(app, cu, fu_ptr);

    int ns = app->nb_multisteps;
    if (ns == 1) return 0;

    int level;
    braid_CoarsenRefStatusGetLevel(status, &level);

    if (((app->bdf_strat == uni0 || app->bdf_strat == uni0_c) && level == 0) ||
        ((app->bdf_strat == uni1 || app->bdf_strat == uni1_c) && level == 1)) {
        PVector xi, xl;
        (*fu_ptr)->xprev.GetPVector(ns - 1, xl);
        for (int i = 0; i < ns - 1; i++) {
            (*fu_ptr)->xprev.GetPVector(i, xi);
            xi.CopyData(xl);
        }
        (*fu_ptr)->dxprev.Fill(0);
        return 0;
    }

    if ((app->bdf_strat == uni0 || app->bdf_strat == uni0_c || app->bdf_strat == uni1 ||
         app->bdf_strat == uni1_c || app->bdf_strat == usual || app->bdf_strat == usual_c) &&
        level != -1)
        return 0;

    Real tstart, tstop_c, tstop_f, tprev_c, tprev_f;
    braid_CoarsenRefStatusGetTpriorTstop(status, &tstart, &tprev_f, &tstop_f, &tprev_c, &tstop_c);

    if (tstart == tstop_c) {
        tstop_c = tstart + (tstart - tprev_c);
        tstop_f = tstart + (tstart - tprev_f);
    }

    int rfactor = round((tstop_c - tstart) / (tstop_f - tstart));
    if (rfactor == 1) return 0;

#ifdef TIMER_BRAID
    global_timer.Start("srefine", "Spatial Refine", "brun");
#endif
    /*
cout << "Refining (rfactor = " << rfactor << ")" << endl;
cout << " Tprevc = " << tprev_c << endl;
cout << " Tprevf = " << tprev_f << endl;
cout << " Tstart = " << tstart << endl;
cout << " Tstopf = " << tstop_f << endl;
cout << " Tstopc = " << tstop_c << endl;
*/
    // Interpolate new times
    int n = app->nb_multisteps;
    Real current_dt, new_dt;
    current_dt = (*fu_ptr)->tprev(0) - (*fu_ptr)->tprev(1);
    new_dt = current_dt / rfactor;
    for (int i = 0; i < n; i++) {
        (*fu_ptr)->tprev(i) = (n - 1 - i) * new_dt + tstart;
    }

    // Interpolate vectors on new times, unless you are on the first point
    // In that case, integrate as in the Init function
    if (tstart == app->ode->GetEq()->GetT0()) {
        map<Real, my_Vector>::iterator init = app->initial_vector.find(new_dt);
        if (init != app->initial_vector.end()) {
            my_Free(app, *fu_ptr);
            my_Clone(app, &(init->second), fu_ptr);
        } else { /*
      cout << "###### Debug int = " << debug_int << " ######" << endl;
      cout << "Case cu=" << cu << ", and fu=" << *fu_ptr << endl;
      cout << " tprev = " << cu->tprev << endl;
      cout << " xprev = " << cu->xprev << endl;
      cout << " dxprev= " << cu->dxprev << endl;
      debug_int++;
     */
            Real t;
            PVector x0;
            (*fu_ptr)->xprev.GetPVector(n - 1, x0);
            DATA_Struct init_data(app->size_x, 1, app->ode->GetEq()->GetNURoots(), app->size_state);
            init_data.tprev(0) = tstart;
            init_data.xprev.CopyData(x0);
            init_data.xnext.CopyData(x0);
            init_data.dxprev.CopyData((*fu_ptr)->dxprev);
            init_data.dxnext.CopyData((*fu_ptr)->dxprev);
            init_data.sprev.CopyData((*fu_ptr)->state);

            for (int i = 1; i < n; i++) {
                t = (*fu_ptr)->tprev(n - 1 - i);
                init_data.next_dt = t - init_data.t;
                app->ode->GetTI()->AdvanceStep(init_data);
                init_data.Rotate();
            }

            (*fu_ptr)->tprev.CopyData(init_data.tprev);
            (*fu_ptr)->xprev.CopyData(init_data.xprev);
            (*fu_ptr)->dxprev.CopyData(init_data.dxprev);
            (*fu_ptr)->state.CopyData(init_data.sprev);
            app->initial_vector.insert(pair<Real, my_Vector>(new_dt, **fu_ptr));
            /*
        cout << " tprev = " << (*fu_ptr)->tprev << endl;
        cout << " xprev = " << (*fu_ptr)->xprev << endl;
        cout << " dxprev= " << (*fu_ptr)->dxprev << endl;
        */
        }
    } else {
        SVector dx = (*fu_ptr)->dxprev;
        SMultiVector new_xprev(n, app->size_x);
        switch (n) {
            case 2:
                iVan_2.Interp(
                    (*fu_ptr)->xprev, (*fu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 3:
                iVan_3.Interp(
                    (*fu_ptr)->xprev, (*fu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 4:
                iVan_4.Interp(
                    (*fu_ptr)->xprev, (*fu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 5:
                iVan_5.Interp(
                    (*fu_ptr)->xprev, (*fu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 6:
                iVan_6.Interp(
                    (*fu_ptr)->xprev, (*fu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            default:
                cerr << "Should not happen" << endl;
                abort();
        }
        (*fu_ptr)->xprev.CopyData(new_xprev);
        (*fu_ptr)->dxprev.CopyData(dx);
        // TODO(phlpt): Change the state.
    }

#ifdef TIMER_BRAID
    global_timer.Stop("srefine");
#endif
    return 0;
}

int my_SpatialCoarsen(braid_App app,
                      braid_Vector fu,
                      braid_Vector* cu_ptr,
                      braid_CoarsenRefStatus status)
{
    my_Clone(app, fu, cu_ptr);

    int ns = app->nb_multisteps;
    if (ns == 1) return 0;

    int level;
    braid_CoarsenRefStatusGetLevel(status, &level);

    if (app->bdf_strat == uni0 || app->bdf_strat == uni0_c || app->bdf_strat == uni1 ||
        app->bdf_strat == uni1_c || app->bdf_strat == usual || app->bdf_strat == usual_c)
        return 0;

    Real tstart, tstop_c, tstop_f, tprev_c, tprev_f;
    braid_CoarsenRefStatusGetTpriorTstop(status, &tstart, &tprev_f, &tstop_f, &tprev_c, &tstop_c);

    if (tstart == tstop_c) {
        tstop_c = tstart + (tstart - tprev_c);
        tstop_f = tstart + (tstart - tprev_f);
    }

    int rfactor = round((tstop_c - tstart) / (tstop_f - tstart));
    if (rfactor == 1) return 0;

#ifdef TIMER_BRAID
    global_timer.Start("scoarsen", "Spatial Coarsen", "brun");
#endif
    /*
cout << "Coarsening (rfactor = " << rfactor << ")" << endl;
cout << " Tprevc = " << tprev_c << endl;
cout << " Tprevf = " << tprev_f << endl;
cout << " Tstart = " << tstart << endl;
cout << " Tstopf = " << tstop_f << endl;
cout << " Tstopc = " << tstop_c << endl;
*/
    // Interpolate new times
    int n = app->nb_multisteps;
    Real current_dt, new_dt;
    current_dt = (*cu_ptr)->tprev(0) - (*cu_ptr)->tprev(1);
    new_dt = current_dt * rfactor;
    for (int i = 0; i < n; i++) {
        (*cu_ptr)->tprev(i) = (n - 1 - i) * new_dt + tstart;
    }

    if (app->bdf_strat == inject || app->bdf_strat == inject_c) {
        (*cu_ptr)->dxprev *= 1.0 / rfactor;
#ifdef TIMER_BRAID
        global_timer.Stop("scoarsen");
#endif
        return 0;
    }

    // Interpolate vectors on new times, unless you are on the first point
    // In that case, integrate as in the Init function
    if (tstart == app->ode->GetEq()->GetT0()) {
        map<Real, my_Vector>::iterator init = app->initial_vector.find(new_dt);
        if (init != app->initial_vector.end()) {
            my_Free(app, *cu_ptr);
            my_Clone(app, &(init->second), cu_ptr);
        } else { /*
      cout << "###### Debug int = " << debug_int << " ######" << endl;
      cout << "Case fu=" << fu << ", and cu=" << *cu_ptr << endl;
      cout << " tprev = " << fu->tprev << endl;
      cout << " xprev = " << fu->xprev << endl;
      cout << " dxprev= " << fu->dxprev << endl;
      debug_int++;
     */
            Real t;
            PVector x0;
            (*cu_ptr)->xprev.GetPVector(n - 1, x0);
            DATA_Struct init_data(app->size_x, 1, app->ode->GetEq()->GetNURoots(), app->size_state);
            init_data.tprev(0) = tstart;
            init_data.xprev.CopyData(x0);
            init_data.xnext.CopyData(x0);
            init_data.dxprev.CopyData((*cu_ptr)->dxprev);
            init_data.dxnext.CopyData((*cu_ptr)->dxprev);
            init_data.sprev.CopyData((*cu_ptr)->state);

            for (int i = 1; i < n; i++) {
                t = (*cu_ptr)->tprev(n - 1 - i);
                init_data.next_dt = t - init_data.t;
                app->ode->GetTI()->AdvanceStep(init_data);
                init_data.Rotate();
            }

            (*cu_ptr)->tprev.CopyData(init_data.tprev);
            (*cu_ptr)->xprev.CopyData(init_data.xprev);
            (*cu_ptr)->dxprev.CopyData(init_data.dxprev);
            (*cu_ptr)->state.CopyData(init_data.sprev);
            app->initial_vector.insert(pair<Real, my_Vector>(new_dt, **cu_ptr));
            /*
        cout << " tprev = " << (*cu_ptr)->tprev << endl;
        cout << " xprev = " << (*cu_ptr)->xprev << endl;
        cout << " dxprev= " << (*cu_ptr)->dxprev << endl;
        */
        }
    } else {
        SVector dx = (*cu_ptr)->dxprev;
        SMultiVector new_xprev(n, app->size_x);
        switch (n) {
            case 2:
                iVan_2.Interp(
                    (*cu_ptr)->xprev, (*cu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 3:
                iVan_3.Interp(
                    (*cu_ptr)->xprev, (*cu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 4:
                iVan_4.Interp(
                    (*cu_ptr)->xprev, (*cu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 5:
                iVan_5.Interp(
                    (*cu_ptr)->xprev, (*cu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            case 6:
                iVan_6.Interp(
                    (*cu_ptr)->xprev, (*cu_ptr)->dxprev, new_xprev, dx, new_dt, current_dt);
                break;
            default:
                cerr << "Should not happen" << endl;
                abort();
        }
        (*cu_ptr)->xprev.CopyData(new_xprev);
        (*cu_ptr)->dxprev.CopyData(dx);
        // TODO(phlpt): Change the state.
    }

#ifdef TIMER_BRAID
    global_timer.Stop("scoarsen");
#endif
    return 0;
}

int find_closest_idx(Real* ta, int n, Real t)
{
    Real m = abs(ta[0] - t);
    int i = 1;
    while (i < n) {
        if (abs(ta[i] - t) >= m)
            break;
        else
            m = abs(ta[i] - t);
        i++;
    }
    i--;
    return i;
}

int my_Init(braid_App app, Real t, braid_Vector* u_ptr)
{
#ifdef TIMER_BRAID
    global_timer.Start("binitv", "Init Vector", "brun");
#endif
    my_Vector* u = new my_Vector;

    Real dt = app->ode->GetEq()->GetTmax() / app->ode->GetEq()->GetNsteps();

    u->tprev.Resize(app->nb_multisteps, 1);
    u->xprev.Resize(app->nb_multisteps, app->size_x);
    u->dxprev.Resize(app->size_x);
    if (app->size_state > 0) u->state.Resize(app->size_state);

    int idx = find_closest_idx(app->grid_initial, app->nb_initial, t);

    if (t == app->ode->GetEq()->GetT0()) {
        DATA_Struct init_data(app->size_x, 1, app->ode->GetEq()->GetNURoots(), app->size_state);
        init_data.t = t;
        init_data.tprev(0) = t;
        PVector x0;
        init_data.xprev.GetPVector(0, x0);

        app->ode->GetEq()->init(t, x0);
        app->ode->GetEq()->root_init_state(t, init_data.sprev);

        for (int i = 1; i < app->nb_multisteps; i++) {
            init_data.next_dt = app->grid_initial[idx + 1] - app->grid_initial[idx];
            idx++;
            app->ode->GetTI()->AdvanceStep(init_data);
            init_data.Rotate();
        }
        u->tprev.CopyData(init_data.tprev);
        u->xprev.CopyData(init_data.xprev);
        u->dxprev.CopyData(init_data.dxprev);
        u->state.CopyData(init_data.sprev);
        app->initial_vector.insert(pair<Real, my_Vector>(dt, *u));
    } else {
        PVector xi;
        for (int i = app->nb_multisteps - 1; i >= 0; i--) {
            t = app->grid_initial[idx];
            u->xprev.GetPVector(i, xi);
            app->ode->GetEq()->init(t, xi);
            u->tprev(i) = t;
            idx++;
        }
        app->ode->GetEq()->root_init_state(t, u->state);
        u->xprev.GetPVector(app->nb_multisteps - 1, xi);
        if (app->ode->GetEq()->GetTypeEq() == ODE)
            app->ode->GetEq()->Get_dy_from_y(u->tprev(app->nb_multisteps - 1),
                                             xi,
                                             u->state,
                                             u->dxprev);
    }

    *u_ptr = u;

#ifdef TIMER_BRAID
    global_timer.Stop("binitv");
#endif
    return 0;
}

int my_InitShell(braid_App app, Real t, braid_Vector* u_ptr)
{
    my_Vector* u = new my_Vector;

    Real dt = app->ode->GetEq()->GetTmax() / app->ode->GetEq()->GetNsteps();

    u->tprev.Resize(app->nb_multisteps, 1);
    for (int i = app->nb_multisteps - 1; i >= 0; i--) {
        u->tprev(i) = t;
        t += dt;
    }
    if (app->size_state > 0) {
        u->state.Resize(app->size_state);
        app->ode->GetEq()->root_init_state(t, u->state);
    }

    *u_ptr = u;

    return 0;
}

int my_Clone(braid_App app, braid_Vector u, braid_Vector* v_ptr)
{
    my_Vector* v = new my_Vector;

    v->xprev = u->xprev;
    v->tprev = u->tprev;
    v->dxprev = u->dxprev;
    v->state = u->state;

    *v_ptr = v;

    return 0;
}

int my_CloneShell(braid_App app, braid_Vector u, braid_Vector* v_ptr)
{
    my_Vector* v = new my_Vector;

    v->tprev = u->tprev;
    v->state = u->state;

    *v_ptr = v;

    return 0;
}

int my_FreeShell(braid_App app, braid_Vector u)
{
    u->xprev.Free();
    u->dxprev.Free();
    return 0;
}

int my_Free(braid_App app, braid_Vector u)
{
    delete u;
    return 0;
}

int my_PropagateShell(braid_App app, braid_Vector x, braid_Vector y)
{
    y->state = x->state;
    return 0;
}

int my_Sum(braid_App app, Real alpha, braid_Vector x, Real beta, braid_Vector y)
{
    y->xprev.AXPBY(alpha, beta, x->xprev);
    y->dxprev.AXPBY(alpha, beta, x->dxprev);
    return 0;
}

int my_SpatialNorm(braid_App app, braid_Vector u, Real* norm_ptr)
{
    Real sqdot = 0;
    Real v;
    PVector xi;

    for (int i = 0; i < app->nb_multisteps; i++) {
        u->xprev.GetPVector(i, xi);
        v = xi.Norm2();
        sqdot += v * v;
    }
    sqdot = sqrt(sqdot);

    *norm_ptr = sqdot;

    return 0;
}

int my_Access(braid_App app, braid_Vector u, braid_AccessStatus astatus)
{
    int done;
    braid_AccessStatusGetDone(astatus, &done);
    if (done == 1) {
        Real t;
        braid_AccessStatusGetT(astatus, &t);

        if (t == app->ode->GetEq()->GetTmax()) {
            // fprintf(stderr, "\n\n  Braid: Saving final solution\n\n");
            my_Clone(app, u, &(app->solution_tfinal));
        }

        if (app->ode->PrintSolution()) {
            PVector xi;

            for (int i = 0; i < app->nb_multisteps; i++) {
                u->xprev.GetPVector(i, xi);
                *(app->ode->GetOutput()) << u->tprev(i) << " ";
                if (app->size_state > 0) *(app->ode->GetOutput()) << u->state << " ";
                xi.dump(*(app->ode->GetOutput()));
                *(app->ode->GetOutput()) << endl;
            }
        }
    } else {
        int level, caller;
        braid_AccessStatusGetLevel(astatus, &level);
        braid_AccessStatusGetCallingFunction(astatus, &caller);
        // cout << "Call access on lvl " << level << " from ";
        // cout << endl;
        app->prevlvl = level;
        if (app->ode->PrintAllITERSolution())  // && level == 0 && caller == braid_ASCaller_FAccess)
        {
            Real t;
            braid_AccessStatusGetT(astatus, &t);
            int iter;
            braid_AccessStatusGetIter(astatus, &iter);
            int ref;
            braid_AccessStatusGetNRefine(astatus, &ref);

            ostringstream name;
            name << "_ITER_" << iter << "_REF_" << ref << "_" << app->ode->GetOutputFilename();
            ofstream out;
            out.open(name.str().c_str(), ofstream::app);
            out << setprecision(20);

            PVector xi;
            int i0 = 0;
            if (((app->bdf_strat == uni0 || app->bdf_strat == uni0_c) && level > 0) ||
                ((app->bdf_strat == uni1 || app->bdf_strat == uni1_c) && level > 1))
                i0 = app->nb_multisteps - 1;

            for (int i = i0; i < app->nb_multisteps; i++) {
                u->xprev.GetPVector(i, xi);
                out << u->tprev(i) << " ";
                if (app->size_state > 0) out << u->state << " ";
                xi.dump(out);
                out << endl;
            }

            out.close();
        }
    }

    return 0;
}

int my_BufSize(braid_App app, int* size_ptr, braid_BufferStatus bstatus)
{
    *size_ptr = sizeof(Real) * ((app->nb_multisteps + 1) * (app->size_x + 1) + app->size_state);
    braid_BufferStatusSetSize(bstatus, *size_ptr);
    return 0;
}

int my_BufPack(braid_App app, braid_Vector u, void* buffer, braid_BufferStatus bstatus)
{
    Real* dbuffer = (Real*)buffer;
    int it = 0;
    int nx = app->size_x;
    int ns = app->nb_multisteps;
    int st = u->state.GetM();

    memcpy(dbuffer + it, u->tprev.GetData(), ns * sizeof(Real));
    it += ns;
    memcpy(dbuffer + it, u->xprev.GetData(), ns * nx * sizeof(Real));
    it += ns * nx;
    memcpy(dbuffer + it, u->dxprev.GetData(), nx * sizeof(Real));
    it += nx;
    *(dbuffer + it) = st;
    it += 1;
    if (st > 0) memcpy(dbuffer + it, u->state.GetData(), st * sizeof(Real));

    int size = sizeof(Real) * (ns + ns * nx + nx + 1 + st);
    braid_BufferStatusSetSize(bstatus, size);
    return 0;
}

int my_BufUnpack(braid_App app, void* buffer, braid_Vector* u_ptr, braid_BufferStatus bstatus)
{
    Real* dbuffer = (Real*)buffer;
    my_Vector* u = new my_Vector;
    int it = 0;
    int nx = app->size_x;
    int ns = app->nb_multisteps;
    int st;

    u->tprev.Resize(ns, 1);
    u->xprev.Resize(ns, nx);
    u->dxprev.Resize(nx);

    memcpy(u->tprev.GetData(), dbuffer + it, ns * sizeof(Real));
    it += ns;
    memcpy(u->xprev.GetData(), dbuffer + it, ns * nx * sizeof(Real));
    it += ns * nx;
    memcpy(u->dxprev.GetData(), dbuffer + it, nx * sizeof(Real));
    it += nx;
    st = *(dbuffer + it);
    it += 1;
    if (st > 0) {
        u->state.Resize(st);
        memcpy(u->state.GetData(), dbuffer + it, st * sizeof(Real));
    }

    *u_ptr = u;

    return 0;
}

int my_TimeGrid(braid_App app, /**< user-defined _braid_App structure */
                braid_Real* ta, /**< temporal grid on level 0 (slice per processor) */
                braid_Int* ilower, /**< lower time index value for this processor */
                braid_Int* iupper) /**< upper time index value for this processor */
{
    memcpy(ta, &(app->braid_grid_initial[*ilower]), (*iupper - *ilower + 1) * sizeof(Real));

    return 0;
}
}  // namespace griddyn::braid
