/* Copyright (c) 2014-2026, Lawrence Livermore National Security
 * SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "GenModel5.h"
#include <string>
namespace griddyn::genmodels {
/**
 * @brief Sixth-order WECC/PSS/E Type-J round-rotor synchronous machine.
 *
 * @details
 * The algebraic states are \f$[I_d,I_q]\f$ and the differential states are
 * \f$[\delta,\omega,E'_d,E'_q,\Psi''_d,\Psi''_q]\f$. Terminal voltage is
 * resolved in the rotor frame as
 * \f[
 * V_d=-V\sin(\delta-\theta),\qquad V_q=V\cos(\delta-\theta).
 * \f]
 * GridDyn parameter names \c Xdp, \c Xqp, \c Xdpp, and \c Xqpp denote
 * \f$x'_d\f$, \f$x'_q\f$, \f$x''_d\f$, and \f$x''_q\f$, respectively.
 *
 * @par Type-J saturation and stator equations
 * Type-J saturation is driven by the air-gap voltage and an armature-current
 * correction:
 * \f[
 * \begin{aligned}
 * A_d &= V_q+r_aI_q-x_lI_d, &
 * A_q &= -V_d-r_aI_d-x_lI_q,\\
 * E_{ag} &= \sqrt{A_d^2+A_q^2}
 *          +K_{IS}\sqrt{I_d^2+I_q^2}, &
 * S_e &= S(E_{ag}).
 * \end{aligned}
 * \f]
 * \f$S(E)\f$ is the PSS/e cutoff scaled-quadratic curve fitted through
 * \f$(1.0,S_{10})\f$ and \f$(1.2,S_{12})\f$:
 * \f[
 * S(E)=\begin{cases}
 * 0,&E<A,\\
 * B(E-A)^2/E,&E\ge A.
 * \end{cases}
 * \f]
 * The d- and q-axis saturation factors and subtransient reactances are
 * \f[
 * D_s=1+S_e,\quad Q_s=1+\frac{x_q}{x_d}S_e,\quad
 * x''_{d,s}=x_l+\frac{x''_d-x_l}{D_s},\quad
 * x''_{q,s}=x_l+\frac{x''_q-x_l}{Q_s}.
 * \f]
 * The stator residuals implemented by this class are
 * \f[
 * \begin{aligned}
 * 0&=V_d+r_aI_d+x''_{q,s}I_q-\Psi''_q,\\
 * 0&=V_q+r_aI_q-x''_{d,s}I_d-\Psi''_d.
 * \end{aligned}
 * \f]
 *
 * @par Differential equations
 * Define the intermediate rotor quantities
 * \f[
 * \begin{aligned}
 * E_{q1}&=\frac{-\Psi''_d(x_d-x'_d)+E'_q(x_d-x''_d)}
 *                {x'_d-x''_d},\\
 * E_{d1}&=\frac{\Psi''_q(x_q-x'_q)+E'_d(x_q-x''_q)}
 *                {x'_q-x''_q},\\
 * E_{q2}&=\left(\Psi''_d-E'_q-I_d\frac{x'_d-x''_d}{D_s}\right)
 *                \frac{x_d-x''_d}{x'_d-x''_d},\\
 * E_{d2}&=-(E'_d+\Psi''_q)\frac{x_q-x''_q}{x'_q-x''_q}
 *          -I_q\frac{x_q-x''_q}{Q_s}.
 * \end{aligned}
 * \f]
 * With \f$\omega_b=2\pi f_b\f$, electrical torque
 * \f[
 * T_e=(\Psi''_d+x''_{d,s}I_d)I_q+
 *     (\Psi''_q-x''_{q,s}I_q)I_d,
 * \f]
 * and field voltage \f$E_f\f$, the six state derivatives are
 * \f[
 * \begin{aligned}
 * \dot\delta&=\omega_b(\omega-1),&
 * \dot\omega&=\frac{P_m-T_e-D(\omega-1)}{2H},\\
 * \dot E'_d&=-Q_sE_{d1}/T'_{q0},&
 * \dot E'_q&=(E_f-D_sE_{q1})/T'_{d0},\\
 * \dot\Psi''_d&=-D_s\frac{x'_d-x''_d}{x_d-x''_d}
 *                 E_{q2}/T''_{d0},&
 * \dot\Psi''_q&= Q_s\frac{x'_q-x''_q}{x_q-x''_q}
 *                 E_{d2}/T''_{q0}.
 * \end{aligned}
 * \f]
 *
 * @par Initialization and PSS/e input
 * Initialization rotates terminal power-flow current into the rotor frame,
 * solves the q-axis steady-state constraint for \f$\delta\f$, and then forms
 * \f$\Psi''_{d,q}\f$, \f$E'_{d,q}\f$, \f$E_{f0}=D_sE_{q1}\f$, and
 * \f$P_{m0}=T_{e0}\f$. The PSS/e \c GENTPJ record maps 19 fields:
 * \c TDOP, \c TDOPP, \c TQOP, \c TQOPP, \c H, \c D, \c XD, \c XQ,
 * \c XDP, \c XQP, \c XDPP, \c XQPP, \c XL, \c S10, \c S12, and \c KIS,
 * plus bus number, model name, and machine ID.
 *
 * @par Equation sources
 * - OpenIPSL PSS/E GENTPJ:
 *   https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/Electrical/Machines/PSSE/GENTPJ.mo
 * - OpenIPSL scaled saturation function:
 *   https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/NonElectrical/Functions/SE.mo
 */
class GenModelGENTPJ final: public GenModel5 {
  public:
    explicit GenModelGENTPJ(const std::string& objName = "gentpj_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    stringVec localStateNames() const override;
    void residual(const IOdata& inputs,
                  const StateData& sD,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& sD,
                    double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& sD,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& sD,
                          MatrixData<double>& md,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;

  private:
    double Kis = 0.0;
};
}  // namespace griddyn::genmodels
