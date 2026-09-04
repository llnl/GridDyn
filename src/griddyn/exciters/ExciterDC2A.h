/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ExciterDC1A.h"
#include <string>
namespace griddyn::exciters {
/** PSS/E ESDC2A / EXDC2 DC excitation system.
 *
 * Both DYR names use this implementation and their common PSS/E schema. It
 * uses the DC1A states \f[E_{fd},V_R,x,R_F,(V_m)\f] and equations, with the
 * DC2A-specific voltage-dependent regulator bounds
 * \f[
 * V_{RMIN}V_m \le V_R \le V_{RMAX}V_m.
 * \f]
 * Thus the inherited field equation is
 * \f$T_E\dot E_{fd}=V_R-K_EE_{fd}-S_E(E_{fd})\f$ and the regulator uses the
 * inherited sensed-voltage lead-lag and washout feedback. The PSS/E `Switch`
 * field is not implemented by PSS/E; the reader rejects a nonzero value.
 */
class ExciterDC2A: public ExciterDC1A {
  protected:
  public:
    explicit ExciterDC2A(const std::string& objName = "exciterDC2A_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateDataValue,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

  protected:
    virtual void limitJacobian(double V,
                               int voltageLoc,
                               int refLoc,
                               double cj,
                               MatrixData<double>& matrixDataValue) override;
};

}  // namespace griddyn::exciters
