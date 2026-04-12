/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta_SDIRK.h"

#include "../math/DBlockTriMatrix.h"
#include "../math/PMultiVector.h"
#include "../solvers/Newton.h"
#include <iostream>
namespace griddyn {
namespace paradae {
    using namespace std;

    RungeKutta_SDIRK::RungeKutta_SDIRK(Equation* eq, bool varstep): RungeKutta_Implicit(eq, varstep)
    {
    }

    void RungeKutta_SDIRK::InitArray()
    {
        allK_previous.Resize(nb_steps, size_x);
        if (this->UseDenseMatrix())
            CurrentJacobian = new DenseMatrix(size_x);
        else
            CurrentJacobian = new SparseMatrix(size_x);
    }

    void RungeKutta_SDIRK::SetDenseMatrix(bool dense_mat_)
    {
        if (dense_mat != dense_mat_) {
            dense_mat = dense_mat_;
            delete CurrentJacobian;
            this->InitArray();
        }
    }

    bool RungeKutta_SDIRK::SolveInnerSteps(Real t,
                                           Real used_dt,
                                           const Vector& x0,
                                           SMultiVector& allK)
    {
        bool success = true;
        PVector Ki, Kim1;
        Solver_App_SDIRK* app_sdirk = dynamic_cast<Solver_App_SDIRK*>(app);
        // Newton newton(100);

        if (app_sdirk == nullptr) {
            cerr << "Error when casting Solver_App_RK in SDIRK. This should not happen" << endl;
            abort();
        }
        for (int i = 0; i < nb_steps; i++) {
            allK.GetPVector(i, Ki);
            app_sdirk->SetSolverStep(i, allK, x0);
            if (i > 0) {
                allK.GetPVector(i - 1, Kim1);
                // Ki.CopyData(Kim1);
            }

            Ki.Fill(0);

            try {
                newton.Solve(app, Ki);
            }
            catch (...) {
                if (do_braid) {
                    allK.CopyData(allK_previous);
                    success = false;
                    return false;
                } else {
                    throw;
                }
            }
        }

        return success;
    }

    Solver_App_RK* RungeKutta_SDIRK::BuildSolverApp(Real t, Real dt, const Vector& x0)
    {
        return new Solver_App_SDIRK(rtol, atol, t + dt, dt, x0, this);
    }

    Solver_App_SDIRK::Solver_App_SDIRK(Real rtol,
                                       const Vector& atol,
                                       Real tn_,
                                       Real dt_,
                                       const Vector& x0_,
                                       RungeKutta_Implicit* rk_):
        Solver_App_IRK(rtol, atol, tn_, dt_, x0_, rk_)
    {
        current_step = 0;
        b.Resize(x0_.GetM());
    }

    void Solver_App_SDIRK::SetSolverStep(int i, SMultiVector& allK, const Vector& x0_)
    {
        if (i == 0)
            update_jacobian = true;
        else
            update_jacobian = false;

        current_step = i;
        PVector Kj;
        b.CopyData(x0_);
        for (int j = 0; j < i; j++) {
            allK.GetPVector(j, Kj);
            b.AXPBY(this->dt * rk_A(i, j), 1.0, Kj);
        }
    }

    void Solver_App_SDIRK::EvaluateFunAndJac(const Vector& Ki,
                                             Vector& gx,
                                             bool require_jac,
                                             bool factorize)
    {
        int size_x = x0.GetM();
        Real t = tn - dt;
        Matrix* jacmat = dynamic_cast<Matrix*>(GetCurrentJacobian());
        SVector xi(size_x);

        require_jac = (require_jac && update_jacobian);

        Real alpha = this->dt * rk_A(current_step, current_step);
        xi.CopyData(this->b);
        xi.AXPBY(alpha, 1.0, Ki);
        equation->function(t + rk_c(current_step) * this->dt, xi, Ki, pstate, gx);

        if (require_jac) {
            equation->jacobian_ypcdy(
                t + rk_c(current_step) * this->dt, xi, Ki, pstate, 1.0 / alpha, *jacmat);
            jacmat->operator*=(alpha);
            if (factorize) jacmat->Factorize();
        }
        // update_jacobian=true;
    }
}  // namespace paradae
}  // namespace griddyn
