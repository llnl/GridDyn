/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RungeKutta_Implicit.h"
namespace griddyn::paradae {
    class Solver_App_DIRK: public Solver_App_IRK {
      public:
        Solver_App_DIRK(Real rtol,
                        const Vector& atol,
                        Real t_,
                        Real dt_,
                        const Vector& x0_,
                        RungeKutta_Implicit* rk_);
        virtual ~Solver_App_DIRK() {};
        virtual void
            EvaluateFunAndJac(const Vector& allK, Vector& gx, bool require_jac, bool factorize);
    };

    class RungeKutta_DIRK: public RungeKutta_Implicit {
      protected:
        virtual void InitArray();

      public:
        RungeKutta_DIRK(Equation* eq, bool varstep = false);
        virtual void SetDenseMatrix(bool dense_mat_ = true);
        virtual Solver_App_RK* BuildSolverApp(Real t, Real dt, const Vector& x0);
        virtual ~RungeKutta_DIRK() {};
    };
}  // namespace griddyn::paradae
