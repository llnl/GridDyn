/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "FE_ExpTrap_12.h"
namespace griddyn {
namespace paradae {
    FE_ExpTrap_12::FE_ExpTrap_12(Equation* eq, bool variable_step):
        RungeKutta_Explicit(eq, variable_step)
    {
        nb_steps = 2;
        order = do_varstep ? 1 : 2;

        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_A(1, 0) = Real(1.0);
        rk_b(0) = Real(1. / 2.);
        rk_b(1) = Real(1. / 2.);
        rk_binf(0) = Real(1.);
        rk_binf(1) = Real(0.);
        rk_c(1) = Real(1.);
    }
}  // namespace paradae
}  // namespace griddyn
