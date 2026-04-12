/*
 * LLNS Copyright Start
 * Copyright (c) 2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#include "Ralston.h"
namespace griddyn {
namespace paradae {
    Ralston::Ralston(Equation* eq): RungeKutta_Explicit(eq, false)
    {
        nb_steps = 2;
        order = 2;
        rk_A.Clone(DenseMatrix(nb_steps, Real(0.0)));
        rk_b.Resize(nb_steps);
        rk_binf.Resize(nb_steps);
        rk_c.Resize(nb_steps);

        Real alpha = 2. / 3.;

        rk_A(1, 0) = alpha;
        rk_b(0) = 1 - 1. / (2. * alpha);
        rk_b(1) = 1. / (2. * alpha);
        rk_c(1) = alpha;
    }
}  // namespace paradae
}  // namespace griddyn
