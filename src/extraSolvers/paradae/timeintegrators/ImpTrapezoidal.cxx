/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ImpTrapezoidal.h"

// 0 | 0
// 1 | 1/2  1/2
// -------------
//   | 1/2  1/2
namespace griddyn {
namespace paradae {
    ImpTrapezoidal::ImpTrapezoidal(Equation* eq): RungeKutta_DIRK(eq, false)
    {
        nb_steps = 2;
        order = 2;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps, 0.5);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);
        rk_A(1, 0) = 1. / 2.;
        rk_A(1, 1) = 1. / 2.;
        rk_c(1) = 1.0;
    }
}  // namespace paradae
}  // namespace griddyn
