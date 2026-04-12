/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ForwardEuler.h"
namespace griddyn {
namespace paradae {
    ForwardEuler::ForwardEuler(Equation* eq): RungeKutta_Explicit(eq, false)
    {
        nb_steps = 1;
        order = 1;
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_b(0) = Real(1);
    }
}  // namespace paradae
}  // namespace griddyn
