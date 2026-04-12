/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ImpMidPoint.h"
namespace griddyn {
namespace paradae {
    ImpMidPoint::ImpMidPoint(Equation* eq): RungeKutta_DIRK(eq, false)
    {
        nb_steps = 1;
        order = 2;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(1. / 2.)));
        rk_b.Resize(nb_steps, 1.0);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps, 0.5);
    }
}  // namespace paradae
}  // namespace griddyn
