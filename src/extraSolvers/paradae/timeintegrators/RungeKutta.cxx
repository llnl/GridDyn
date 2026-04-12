/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "RungeKutta.h"

#include "../math/IPoly.h"
#include "../math/PVector.h"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace std;
namespace griddyn {
namespace paradae {
    RungeKutta::RungeKutta() {}

    RungeKutta::RungeKutta(Equation* eq, bool variable_step)
    {
        size_x = eq->GetM();
        equation = eq;
        do_varstep = variable_step;
    }

    void RungeKutta::show()
    {
        cout << "###################" << endl;
        cout << "###################" << endl;
        rk_A.dump(cout);
        cout << "###################" << endl;
        rk_b.dump(cout);
        cout << "###################" << endl;
        rk_binf.dump(cout);
        cout << "###################" << endl;
        rk_c.dump(cout);
        cout << "###################" << endl;
        cout << "###################" << endl;
    }

    RCODE RungeKutta::AdvanceStep(DATA_Struct& val, int iter_ref)
    {
        nb_steps_done++;
        PVector x0;
        val.xprev.GetPVector(0, x0);
        pstate = val.sprev;
        val.used_dt = val.next_dt;

        SMultiVector allK(
            nb_steps,
            size_x);  // TODO : put allK in the class, allocate in constr, reinit to 0 here.
        PVector Ki;
        // Set dxprev as a good initial guess for Ki
        for (int i = 0; i < nb_steps; i++) {
            allK.GetPVector(i, Ki);
            Ki.CopyData(val.dxprev);
        }

        app = this->BuildSolverApp(val.t, val.used_dt, x0);
        bool success_solver, root_crossed = false, success_error_test = false;
        success_solver = this->SolveInnerSteps(val.t, val.used_dt, x0, allK);
        if (success_solver) {
            val.xnext.CopyData(x0);
            for (int i = 0; i < nb_steps; i++) {
                allK.GetPVector(i, Ki);
                val.xnext.AXPBY(val.used_dt * rk_b(i), 1.0, Ki);
            }
            // WARNING : This is Really not sure ... True only if b_j == a_{n,j}
            val.dxnext.CopyData(Ki);

            root_crossed = CheckRoots(val);

            Real refinement = 1.0;
            success_error_test =
                this->EstimateNextStepSize(x0, val.xnext, allK, val.used_dt, refinement);
            val.next_dt = val.used_dt / refinement;
        }

        delete app;
        return this->PostStep(val, success_solver, root_crossed, success_error_test);
    }

    bool RungeKutta::EstimateNextStepSize(const Vector& x0,
                                          const Vector& x1,
                                          const SMultiVector& allK,
                                          const Real& used_dt,
                                          Real& refinement)
    {
        bool success = true;
        if (do_varstep) {
            SVector correction(x0);
            PVector Ki;
            for (int i = 0; i < nb_steps; i++) {
                allK.GetPVector(i, Ki);
                correction.AXPBY(used_dt * rk_binf(i), 1.0, Ki);
            }

            correction.AXPBY(1.0, -1.0, x1);
            Real err = app->XNorm(correction, x1);
            // err=correction.Norm2()/x1.Norm2()/rtol;
            refinement = pow(0.9 / err, 1. / Real(order + 1));
            refinement = 1.0 / refinement;
            refinement = min(max_rfactor, max(refinement, 1.0 / max_rfactor));

            if (err > 1) success = false;
        }
        return success;
    }

    Solver_App_RK* RungeKutta::BuildSolverApp(Real t, Real dt, const Vector& x0)
    {
        return new Solver_App_RK(rtol, atol, x0, this);
    }

    Solver_App_RK::Solver_App_RK(Real rtol_,
                                 const Vector& atol_,
                                 const Vector& x0_,
                                 RungeKutta* rk_)
    {
        rtol = rtol_;
        pstate = rk_->GetPState();
        if (atol_.GetM() == x0_.GetM()) {
            atol.Resize(rk_->GetNbSteps(), x0_.GetM());
            PVector atoli;
            for (int i = 0; i < rk_->GetNbSteps(); i++) {
                atol.GetPVector(i, atoli);
                atoli.CopyData(atol_);
            }
        } else {
            atol.Resize(1, atol_.GetM());
            atol.CopyData(atol_);
        }
    }

    Real Solver_App_RK::XNorm(const Vector& dx, const Vector& x) const
    {
        Real res = 0, w;
        for (int i = 0; i < dx.GetM(); i++) {
            if (atol.GetM() == 1)
                w = (atol)(0);
            else
                w = (atol)(i);
            w += rtol * abs(x(i));
            res += pow(dx(i) / max(w, 1e-15), 2);
        }
        return sqrt(res / dx.GetM());
    }

    Real Solver_App_RK::FxNorm(const Vector& fx) const
    {
        Real res = 0, w;
        for (int i = 0; i < fx.GetM(); i++) {
            if (atol.GetM() == 1)
                w = (atol)(0);
            else
                w = (atol)(i);
            res += pow(fx(i) / max(w, 1e-15), 2);
        }
        return sqrt(res / fx.GetM());
    }

    Real Solver_App_RK::XNorm(const Vector& dx, const Vector& x, Real tol_) const
    {
        Real res = 0, w;
        for (int i = 0; i < dx.GetM(); i++) {
            w = tol_ * (1 + abs(x(i)));
            res += pow(dx(i) / max(w, 1e-15), 2);
        }
        return sqrt(res / dx.GetM());
    }

    Real Solver_App_RK::FxNorm(const Vector& fx, Real tol_) const
    {
        Real res = 0, w;
        for (int i = 0; i < fx.GetM(); i++) {
            w = tol_;
            res += pow(fx(i) / max(w, 1e-15), 2);
        }
        return sqrt(res / fx.GetM());
    }
}  // namespace paradae
}  // namespace griddyn
