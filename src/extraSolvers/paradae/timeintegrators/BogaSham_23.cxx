/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "BogaSham_23.h"

// 0    | 0
// 0.5  | 0.5  0
// 0.75 | 0    3/4  0
// 1    | 2/9  1/3  4/9  0
// --------------------------
//      | 2/9  1/3  4/9  0
//      | 7/24 1/4  1/3  1/8
namespace griddyn {
namespace paradae {
    BogaSham_23::BogaSham_23(Equation* eq, bool variable_step):
        RungeKutta_Explicit(eq, variable_step)
    {
        nb_steps = 4;
        order = do_varstep ? 2 : 3;
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_A(1, 0) = Real(1. / 2.);
        rk_A(2, 1) = Real(3. / 4.);
        rk_A(3, 0) = Real(2. / 9.);
        rk_A(3, 1) = Real(1. / 3.);
        rk_A(3, 2) = Real(4. / 9.);
        rk_b(0) = Real(2. / 9.);
        rk_b(1) = Real(1. / 3.);
        rk_b(2) = Real(4. / 9.);
        rk_b(3) = Real(0.);
        rk_binf(0) = Real(7. / 24.);
        rk_binf(1) = Real(1. / 4.);
        rk_binf(2) = Real(1. / 3.);
        rk_binf(3) = Real(1. / 8.);
        rk_c(1) = Real(1. / 2.);
        rk_c(2) = Real(3. / 4.);
        rk_c(3) = Real(1.);
    }
}  // namespace paradae
}  // namespace griddyn
