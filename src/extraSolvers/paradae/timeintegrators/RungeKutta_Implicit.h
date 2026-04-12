/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RUNGEKUTTA_IMPLICIT_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RUNGEKUTTA_IMPLICIT_H_

#include "../math/PVector.h"
#include "../math/SMultiVector.h"
#include "RungeKutta.h"
namespace griddyn {
namespace paradae {
    class RungeKutta_Implicit;

    class Solver_App_IRK: public Solver_App_RK {
      protected:
        Real tn, dt;
        PVector x0;
        DenseMatrix rk_A;
        PVector rk_c;
        Equation* equation;

      public:
        Solver_App_IRK(Real rtol,
                       const Vector& atol,
                       Real tn_,
                       Real dt_,
                       const Vector& x0_,
                       RungeKutta_Implicit* rk_);
        virtual ~Solver_App_IRK();
        virtual void
            EvaluateFunAndJac(const Vector& allK, Vector& gx, bool require_jac, bool factorize);
        virtual void dump() const;
    };

    class RungeKutta_Implicit: public RungeKutta {
      protected:
        VirtualMatrix* CurrentJacobian;
        virtual void InitArray();

      public:
        SMultiVector allK_previous;
        RungeKutta_Implicit(Equation* eq, bool varstep);
        virtual ~RungeKutta_Implicit();
        virtual bool SolveInnerSteps(Real t, Real used_dt, const Vector& x0, SMultiVector& allK);
        virtual Solver_App_RK* BuildSolverApp(Real t, Real dt, const Vector& x0);
        virtual VirtualMatrix* GetCurrentJac() { return CurrentJacobian; };
        virtual void SetDenseMatrix(bool dense_mat_ = true);
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_TIMEINTEGRATORS_RUNGEKUTTA_IMPLICIT_H_
