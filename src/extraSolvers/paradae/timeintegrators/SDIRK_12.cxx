/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SDIRK_12.h"
namespace griddyn {
namespace paradae {
    SDIRK_12::SDIRK_12(Equation* eq, bool variable_step): RungeKutta_SDIRK(eq, variable_step)
    {
        nb_steps = 2;
        order = do_varstep ? 1 : 2;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_A(0, 0) = 1.;
        rk_A(0, 1) = 0;
        rk_A(1, 0) = -1.;
        rk_A(1, 1) = 1.;
        rk_b(0) = 1. / 2.;
        rk_b(1) = 1. / 2.;
        rk_c(0) = 1;
        rk_c(1) = 0;
        rk_binf(0) = 1;
        rk_binf(1) = 0;
    }
}  // namespace paradae
}  // namespace griddyn
