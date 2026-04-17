#pragma once

/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "../common/def.h"
#include "Solver.h"

namespace griddyn::paradae {
    enum NWT_error { NEWTON_NOT_CONVERGED, NEWTON_INF_NAN };

    class Newton: Solver {
        Real tol;
        bool newton_always_update_jac;

      public:
        Newton()
        {
            tol = -1;
            newton_always_update_jac = false;
        };
        Newton(int max_iter_, Real tol_ = -1, bool update = false);
        int Solve(Solver_App* app, Vector& x);
    };
}  // namespace griddyn::paradae
