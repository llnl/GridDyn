/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Billington_23.h"
namespace griddyn {
namespace paradae {
    Billington_23::Billington_23(Equation* eq, bool variable_step):
        RungeKutta_SDIRK(eq, variable_step)
    {
        nb_steps = 3;
        order = do_varstep ? 2 : 3;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        Real sq2 = sqrt(2);

        rk_A(0, 0) = 1 - sq2 / 2.0;  // 0.292893218813;
        rk_A(1, 0) = -19 + 14 * sq2;  // 0.798989873223;
        rk_A(2, 0) = (53 - 5 * sq2) / 62.0;  // 0.740789228841;
        rk_A(1, 1) = 1 - sq2 / 2.0;  // 0.292893218813;
        rk_A(2, 1) = (9 + 5 * sq2) / 62.0;  // 0.259210771159;
        rk_A(2, 2) = 1 - sq2 / 2.0;  // 0.292893218813;
        rk_b(0) = (263 - 95 * sq2) / 186.0;  // 0.691665115992;
        rk_b(1) = (47 + 33 * sq2) / 186.0;  // 0.503597029883;
        rk_b(2) = (-2 + sq2) / 3.0;  //-0.195262145876;
        rk_binf(0) = (53 - 5 * sq2) / 62.0;  // 0.740789228840;
        rk_binf(1) = (9 + 5 * sq2) / 62.0;  // 0.259210771159;
        rk_c(0) = 1 - sq2 / 2.0;  // 0.292893218813;
        rk_c(1) = -18 + 27 * sq2 / 2.0;  // 1.091883092037;
        rk_c(2) = 2 - sq2 / 2.0;  // 1.292893218813;
    }
}  // namespace paradae
}  // namespace griddyn
