/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "DormPrince_45.h"
namespace griddyn {
namespace paradae {
    DormPrince_45::DormPrince_45(Equation* eq, bool variable_step):
        RungeKutta_Explicit(eq, variable_step)
    {
        nb_steps = 7;
        order = do_varstep ? 4 : 5;
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        rk_A(1, 0) = Real(0.2);
        rk_A(2, 0) = Real(3. / 40.);
        rk_A(2, 1) = Real(9. / 40.);
        rk_A(3, 0) = Real(44. / 45.);
        rk_A(3, 1) = Real(-56. / 15.);
        rk_A(3, 2) = Real(32. / 9.);
        rk_A(4, 0) = Real(19372. / 6561.);
        rk_A(4, 1) = Real(-25360. / 2187.);
        rk_A(4, 2) = Real(64448. / 6561.);
        rk_A(4, 3) = Real(-212. / 729.);
        rk_A(5, 0) = Real(9017. / 3168.);
        rk_A(5, 1) = Real(-355. / 33.);
        rk_A(5, 2) = Real(46732. / 5247.);
        rk_A(5, 3) = Real(49. / 176.);
        rk_A(5, 4) = Real(-5103. / 18656.);
        rk_A(6, 0) = Real(35. / 384.);
        rk_A(6, 1) = Real(0.);
        rk_A(6, 2) = Real(500. / 1113.);
        rk_A(6, 3) = Real(125. / 192.);
        rk_A(6, 4) = Real(-2187. / 6784.);
        rk_A(6, 5) = Real(11. / 84.);
        rk_b(0) = Real(35. / 384.);
        rk_b(1) = Real(0.);
        rk_b(2) = Real(500. / 1113.);
        rk_b(3) = Real(125. / 192.);
        rk_b(4) = Real(-2187. / 6784.);
        rk_b(5) = Real(11. / 84.);
        rk_binf(0) = Real(5179. / 57600.);
        rk_binf(1) = Real(0.);
        rk_binf(2) = Real(7571. / 16695.);
        rk_binf(3) = Real(393. / 640.);
        rk_binf(4) = Real(-92097. / 339200.);
        rk_binf(5) = Real(187. / 2100.);
        rk_binf(6) = Real(1. / 40.);
        rk_c(1) = 0.2;
        rk_c(2) = 0.3;
        rk_c(3) = 0.8;
        rk_c(4) = Real(8. / 9.);
        rk_c(5) = 1.0;
        rk_c(6) = 1.0;
    }
}  // namespace paradae
}  // namespace griddyn
