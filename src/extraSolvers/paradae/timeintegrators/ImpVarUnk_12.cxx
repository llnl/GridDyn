/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ImpVarUnk_12.h"
namespace griddyn {
namespace paradae {
    ImpVarUnk_12::ImpVarUnk_12(Equation* eq, bool variable_step): RungeKutta_DIRK(eq, variable_step)
    {
        nb_steps = 4;
        order = do_varstep ? 1 : 2;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_A(0, 0) = 1.;
        rk_A(0, 1) = 0;
        rk_A(0, 2) = 0;
        rk_A(0, 3) = 0;
        rk_A(1, 0) = 1.;
        rk_A(1, 1) = 0;
        rk_A(1, 2) = 0;
        rk_A(1, 3) = 0;
        rk_A(2, 0) = -88. / 45.;
        rk_A(2, 1) = 0;
        rk_A(2, 2) = 12. / 5.;
        rk_A(2, 3) = 0;
        rk_A(3, 0) = -407. / 75.;
        rk_A(3, 1) = 0;
        rk_A(3, 2) = 144. / 25.;
        rk_A(3, 3) = 0;
        rk_b(0) = 1. / 10.;
        rk_b(1) = 0;
        rk_b(2) = 9. / 10.;
        rk_b(3) = 0;
        rk_c(0) = 1;
        rk_c(1) = 1;
        rk_c(2) = 4. / 9.;
        rk_c(3) = 1. / 3.;
        rk_binf(0) = 1;
        rk_binf(1) = 0;
        rk_binf(2) = 0;
        rk_binf(3) = 0;
    }
}  // namespace paradae
}  // namespace griddyn
