/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../equations/Equation.h"
#include "../math/PMultiVector.h"
#include "../math/PVector.h"
#include "../math/SMultiVector.h"
#include "../math/SVector.h"
#include "../math/Vector.h"
#include "../solvers/Solver.h"
#include "TimeIntegrator.h"
#include <list>
#include <string>

namespace griddyn::paradae {
enum BDF_type { BDF_1 = 1, BDF_2 = 2, BDF_3 = 3, BDF_4 = 4, BDF_5 = 5, BDF_6 = 6 };

enum BDF_error { BDF_ORDER_NOT_IMPLEMENTED, BDF_FAILED_IMPLICIT_STEP };

class BackwardDiff;

class Solver_App_BDF: public Solver_App {
    Real tn;
    Real rtol;
    PVector atol;
    PMultiVector tprev;
    PMultiVector xprev;
    PVector dxprev;
    PVector pstate;

    Equation* equation;
    BackwardDiff* bdf;

    PVector dxnext;
    PVector pcoeff;

  public:
    Solver_App_BDF(Real rtol,
                   const Vector& atol,
                   Real tn_,
                   const SMultiVector& tprev_,
                   const SMultiVector& xprev_,
                   const Vector& dxprev_,
                   Vector& dxnext_,
                   BackwardDiff* bdf_);
    virtual void dump() const;
    virtual void EvaluateFunAndJac(const Vector& x, Vector& gx, bool require_jac, bool factorize);
    virtual Real XNorm(const Vector& dx, const Vector& x) const;
    virtual Real FxNorm(const Vector& fx) const;
    virtual Real XNorm(const Vector& dx, const Vector& x, Real tol_) const;
    virtual Real FxNorm(const Vector& fx, Real tol_) const;
};

class BackwardDiff: public TimeIntegrator {
  protected:
    SVector BDF_a;
    Real BDF_b;
    SVector coeff;
    SVector x0predicted;
    Real Cprime;
    VirtualMatrix* CurrentJacobian;
    bool fullyvariable;
    bool use_dx_as_unknown;

    virtual void InitArray();
    void ComputeBDFCoeff_FS(Real tn,
                            const SMultiVector& tprev,
                            const SMultiVector& xprev,
                            const Vector& dxprev);
    void ComputeBDFCoeff_VS(Real tn,
                            const SMultiVector& tprev,
                            const SMultiVector& xprev,
                            const Vector& dxprev);
    void ComputeBDFCoeff_FLC(Real tn,
                             const SMultiVector& tprev,
                             const SMultiVector& xprev,
                             const Vector& dxprev);

  public:
    BackwardDiff();
    virtual ~BackwardDiff();
    BackwardDiff(int type, Equation* eq, bool do_varstep, bool force_FLC);
    RCODE AdvanceStep(DATA_Struct& val, int iter_ref = 0);
    bool EstimateNextStepSize(const Solver_App_BDF& app, const Vector& x1, Real& refinement);
    void SetOrder(int type);
    inline int GetOrder() { return order; };
    inline Vector& GetA() { return BDF_a; };
    inline Real GetB() { return BDF_b; };
    inline Vector& GetCoeff() { return coeff; };
    inline bool UseDxAsUnknown() const { return use_dx_as_unknown; };
    inline bool& UseDxAsUnknown() { return use_dx_as_unknown; };
    virtual std::string GetName();
    virtual void show();
    virtual TI_type GetType() { return BDF; };
    bool IsFullyVariable() const { return fullyvariable; };
    bool& IsFullyVariable() { return fullyvariable; };
    virtual VirtualMatrix* GetCurrentJac() { return CurrentJacobian; };
    virtual void SetDenseMatrix(bool dense_mat_ = true);
    void ComputeUnknown(Vector& var,
                        const Vector& unk,
                        const SMultiVector& xprev,
                        const Vector& dxprev);
    void ComputeUnknown(Vector& var,
                        const Vector& unk,
                        const PMultiVector& xprev,
                        const Vector& dxprev);
    void ComputeBDFCoeff(Real tn,
                         const SMultiVector& tprev,
                         const SMultiVector& xprev,
                         const Vector& dxprev);
};
}  // namespace griddyn::paradae
