/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Solver.h"

namespace griddyn {
namespace paradae {
    using namespace std;

    void Solver_App::ApproxJacobian(const Vector& x, bool do_facto)
    {
        SVector fx(x.GetM(), 0.0);
        this->EvaluateFunAndJac(x, fx, false, false);
        this->ApproxJacobian(x, fx, do_facto);
    }

    void Solver_App::ApproxJacobian(const Vector& x, const Vector& fx, bool do_facto)
    {
        int n = x.GetM();
        Real h = x.Norm2() / (1000. * n);
        SVector xh(x);
        SVector fxh(n, 0.0);

        if (h < 1e-7) h = 1e-7;
        for (int j = 0; j < n; j++) {
            xh(j) += h;
            this->EvaluateFunAndJac(xh, fxh, false, false);
            fxh.AXPBY(-1.0 / h, 1.0 / h, fx);

            for (int i = 0; i < n; i++)
                CurrentJacobian->operator()(i, j) = fxh(i);
            xh(j) -= h;
        }
        if (do_facto) {
            CurrentJacobian->Factorize();
        }
    }
}  // namespace paradae
}  // namespace griddyn
