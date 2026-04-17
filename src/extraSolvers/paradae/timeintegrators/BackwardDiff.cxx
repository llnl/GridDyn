/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "BackwardDiff.h"

#include "../math/DenseMatrix.h"
#include "../math/IPoly.h"
#include "../math/SparseMatrix.h"
#include "../solvers/LineSearch.h"
#include "../solvers/Newton.h"
#include "AllTimeIntegrators.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

namespace griddyn::paradae {
using namespace std;

BackwardDiff::BackwardDiff() {}

BackwardDiff::BackwardDiff(int type, Equation* eq, bool variable_step, bool force_FLC)
{
    size_x = eq->GetM();
    equation = eq;
    do_varstep = variable_step;
    use_dx_as_unknown = true;
    // If we want variable time steps, we need fixed-leading coefficient
    // for error estimates. Otherwise, use fully variable BDF for stability issues
    fullyvariable = !do_varstep && !force_FLC;

    CurrentJacobian = NULL;

    this->SetOrder(type);
}

void BackwardDiff::show()
{
    cout << "###################" << endl;
    cout << "###################" << endl;
    BDF_a.dump(cout);
    cout << "###################" << endl;
    cout << BDF_b << endl;
    ;
    cout << "###################" << endl;
    cout << "###################" << endl;
}

void BackwardDiff::InitArray()
{
    if (CurrentJacobian == NULL) {
        if (this->UseDenseMatrix())
            CurrentJacobian = new DenseMatrix(size_x);
        else
            CurrentJacobian = new SparseMatrix(size_x);
    }
}

void BackwardDiff::SetDenseMatrix(bool dense_mat_)
{
    if (dense_mat != dense_mat_) {
        dense_mat = dense_mat_;
        delete CurrentJacobian;
        CurrentJacobian = NULL;
        this->InitArray();
    }
}

BackwardDiff::~BackwardDiff()
{
    if (CurrentJacobian != NULL) delete CurrentJacobian;
}

string BackwardDiff::GetName()
{
    ostringstream s;
    s << "BDF_" << order;
    return s.str();
}

RCODE BackwardDiff::AdvanceStep(DATA_Struct& val, int iter_ref)
{
    nb_steps_done++;
    pstate = val.sprev;
    val.used_dt = val.next_dt;
    Real tn = val.t + val.used_dt;

    int q = this->GetOrder();

    if (val.tprev.GetSSize() < q) {
        cout << "Using RK" << endl;
        TimeIntegrator* RK;
        switch (q) {
            case 2:
                RK = new ImpMidPoint(equation);
                break;
            case 3:
                RK = new Billington_23(equation);
                break;
            case 4:
                RK = new Cash_34(equation, do_varstep);
                break;
            case 5:
                RK = new Fudziah_45(equation, do_varstep);
                break;
            case 6:
                RK = new Gauss6(equation);
                break;
            default:
                cerr << "Bad BDF order, this should never happen" << endl;
                abort();
        }
        RK->SetRTol(rtol);
        RK->SetATol(atol);
        RK->SetDenseMatrix(dense_mat);
        RK->DoBraid() = true;
        RK->CopyNewtonSolverOpt(*this);
        RK->SetMaxRFactor(max_rfactor);
        RCODE return_code = RK->AdvanceStep(val);
        val.tprev.PushBack(0.);
        val.xprev.PushBack(val.xnext);
        delete RK;
        return return_code;
    }

    this->ComputeBDFCoeff(tn, val.tprev, val.xprev, val.dxprev);
    Solver_App_BDF app(rtol, atol, tn, val.tprev, val.xprev, val.dxprev, val.dxnext, this);

    bool success_solver, root_crossed = false, success_error_test = false;
    PVector px1;
    try {
        // Newton newton(50000);
        // Initial guess for the Newton solver is x1 or dx1 (depending on the choice of unknown)
        // unless variable step is used, in that case we have the prediction computed in
        // ComputeBDFCoeff
        if (use_dx_as_unknown)
            val.xnext.CopyData(val.dxnext);
        else if (do_varstep)
            val.xnext.CopyData(x0predicted);

        newton.Solve(&app, val.xnext);
        if (use_dx_as_unknown) {
            val.dxnext.CopyData(val.xnext);
            this->ComputeUnknown(val.xnext, val.dxnext, val.xprev, val.dxprev);
        }
        success_solver = true;
    }
    catch (NWT_error e) {
        if (e == NEWTON_NOT_CONVERGED)
            cerr << "Newton did not converge at time t=" << tn
                 << ". Trying LineSearch keeping current solution" << endl;
        else if (e == NEWTON_INF_NAN)
            cerr << "Newton diverged at time t=" << tn
                 << ". Trying LineSearch discarding current solution" << endl;
        if (!do_braid) {
            LinearSearch solver2(100);
            if (use_dx_as_unknown)
                val.xnext.CopyData(val.dxprev);
            else {
                if (do_varstep)
                    val.xnext.CopyData(x0predicted);
                else {
                    val.xprev.GetPVector(0, px1);
                    val.xnext.CopyData(px1);
                }
            }
            solver2.Solve(&app, val.xnext);
            if (use_dx_as_unknown) {
                val.dxnext.CopyData(val.xnext);
                this->ComputeUnknown(val.xnext, val.dxnext, val.xprev, val.dxprev);
            }
            success_solver = true;
        } else {
            success_solver = false;
        }
    }

    if (success_solver) {
        root_crossed = CheckRoots(val);
        Real refinement = 1.0;
        success_error_test = this->EstimateNextStepSize(app, val.xnext, refinement);
        val.next_dt = val.used_dt / refinement;
    }

    return this->PostStep(val, success_solver, root_crossed, success_error_test);
}

bool BackwardDiff::EstimateNextStepSize(const Solver_App_BDF& app,
                                        const Vector& x1,
                                        Real& refinement)
{
    bool success = true;
    if (do_varstep) {
        SVector correction(x1);
        correction.AXPBY(-Cprime, Cprime, x0predicted);
        Real err = app.XNorm(correction, x1);  // err=correction.Norm2()/rtol/x1.Norm2();
        err = max(err, 1e-10);
        refinement = pow(1.0 / 6.0 / err, 1.0 / Real(order + 1.0));
        refinement = 1.0 / refinement;
        refinement = min(max_rfactor, max(refinement, 1.0 / max_rfactor));

        if (err > 1) success = false;
    }
    return success;
}

void BackwardDiff::SetOrder(int type)
{
    BDF_a.Resize(type + 1, 0.0);
    switch (type) {
        case 1:
            BDF_b = Real(1);
            BDF_a(0) = Real(1);
            BDF_a(1) = Real(-1);
            break;
        case 2:
            BDF_b = Real(2. / 3.);
            BDF_a(0) = Real(1);
            BDF_a(1) = Real(-4. / 3.);
            BDF_a(2) = Real(1. / 3.);
            break;
        case 3:
            BDF_b = Real(6. / 11.);
            BDF_a(0) = Real(1);
            BDF_a(1) = Real(-18. / 11.);
            BDF_a(2) = Real(9. / 11.);
            BDF_a(3) = Real(-2. / 11.);
            break;
        case 4:
            BDF_b = Real(12. / 25.);
            BDF_a(0) = Real(1);
            BDF_a(1) = Real(-48. / 25.);
            BDF_a(2) = Real(36. / 25.);
            BDF_a(3) = Real(-16. / 25.);
            BDF_a(4) = Real(3. / 25.);
            break;
        case 5:
            BDF_b = Real(60. / 137.);
            BDF_a(0) = Real(1);
            BDF_a(1) = Real(-300. / 137.);
            BDF_a(2) = Real(300. / 137.);
            BDF_a(3) = Real(-200. / 137.);
            BDF_a(4) = Real(75. / 137.);
            BDF_a(5) = Real(-12. / 137.);
            break;
        case 6:
            BDF_b = Real(60. / 147.);
            BDF_a(0) = Real(1);
            BDF_a(1) = Real(-360. / 147.);
            BDF_a(2) = Real(450. / 147.);
            BDF_a(3) = Real(-400. / 147.);
            BDF_a(4) = Real(225. / 147.);
            BDF_a(5) = Real(-72. / 147.);
            BDF_a(6) = Real(10. / 147.);
            break;
        default:
            cerr << "Not implemented" << std::endl;
            throw BDF_ORDER_NOT_IMPLEMENTED;
    }
    order = type;
    nb_steps = type;
    this->InitArray();
}

// Coefficients for fixed-step
void BackwardDiff::ComputeBDFCoeff_FS(Real tn,
                                      const SMultiVector& tprev,
                                      const SMultiVector& xprev,
                                      const Vector& dxprev)
{
    int q = GetOrder();

    if (use_dx_as_unknown) {
        Real h = tn - tprev(0);
        coeff(0) = h * BDF_b / BDF_a(0);
        for (int i = 1; i < q + 1; i++)
            coeff(i) = -BDF_a(i) / BDF_a(0);
        coeff(q + 1) = 0;
    } else {
        for (int i = 0; i < q + 1; i++)
            coeff(i) = BDF_a(i) / BDF_b;
        coeff(q + 1) = 0;
    }
}

// Coefficients for variable-step
void BackwardDiff::ComputeBDFCoeff_VS(Real tn,
                                      const SMultiVector& tprev,
                                      const SMultiVector& xprev,
                                      const Vector& dxprev)
{
    int q = GetOrder();
    SVector allh(q);
    Real tc = tn;
    for (int i = 0; i < q; i++) {
        allh(i) = tc - tprev(i);
        tc = tprev(i);
    }

    SVector maple_coef(q + 1);
    maple_coef(1) = -allh(0) / allh(0);
    for (int i = 1; i < q; i++)
        maple_coef(i + 1) = maple_coef(i) - allh(i) / allh(0);
    DenseMatrix Linv(q + 1);
    for (int j = 0; j < q + 1; j++) {
        Linv(j, j) = 1;
        for (int i = j + 1; i < q + 1; i++)
            if (j == 0)
                Linv(i, j) = -Linv(i - 1, j) * maple_coef(i - 1);
            else
                Linv(i, j) = Linv(i - 1, j - 1) - Linv(i - 1, j) * maple_coef(i - 1);
    }
    DenseMatrix W(q + 1);
    SVector Diag(q + 1, 1);
    for (int i = 0; i < q + 1; i++)
        for (int j = 0; j < q + 1; j++)
            if (i != j) Diag(i) /= (maple_coef(i) - maple_coef(j));
    for (int i = q; i >= 0; i--) {
        W(i, q) = 1.0;
        for (int j = q - 1; j >= i; j--)
            W(i, j) = W(i, j + 1) * (maple_coef(i) - maple_coef(j + 1));
    }
    SVector rhs(q + 1);
    rhs(1) = 1;
    Linv.MatMult(rhs);
    W.MatMult(rhs);
    for (int i = 0; i < q + 1; i++)
        coeff(i) = rhs(i) * Diag(i);
    coeff(q + 1) = 0;
    if (use_dx_as_unknown) {
        Real b0 = 1.0 / coeff(0);
        Real h = tn - tprev(0);
        coeff(0) = b0 * h;
        for (int i = 1; i < q + 2; i++)
            coeff(i) *= -b0;
    }
}

// Coefficients for fixed-leading-coefficient
void BackwardDiff::ComputeBDFCoeff_FLC(Real tn,
                                       const SMultiVector& tprev,
                                       const SMultiVector& xprev,
                                       const Vector& dxprev)
{
    int q = GetOrder();
    SVector all_cum_dt(q);
    Real dt = tn - tprev(0);

    for (int i = 0; i < q; i++)
        all_cum_dt(i) = tn - tprev(i);

    Real alpha0 = 0, alpha0hat = 0;
    for (int i = 0; i < q; i++) {
        alpha0 -= 1.0 / (i + 1.0);
        alpha0hat -= dt / all_cum_dt(i);
    }
    Real beta0 = -1.0 / alpha0;
    Cprime = (alpha0 + 1 - alpha0hat) / (alpha0 * (1 + q * (alpha0 + 1 - alpha0hat)));

    DenseMatrix M(q + 1);
    for (int i = 1; i < q + 1; i++)
        M(i, 0) = i * pow(-1, i - 1) / dt;
    for (int j = 1; j < q + 1; j++)
        for (int i = 0; i < q + 1; i++)
            M(i, j) = pow(-all_cum_dt(j - 1) / dt, i);
    M.Factorize();
    SVector line(q + 1), line_dot(q + 1);
    line(0) = 1;
    line_dot(1) = 1;
    M.Solve(line);
    M.Solve(line_dot);

    x0predicted.Resize(xprev.GetXSize());
    x0predicted.CopyData(dxprev);
    PVector xi;
    xprev.GetPVector(0, xi);
    x0predicted.AXPBY(line(1), line(0), xi);
    for (int i = 2; i < q + 1; i++) {
        xprev.GetPVector(i - 1, xi);
        x0predicted.AXPBY(line(i), 1.0, xi);
    }

    coeff(0) = 1.0 / beta0;
    coeff(q + 1) = line_dot(0) - line(0) / beta0;
    for (int i = 1; i < q + 1; i++)
        coeff(i) = line_dot(i) - line(i) / beta0;

    if (use_dx_as_unknown) {
        Real b0 = 1.0 / coeff(0);
        Real h = tn - tprev(0);
        coeff(0) = b0 * h;
        for (int i = 1; i < q + 2; i++)
            coeff(i) *= -b0;
    }
}

/*!
This method computes the coefficient of the variable-step BDF method. If all time steps are equal,
this reduces to the initial constant step method. We can use fully variable coefficient
(`bdf_fixedleading=false` and `varstep=false` and \f$\alpha_{q+1}=0\f$) or fixed leading
coefficient (`bdf_fixedleading=true` or `varstep=true` and \f$\alpha_{q+1}\neq 0\f$) \f[
h_n\,\dot{y}_{n}=\sum_{i=0}^q \alpha_i y_{n-i}+\alpha_{q+1}\dot{y}_{n-1}
\f]
*/
void BackwardDiff::ComputeBDFCoeff(Real tn,
                                   const SMultiVector& tprev,
                                   const SMultiVector& xprev,
                                   const Vector& dxprev)
{
    int q = GetOrder();
    coeff.Resize(q + 2);

    if (DoVarstep() || ((DoBraid() || DoFalseVarstep()) && q > 1))
        if (IsFullyVariable())
            this->ComputeBDFCoeff_VS(tn, tprev, xprev, dxprev);
        else
            this->ComputeBDFCoeff_FLC(tn, tprev, xprev, dxprev);
    else
        this->ComputeBDFCoeff_FS(tn, tprev, xprev, dxprev);
}

void BackwardDiff::ComputeUnknown(Vector& var,
                                  const Vector& unk,
                                  const SMultiVector& xprev,
                                  const Vector& dxprev)
{
    int q = GetOrder();
    var.CopyData(dxprev);
    var.AXPBY(coeff(0), coeff(q + 1), unk);

    PVector xi;
    for (int i = 1; i < q + 1; i++) {
        xprev.GetPVector(i - 1, xi);
        var.AXPBY(coeff(i), Real(1.0), xi);
    }
}

void BackwardDiff::ComputeUnknown(Vector& var,
                                  const Vector& unk,
                                  const PMultiVector& xprev,
                                  const Vector& dxprev)
{
    int q = GetOrder();
    var.CopyData(dxprev);
    var.AXPBY(coeff(0), coeff(q + 1), unk);

    PVector xi;
    for (int i = 1; i < q + 1; i++) {
        xprev.GetPVector(i - 1, xi);
        var.AXPBY(coeff(i), Real(1.0), xi);
    }
}

Solver_App_BDF::Solver_App_BDF(Real rtol_,
                               const Vector& atol_,
                               Real tn_,
                               const SMultiVector& tprev_,
                               const SMultiVector& xprev_,
                               const Vector& dxprev_,
                               Vector& dxnext_,
                               BackwardDiff* bdf_)
{
    tn = tn_;
    tprev = tprev_;
    xprev = xprev_;
    dxprev = dxprev_;
    dxnext = dxnext_;
    pcoeff = bdf_->GetCoeff();
    pstate = bdf_->GetPState();
    update_jacobian = true;
    equation = bdf_->GetEq();
    CurrentJacobian = bdf_->GetCurrentJac();
    dense_mat = bdf_->UseDenseMatrix();
    bdf = bdf_;
    rtol = rtol_;
    atol = atol_;
    SetTol(rtol);
}

void Solver_App_BDF::dump() const
{
    Real dt = tn - tprev(0);
    cout << tn << " " << dt << " ";
}

Real Solver_App_BDF::XNorm(const Vector& dx, const Vector& x) const
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

Real Solver_App_BDF::FxNorm(const Vector& fx) const
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

Real Solver_App_BDF::XNorm(const Vector& dx, const Vector& x, Real tol_) const
{
    Real res = 0, w;
    for (int i = 0; i < dx.GetM(); i++) {
        w = tol_ * (1 + abs(x(i)));
        res += pow(dx(i) / max(w, 1e-15), 2);
    }
    return sqrt(res / dx.GetM());
}

Real Solver_App_BDF::FxNorm(const Vector& fx, Real tol_) const
{
    Real res = 0, w;
    for (int i = 0; i < fx.GetM(); i++) {
        w = tol_;
        res += pow(fx(i) / max(w, 1e-15), 2);
    }
    return sqrt(res / fx.GetM());
}

void Solver_App_BDF::EvaluateFunAndJac(const Vector& x,
                                       Vector& gx,
                                       bool require_jac,
                                       bool factorize)
{
    // WARNING : the unknown is now the derivative, so Vector x is the derivative
    // WARNING : dxnext is used as a variable to store the y=y(dy)...

    require_jac = (require_jac && update_jacobian);

    this->bdf->ComputeUnknown(dxnext, x, xprev, dxprev);

    if (bdf->UseDxAsUnknown())  // WARNING : this is where the tricky x <-> dxnext is
                                // confusing...
        equation->function(this->tn, dxnext, x, pstate, gx);
    else  // x is really x, dxnext is really dx
    {
        dxnext *= (1.0 / (tn - tprev(0)));
        equation->function(this->tn, x, dxnext, pstate, gx);
    }

    if (require_jac) {
        Matrix* jacmat = dynamic_cast<Matrix*>(GetCurrentJacobian());
        if (jacmat == NULL) {
            cerr << "Using DBlockTriMatrix with BDF method. Should not happen !!" << endl;
            abort();
        }

        if (bdf->UseDxAsUnknown())  // WARNING : this is where the tricky x <-> dxnext is
                                    // confusing...
        {
            equation->jacobian_ypcdy(this->tn, dxnext, x, pstate, 1.0 / pcoeff(0), *jacmat);
            (*jacmat) *= pcoeff(0);
        } else {
            equation->jacobian_ypcdy(
                this->tn, x, dxnext, pstate, pcoeff(0) / (tn - tprev(0)), *jacmat);
        }

        if (require_jac && factorize) {
            jacmat->Factorize();
        }
    }
    // update_jacobian=true;
}
}  // namespace griddyn::paradae
