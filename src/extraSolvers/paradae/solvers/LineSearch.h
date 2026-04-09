/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_SOLVERS_LINESEARCH_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_SOLVERS_LINESEARCH_H_

#include "../common/def.h"
#include "../math/Vector.h"
#include "Solver.h"

namespace griddyn {
namespace paradae {
    enum LS_error { LS_NOT_CONVERGED, LS_INF_NAN };

    class LinearSearch: Solver {
        static const int max_iter_int = 100;

      public:
        LinearSearch() {};
        LinearSearch(int max_iter_);
        int Solve(Solver_App* app, Vector& x);
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_SOLVERS_LINESEARCH_H_
