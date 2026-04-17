/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Newton.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

#ifdef DEBUG_NEWTON
#    include <math/PVector.h>
#    include <math/SMultiVector.h>
#endif

#ifdef STATS_NEWTON
#    include "solvers/NewtonStats.h"
#endif

namespace griddyn::paradae {
using namespace std;

Newton::Newton(int max_iter_, Real tol_, bool update)
{
    max_iter = max_iter_;
    tol = tol_;
    newton_always_update_jac = update;
}

int Newton::Solve(Solver_App* app, Vector& x)
{
    int n = x.GetM();
    SVector fdx(n, 0.0);
    Real residual_dx = 2, residual_fx = 2;
    // Real R=20./21., ndx1=0;

    int it = 0;
    app->EvaluateFunAndJac(x, fdx, true, true);

    if (tol > 0)
        residual_fx = app->FxNorm(fdx, tol);
    else
        residual_fx = app->FxNorm(fdx);

#ifdef DEBUG_NEWTON
    Real* history_dx = new Real[max_iter + 1];
    Real* history_fx = new Real[max_iter + 1];
    SMultiVector history_x(x);
    history_dx[it] = residual_dx;
    history_fx[it] = residual_fx;
#endif

    while (residual_fx > 1 && it < max_iter) {
        app->GetCurrentJacobian()->Solve(fdx);
        x.AXPBY(-1.0, 1.0, fdx);
        if (tol > 0)
            residual_dx = app->XNorm(fdx, x, tol);
        else
            residual_dx = app->XNorm(fdx, x);

        app->EvaluateFunAndJac(x, fdx, newton_always_update_jac, true);
        if (tol > 0)
            residual_fx = app->FxNorm(fdx, tol);
        else
            residual_fx = app->FxNorm(fdx);

        /*
  if (it==0)
ndx1=residual_dx;
  else
R=pow(residual_dx/ndx1,1.0/it);
  */
        it++;

#ifdef DEBUG_NEWTON
        history_dx[it] = residual_dx;
        history_fx[it] = residual_fx;
        history_x.PushBack(x);
#endif
    }

#ifdef STATS_NEWTON
    newton_stats.Add(it);
#endif

    if (std::isnan(residual_fx) || std::isnan(residual_dx) || std::isinf(residual_fx) ||
        std::isinf(residual_dx)) {
        cerr << "Newton exit on BAD failure : res_fx = " << residual_fx
             << ", res_dx = " << residual_dx << endl;
#ifdef DEBUG_NEWTON
        ofstream file;
        file.open("newton_hist");
        file << setprecision(20);
        PVector hi;
        for (int i = 0; i < it; i++) {
            history_x.GetPVector(i, hi);
            file << i << " " << history_dx[i] << " " << history_fx[i] << " " << hi << endl;
        }
        file.close();
        delete[] history_dx;
        delete[] history_fx;
        app->GetCurrentJacobian()->dump("newton_mat.dat");
        abort();
#endif
        throw NEWTON_INF_NAN;
    } else if (it >= max_iter && residual_fx >= 1) {
        cerr << "Newton exit on failure : res_fx = " << residual_fx << ", res_dx = " << residual_dx
             << endl;
#ifdef DEBUG_NEWTON
        ofstream file;
        file.open("newton_hist");
        file << setprecision(20);
        PVector hi;
        for (int i = 0; i < it; i++) {
            history_x.GetPVector(i, hi);
            file << i << " " << history_dx[i] << " " << history_fx[i] << " " << hi << endl;
        }
        file.close();
        delete[] history_dx;
        delete[] history_fx;
        app->GetCurrentJacobian()->dump("newton_mat.dat");
        abort();
#endif
        throw NEWTON_NOT_CONVERGED;
    } else {
#ifdef DEBUG_NEWTON
        delete[] history_dx;
        delete[] history_fx;
#endif
    }

    return it;
}
}  // namespace griddyn::paradae
