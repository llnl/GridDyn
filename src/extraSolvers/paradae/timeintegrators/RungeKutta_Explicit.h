/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RUNGEKUTTA_EXPLICIT_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RUNGEKUTTA_EXPLICIT_H_

#include "RungeKutta.h"
namespace griddyn {
namespace paradae {
    class RungeKutta_Explicit: public RungeKutta {
      public:
        RungeKutta_Explicit(Equation* eq, bool varstep);
        ~RungeKutta_Explicit() {};
        virtual bool SolveInnerSteps(Real t, Real used_dt, const Vector& x0, SMultiVector& allK);
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RUNGEKUTTA_EXPLICIT_H_
