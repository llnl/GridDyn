/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta_Explicit.h"

#include "../math/PVector.h"
namespace griddyn {
namespace paradae {
    RungeKutta_Explicit::RungeKutta_Explicit(Equation* eq, bool varstep): RungeKutta(eq, varstep) {}

    bool RungeKutta_Explicit::SolveInnerSteps(Real t,
                                              Real used_dt,
                                              const Vector& x0,
                                              SMultiVector& allK)
    {
        SVector xi(x0);
        PVector Kj;

        for (int i = 0; i < nb_steps; i++) {
            xi.CopyData(x0);
            for (int j = 0; j < i; j++) {
                allK.GetPVector(j, Kj);
                xi.AXPBY(used_dt * rk_A(i, j), 1.0, Kj);
            }

            allK.GetPVector(i, Kj);
            equation->Get_dy_from_y(t + rk_c(i) * used_dt, xi, pstate, Kj);
        }

        return true;
    }
}  // namespace paradae
}  // namespace griddyn
