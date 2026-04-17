#pragma once

/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta.h"
namespace griddyn::paradae {
    class RungeKutta_Explicit: public RungeKutta {
      public:
        RungeKutta_Explicit(Equation* eq, bool varstep);
        ~RungeKutta_Explicit() {};
        virtual bool SolveInnerSteps(Real t, Real used_dt, const Vector& x0, SMultiVector& allK);
    };
}  // namespace griddyn::paradae
