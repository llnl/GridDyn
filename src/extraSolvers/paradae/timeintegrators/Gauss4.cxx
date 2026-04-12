/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Gauss4.h"

#include <cmath>
namespace griddyn {
namespace paradae {
    Gauss4::Gauss4(Equation* eq): RungeKutta_Implicit(eq, false)
    {
        nb_steps = 2;
        order = 4;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        Real sq3s6 = sqrt(3.) / 6.;
        rk_A(0, 0) = Real(1. / 4.);
        rk_A(0, 1) = Real(1. / 4.) - sq3s6;
        rk_A(1, 0) = Real(1. / 4.) + sq3s6;
        rk_A(1, 1) = Real(1. / 4.);
        rk_b(0) = Real(1. / 2.);
        rk_b(1) = Real(1. / 2.);
        rk_c(0) = Real(1. / 2.) - sq3s6;
        rk_c(1) = Real(1. / 2.) + sq3s6;
    }
}  // namespace paradae
}  // namespace griddyn
