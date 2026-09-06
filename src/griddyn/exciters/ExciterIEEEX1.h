/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ExciterDC2A.h"
#include <string>

namespace griddyn::exciters {
/** PSS/E IEEEX1 DC excitation system.
 *
 * With \f$V_m\f$ denoting the transduced terminal voltage, the model is
 *
 * \f[
 * \begin{aligned}
 * T_R\dot V_m &= V_T-V_m,\\
 * v_i &= V_{ref}+V_{bias}-V_m-
 *        \left(K_F E_{fd}/T_F-R_F\right),\\
 * T_B\dot x &= v_i-x,\\
 * v_{LL} &= (T_C/T_B)v_i+(1-T_C/T_B)x,\\
 * T_A\dot V_R &= K_Av_{LL}-V_R,\\
 * T_F\dot R_F &= K_FE_{fd}/T_F-R_F,\\
 * T_E\dot E_{fd} &= V_R-K_EE_{fd}-S_E(E_{fd})E_{fd}.
 * \end{aligned}
 * \f]
 *
 * The non-windup regulator bounds are \f$V_{RMIN}V_T\f$ and
 * \f$V_{RMAX}V_T\f$.  The bounds use instantaneous terminal voltage, not the
 * transducer state.  When \f$T_B\leq0\f$, the ANDES lead-lag block explicitly
 * bypasses as \f$v_{LL}=v_i\f$; consequently the otherwise unused \f$x\f$ state
 * is omitted here.  A zero \f$T_R\f$ similarly bypasses the transducer.
 *
 * Saturation is the PSS/E two-point quadratic curve and the exciter output is
 * \f$E_{fd}\f$ (it is not multiplied by rotor speed).
 *
 * @par Equation sources
 * - ANDES IEEEX1/EXDC2 and reusable block equations, commit
 *   eda5163c9ee8d19945a1dd5d1771fec5da608c27.
 * - OpenIPSL IEEEX1 rotating-exciter realization, commit
 *   8155c73f51ceeec935eb158247c7c043eb697ff5.
 *
 * @note OpenIPSL's IEEEX1 implementation uses constant regulator bounds,
 * whereas the PSS/E DYR-compatible ANDES implementation uses the
 * voltage-scaled bounds above.  GridDyn follows the latter for DYR parity.
 * OpenIPSL's shared DC-exciter initialization can also derive a replacement
 * for a supplied zero \f$K_E\f$; GridDyn preserves the DYR value, as ANDES
 * does, so importing a file never silently changes that model parameter.
 */
class ExciterIEEEX1 final: public ExciterDC2A {
  public:
    explicit ExciterIEEEX1(const std::string& objName = "exciterIEEEX1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    ChangeCode rootCheck(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode,
                         CheckLevel level) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    stringVec localStateNames() const override;

  private:
    bool hasLeadLag() const;
    double directMeasuredVoltage(const IOdata& inputs, const double state[]) const;
    double regulatorDrive(const IOdata& inputs, const double state[]) const;

  protected:
    void limitJacobian(double voltage,
                       int voltageLoc,
                       int refLoc,
                       double cj,
                       MatrixData<double>& matrixData) override;
};

}  // namespace griddyn::exciters
