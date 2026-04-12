/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Gauss6.h"

#include <cmath>
namespace griddyn {
namespace paradae {
    Gauss6::Gauss6(Equation* eq): RungeKutta_Implicit(eq, false)
    {
        nb_steps = 3;
        order = 6;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        Real sq15 = sqrt(15.);
        rk_A(0, 0) = Real(5. / 36.);
        rk_A(0, 1) = Real(2. / 9.) - sq15 / 15.;
        rk_A(0, 2) = Real(5. / 36.) - sq15 / 30.;
        rk_A(1, 0) = Real(5. / 36.) + sq15 / 24.;
        rk_A(1, 1) = Real(2. / 9.);
        rk_A(1, 2) = Real(5. / 36.) - sq15 / 24.;
        rk_A(2, 0) = Real(5. / 36.) + sq15 / 30.;
        rk_A(2, 1) = Real(2. / 9.) + sq15 / 15.;
        rk_A(2, 2) = Real(5. / 36.);
        rk_b(0) = Real(5. / 18.);
        rk_b(1) = Real(4. / 9.);
        rk_b(2) = Real(5. / 18.);
        rk_c(0) = Real(1. / 2.) - sq15 / 10.;
        rk_c(1) = Real(1. / 2.);
        rk_c(2) = Real(1. / 2.) + sq15 / 10.;
    }
}  // namespace paradae
}  // namespace griddyn
