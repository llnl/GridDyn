/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExpMidPoint.h"
namespace griddyn {
namespace paradae {
    ExpMidPoint::ExpMidPoint(Equation* eq): RungeKutta_Explicit(eq, false)
    {
        nb_steps = 2;
        order = 2;
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        Real alpha = 1. / 2.;

        rk_A(1, 0) = alpha;
        rk_b(0) = 1 - 1. / (2. * alpha);
        rk_b(1) = 1. / (2. * alpha);
        rk_c(1) = alpha;
    }
}  // namespace paradae
}  // namespace griddyn
