/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "BackwardEuler.h"

// 1 | 1
// ------
//   | 1
namespace griddyn {
namespace paradae {
    BackwardEuler::BackwardEuler(Equation* eq): RungeKutta_DIRK(eq, false)
    {
        nb_steps = 1;
        order = 1;
        this->InitArray();
        rk_A.Clone(DenseMatrix(nb_steps, Real(1.0)));
        rk_b.Resize(nb_steps, 1.0);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps, 1.0);
    }
}  // namespace paradae
}  // namespace griddyn
