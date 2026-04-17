/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta_DIRK.h"

#include "../math/DBlockTriMatrix.h"
#include "../math/PMultiVector.h"
namespace griddyn::paradae {
RungeKutta_DIRK::RungeKutta_DIRK(Equation* eq, bool varstep): RungeKutta_Implicit(eq, varstep) {}

void RungeKutta_DIRK::InitArray()
{
    allK_previous.Resize(nb_steps, size_x);
    CurrentJacobian = new DBlockTriMatrix(nb_steps, size_x, dense_mat);
}

void RungeKutta_DIRK::SetDenseMatrix(bool dense_mat_)
{
    if (dense_mat != dense_mat_) {
        dense_mat = dense_mat_;
        delete CurrentJacobian;
        this->InitArray();
    }
}

Solver_App_RK* RungeKutta_DIRK::BuildSolverApp(Real t, Real dt, const Vector& x0)
{
    return new Solver_App_DIRK(rtol, atol, t + dt, dt, x0, this);
}

Solver_App_DIRK::Solver_App_DIRK(Real rtol,
                                 const Vector& atol,
                                 Real tn_,
                                 Real dt_,
                                 const Vector& x0_,
                                 RungeKutta_Implicit* rk_):
    Solver_App_IRK(rtol, atol, tn_, dt_, x0_, rk_)
{
}

void Solver_App_DIRK::EvaluateFunAndJac(const Vector& allK,
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

    require_jac = (require_jac && update_jacobian);

    for (int i = 0; i < nb_steps; i++) {
        xi.CopyData(this->x0);
        for (int j = 0; j <= i; j++) {
            pallK.GetPVector(j, Ki);
            xi.AXPBY(this->dt * rk_A(i, j), 1.0, Ki);
        }
        pgx.GetPVector(i, gi);
        equation->function(t + rk_c(i) * this->dt, xi, Ki, pstate, gi);

        if (require_jac) {
            Matrix* Ji;
            if (dense_mat)
                Ji = new DenseMatrix(size_x, Real(0.0));
            else
                Ji = new SparseMatrix(size_x, Real(0.0));
            equation->jacobian_ypcdy(t + rk_c(i) * this->dt, xi, Ki, pstate, 0, *Ji);
            for (int j = 0; j < i; j++)
                if (i != j) jacmat->SetSubMat(i * size_x, j * size_x, *Ji, this->dt * rk_A(i, j));
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
}  // namespace griddyn::paradae
