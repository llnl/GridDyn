/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta_Implicit.h"

#include "../math/DenseMatrix.h"
#include "../math/PMultiVector.h"
#include "../math/SparseMatrix.h"
#include "../solvers/LineSearch.h"
#include "../solvers/Newton.h"
#include <cmath>

using namespace std;
namespace griddyn {
namespace paradae {
    RungeKutta_Implicit::RungeKutta_Implicit(Equation* eq, bool varstep): RungeKutta(eq, varstep)
    {
        CurrentJacobian = NULL;
    }

    void RungeKutta_Implicit::InitArray()
    {
        allK_previous.Resize(nb_steps, size_x);
        if (CurrentJacobian == NULL) {
            if (this->UseDenseMatrix())
                CurrentJacobian = new DenseMatrix(nb_steps * size_x);
            else
                CurrentJacobian = new SparseMatrix(nb_steps * size_x);
        }
    }

    void RungeKutta_Implicit::SetDenseMatrix(bool dense_mat_)
    {
        if (dense_mat != dense_mat_) {
            dense_mat = dense_mat_;
            delete CurrentJacobian;
            CurrentJacobian = NULL;
            this->InitArray();
        }
    }

    RungeKutta_Implicit::~RungeKutta_Implicit()
    {
        if (CurrentJacobian != NULL) delete CurrentJacobian;
    }

    Solver_App_RK* RungeKutta_Implicit::BuildSolverApp(Real t, Real dt, const Vector& x0)
    {
        return new Solver_App_IRK(rtol, atol, t + dt, dt, x0, this);
    }

    bool RungeKutta_Implicit::SolveInnerSteps(Real t,
                                              Real used_dt,
                                              const Vector& x0,
                                              SMultiVector& allK)
    {
        bool success = true;

        try {
            allK.CopyData(allK_previous);
            // Newton newton(100);
            newton.Solve(app, allK);
        }
        catch (...) {
            allK.CopyData(allK_previous);
            if (!do_braid) {
                LinearSearch solver2(1000);
                solver2.Solve(app, allK);
            } else {
                success = false;
                return false;
            }
        }
        allK_previous.CopyData(allK);
        return success;
    }

    Solver_App_IRK::Solver_App_IRK(Real rtol_,
                                   const Vector& atol_,
                                   Real tn_,
                                   Real dt_,
                                   const Vector& x0_,
                                   RungeKutta_Implicit* rk_): Solver_App_RK(rtol_, atol_, x0_, rk_)
    {
        tn = tn_;
        dt = dt_;
        x0 = x0_;
        rk_A.Clone(rk_->GetA());
        rk_c = rk_->GetC();
        equation = rk_->GetEq();
        CurrentJacobian = rk_->GetCurrentJac();
        update_jacobian = true;
        dense_mat = rk_->UseDenseMatrix();
    }

    void Solver_App_IRK::dump() const
    {
        cout << tn << " " << dt << " ";
    }

    Solver_App_IRK::~Solver_App_IRK() {}

    void Solver_App_IRK::EvaluateFunAndJac(const Vector& allK,
                                           Vector& gx,
                                           bool require_jac,
                                           bool factorize)
    {
        int nb_steps = rk_A.GetM();
        int size_x = x0.GetM();
        Real t = tn - dt;
        VirtualMatrix* jacmat = GetCurrentJacobian();
        PMultiVector pallK(allK, nb_steps, size_x);
        PMultiVector pgx(gx, nb_steps, size_x);
        SVector xi(size_x);
        PVector Ki, gi;

        // TODO : Add other conditions, every 50 iter...
        require_jac = (require_jac && update_jacobian);

        for (int i = 0; i < nb_steps; i++) {
            xi.CopyData(this->x0);
            for (int j = 0; j < nb_steps; j++) {
                pallK.GetPVector(j, Ki);
                xi.AXPBY(this->dt * rk_A(i, j), 1.0, Ki);
            }
            pgx.GetPVector(i, gi);
            pallK.GetPVector(i, Ki);
            equation->function(t + rk_c(i) * this->dt, xi, Ki, pstate, gi);

            if (require_jac) {
                Matrix* Ji;
                if (dense_mat)
                    Ji = new DenseMatrix(size_x, Real(0.0));
                else
                    Ji = new SparseMatrix(size_x, Real(0.0));
                equation->jacobian_ypcdy(t + rk_c(i) * this->dt, xi, Ki, pstate, 0, *Ji);
                for (int j = 0; j < nb_steps; j++)
                    if (i != j)
                        jacmat->SetSubMat(i * size_x, j * size_x, *Ji, this->dt * rk_A(i, j));
                if (rk_A(i, i) == 0) {
                    Matrix* Jii;
                    if (dense_mat)
                        Jii = new DenseMatrix(size_x, Real(0.0));
                    else
                        Jii = new SparseMatrix(size_x, Real(0.0));
                    equation->jacobian_ypcdy(t + rk_c(i) * this->dt, xi, Ki, pstate, 1, *Jii);
                    Ji->AXPBY(1.0, -1.0, *Jii);
                    delete Jii;
                } else {
                    equation->jacobian_ypcdy(
                        t + rk_c(i) * this->dt, xi, Ki, pstate, 1.0 / (this->dt * rk_A(i, i)), *Ji);
                    Ji->operator*=(this->dt * rk_A(i, i));
                }
                jacmat->SetSubMat(i * size_x, i * size_x, *Ji);
                delete Ji;
            }
        }
        if (require_jac && factorize) {
            jacmat->Factorize();
        }
        // update_jacobian=true;
    }
}  // namespace paradae
}  // namespace griddyn
