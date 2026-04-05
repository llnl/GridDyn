/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef RungeKutta_SDIRK_h
#define RungeKutta_SDIRK_h

#include "RungeKutta_Implicit.h"

// class SMultiVector;
namespace griddyn {
namespace paradae {
    class Solver_App_SDIRK: public Solver_App_IRK {
        int current_step;
        SVector b;

      public:
        Solver_App_SDIRK(Real rtol,
                         const Vector& atol,
                         Real t_,
                         Real dt_,
                         const Vector& x0_,
                         RungeKutta_Implicit* rk_);
        void SetSolverStep(int i, SMultiVector& allK, const Vector& x0);
        virtual ~Solver_App_SDIRK() {};
        virtual void
            EvaluateFunAndJac(const Vector& allK, Vector& gx, bool require_jac, bool factorize);
    };

    class RungeKutta_SDIRK: public RungeKutta_Implicit {
      protected:
        virtual void InitArray();

      public:
        RungeKutta_SDIRK(Equation* eq, bool varstep = false);
        virtual void SetDenseMatrix(bool dense_mat_ = true);
        virtual Solver_App_RK* BuildSolverApp(Real t, Real dt, const Vector& x0);
        virtual bool SolveInnerSteps(Real t, Real used_dt, const Vector& x0, SMultiVector& allK);
        virtual ~RungeKutta_SDIRK() {};
    };
}  // namespace paradae
}  // namespace griddyn

#endif
