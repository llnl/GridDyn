/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "LineSearch.h"

#include <cmath>
#include <iostream>

namespace griddyn {
namespace paradae {
    using namespace std;

    LinearSearch::LinearSearch(int max_iter_)
    {
        max_iter = max_iter_;
    }

    int LinearSearch::Solve(Solver_App* app, Vector& x)
    {
        int n = x.GetM();

        SVector fx(n, 0.0), next_fx(n, 0.0);
        SVector dx(n, 0.0), next_x(n, 0.0);
        Real residual, next_residual, pnext_residual;
        Real alpha, diff;
        int it = 0, it_int;

        app->EvaluateFunAndJac(x, fx, true, false);

        residual = app->FxNorm(fx);

        while (residual > 1 && it < max_iter) {
            dx = fx;
            app->GetCurrentJacobian()->MatMult(dx, true);
            alpha = 2.0;
            it_int = 0;

            // next_x=x-alpha*dx;
            next_x = x;
            next_x.AXPBY(-alpha, 1.0, dx);

            app->EvaluateFunAndJac(next_x, next_fx, true, false);

            next_residual = app->FxNorm(fx);
            diff = -1;
            pnext_residual = next_residual;
            while ((next_residual > residual || diff < 0) && it_int < max_iter_int) {
                alpha *= 0.9;
                // next_x=x-alpha*dx;
                next_x = x;
                next_x.AXPBY(-alpha, 1.0, dx);

                app->EvaluateFunAndJac(next_x, next_fx, true, false);
                next_residual = app->FxNorm(fx);
                diff = next_residual - pnext_residual;
                pnext_residual = next_residual;
                it_int++;
            }

            x.CopyData(next_x);
            fx = next_fx;
            residual = next_residual;
            it++;
        }

        if (residual > 1) {
            cerr << "LineSearch exit on failure : res = " << residual << endl;
            throw LS_NOT_CONVERGED;
        } else if (std::isnan(residual) || std::isinf(residual))
            throw LS_INF_NAN;

        return it;
    }
}  // namespace paradae
}  // namespace griddyn
