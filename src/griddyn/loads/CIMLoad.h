/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "MotorLoad5.h"
#include "utilities/Saturation.h"

namespace griddyn::loads {
/**
 * @brief PSS/E CIM5 five-state induction motor with speed-power torque.
 *
 * @details
 * CIM5 inherits the common five-state induction-motor DAE with algebraic
 * currents \f$[I_r,I_m]\f$ and differential states
 * \f$[s,E'_r,E'_m,E''_r,E''_m]\f$.  The terminal-voltage components are
 * \f$V_r=-VV_c\sin\theta\f$ and \f$V_m=VV_c\cos\theta\f$.  The stator
 * equations are
 * \f[
 * \begin{aligned}
 * 0&=V_m-E''_m-r_aI_m-x''I_r,\\
 * 0&=V_r-E''_r-r_aI_r+x''I_m.
 * \end{aligned}
 * \f]
 * Its mechanical torque/power characteristic is
 * \f[
 * T_m=T_{\rm nom}(1-s)^D.
 * \f]
 *
 * @par Saturation and shared motor dynamics
 * Let \f$E''=\sqrt{(E''_r)^2+(E''_m)^2}\f$.  The OpenIPSL/PSS/e saturation
 * term applied to the transient-axis equations is
 * \f[
 * g(E''_r,E''_m)=\frac{S_E(E'')}{E''},
 * \f]
 * with \f$g=0\f$ at the origin.  \f$S_E\f$ is the cutoff scaled-quadratic
 * curve fitted through \f$(E_1,SE_1)\f$ and \f$(E_2,SE_2)\f$.  In particular,
 * \f[
 * S_E(E)=\begin{cases}0,&E<A,\\B(E-A)^2/E,&E\ge A.\end{cases}
 * \f]
 * The relevant common state equations are
 * \f[
 * \begin{aligned}
 * \dot s&=\frac{T_m-(E''_rI_r+E''_mI_m)}{2H},\\
 * T'_{0}\dot E'_r&=\omega_bsT'_{0}E'_m-E'_r-(x_0-x')I_m+E''_mg,\\
 * T'_{0}\dot E'_m&=-\omega_bsT'_{0}E'_r-E'_m+(x_0-x')I_r-E''_rg.
 * \end{aligned}
 * \f]
 * The inherited subtransient equations use \f$E'_r,E'_m,E''_r,E''_m\f$,
 * \f$x'-x''\f$, and \f$T''_0\f$ directly; their residuals and analytic
 * Jacobian include the saturation derivatives.
 *
 * @par Topology and input scope
 * \c mtype=1 selects Type-A, \c mtype=2 selects Type-B.  For a single-cage
 * input (\c R2 or \c X2 non-positive), the subtransient circuit collapses to
 * \f$x''=x'\f$ using a small nonzero \f$T''_0\f$ to keep the five-state DAE
 * nonsingular.  Motor DYR records are deliberately not supported yet; use the
 * native load factory name \c cim5.
 *
 * @par Equation sources
 * - OpenIPSL PSS/E CIM5:
 *   https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/Electrical/Machines/PSSE/CIM5.mo
 * - OpenIPSL scaled saturation function:
 *   https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/NonElectrical/Functions/SE.mo
 */
class CIM5: public MotorLoad5 {
  public:
    explicit CIM5(const std::string& objName = "cim5_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  protected:
    double mechPower(double slip) const override;
    double dmechds(double slip) const override;
    double saturationFactor(double erpp, double empp) const override;
    void saturationFactorDerivatives(double erpp,
                                     double empp,
                                     double& derivativeErpp,
                                     double& derivativeEmpp) const override;

  private:
    double torqueNominal = 1.0;
    double torqueExponent = 1.0;
    utilities::Saturation saturation{
        utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC};
    double saturationE1 = 1.0;
    double saturationSE1 = 0.06;
    double saturationE2 = 1.2;
    double saturationSE2 = 0.6;
};

/**
 * @brief PSS/E CIM6 five-state induction motor with polynomial/exponential
 * torque.
 *
 * @details
 * CIM6 uses the same stator, rotor, saturation, Type-A/Type-B, and
 * single-/double-cage equations documented for CIM5.  It changes only the
 * mechanical torque characteristic:
 * \f[
 * T_m=T_{\rm nom}\left[A(1-s)^2+B(1-s)+C+
 *                      D(1-s)^E\right].
 * \f]
 * The implementation differentiates this expression analytically for the
 * slip row of the DAE Jacobian:
 * \f[
 * \frac{\partial T_m}{\partial s}=-T_{\rm nom}
 * \left[2A(1-s)+B+DE(1-s)^{E-1}\right].
 * \f]
 * Saturation is \f$g=S_E(E'')/E''\f$ with the same \c E1, \c SE1, \c E2,
 * and \c SE2 fitting points as CIM5. Motor DYR records are deliberately not
 * supported yet; use the native load factory name \c cim6.
 *
 * @par Equation sources
 * - OpenIPSL PSS/E CIM6:
 *   https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/Electrical/Machines/PSSE/CIM6.mo
 * - OpenIPSL scaled saturation function:
 *   https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/NonElectrical/Functions/SE.mo
 */
class CIM6: public MotorLoad5 {
  public:
    explicit CIM6(const std::string& objName = "cim6_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  protected:
    double mechPower(double slip) const override;
    double dmechds(double slip) const override;
    double saturationFactor(double erpp, double empp) const override;
    void saturationFactorDerivatives(double erpp,
                                     double empp,
                                     double& derivativeErpp,
                                     double& derivativeEmpp) const override;

  private:
    double torqueNominal = 1.0;
    double coefficientA = 1.0;
    double coefficientB = 1.0;
    double coefficientC = 1.0;
    double coefficientD = 1.0;
    double exponentE = 1.0;
    utilities::Saturation saturation{
        utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC};
    double saturationE1 = 1.0;
    double saturationSE1 = 0.06;
    double saturationE2 = 1.2;
    double saturationSE2 = 0.6;
};
}  // namespace griddyn::loads
