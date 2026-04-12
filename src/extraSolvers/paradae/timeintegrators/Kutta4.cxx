/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Kutta4.h"
namespace griddyn {
namespace paradae {
    Kutta4::Kutta4(Equation* eq): RungeKutta_Explicit(eq, false)
    {
        nb_steps = 4;
        order = 4;
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_A(1, 0) = Real(1. / 2.);
        rk_A(2, 1) = Real(1. / 2.);
        rk_A(3, 2) = Real(1.);
        rk_b(0) = Real(1. / 6.);
        rk_b(1) = Real(1. / 3.);
        rk_b(2) = Real(1. / 3.);
        rk_b(3) = Real(1. / 6.);
        rk_c(1) = Real(1. / 2.);
        rk_c(2) = Real(1. / 2.);
        rk_c(3) = Real(1.0);
    }
}  // namespace paradae
}  // namespace griddyn
