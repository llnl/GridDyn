/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GenModel5.h"
#include <string>

namespace griddyn::genmodels {
/**
 * @brief Sixth-order PSS/E GENROU round-rotor synchronous-machine model.
 *
 * @details
 * This class implements the differential-algebraic GENROU formulation used by
 * ANDES, translated to GridDyn's established dq-axis sign convention. All
 * electrical quantities are per-unit on the machine base. The algebraic states
 * are \f$[I_d,I_q]\f$ and the differential states are
 * \f$[\delta,\omega,e'_{d},e'_{q},e''_{q},e''_{d}]\f$. The input vector supplies
 * terminal voltage magnitude \f$V\f$, terminal angle \f$\theta\f$, field
 * voltage \f$E_f\f$, and mechanical power \f$P_m\f$.
 *
 * @par Rotor-frame voltage and coefficient definitions
 * The terminal-voltage components are
 * \f[
 * V_d=-V\sin(\delta-\theta), \qquad
 * V_q= V\cos(\delta-\theta).
 * \f]
 * The transient/subtransient coefficients follow the ANDES notation:
 * \f[
 * \begin{aligned}
 * \gamma_{d1} &= \frac{x''_d-x_l}{x'_d-x_l}, &
 * \gamma_{q1} &= \frac{x''_q-x_l}{x'_q-x_l}, \\
 * \gamma_{d2} &= \frac{x'_d-x''_d}{(x'_d-x_l)^2}, &
 * \gamma_{q2} &= \frac{x'_q-x''_q}{(x'_q-x_l)^2}, \\
 * \gamma_{qd} &= \frac{x_q-x_l}{x_d-x_l}.
 * \end{aligned}
 * \f]
 * GridDyn parameter names \c Xdp, \c Xqp, \c Xdpp, and \c Xqpp denote
 * \f$x'_d\f$, \f$x'_q\f$, \f$x''_d\f$, and \f$x''_q\f$, respectively.
 *
 * @par Air-gap flux and stator algebraic equations
 * The subtransient air-gap flux components and magnitude are
 * \f[
 * \begin{aligned}
 * \psi''_q &= \gamma_{q1}e'_d+(1-\gamma_{q1})e''_q,\\
 * \psi''_d &= \gamma_{d1}e'_q+(1-\gamma_{d1})e''_d,\\
 * \psi''   &= \sqrt{(\psi''_d)^2+(\psi''_q)^2}.
 * \end{aligned}
 * \f]
 * The two algebraic residuals are
 * \f[
 * \begin{aligned}
 * 0 &= V_d+r_a I_d+x''_q I_q-\psi''_q,\\
 * 0 &= V_q+r_a I_q-x''_d I_d-\psi''_d.
 * \end{aligned}
 * \f]
 * Electrical torque, including stator copper loss, is
 * \f[
 * T_e=(V_d+r_a I_d)I_d+(V_q+r_a I_q)I_q.
 * \f]
 *
 * @par Differential equations
 * With synchronous electrical speed \f$\omega_b=2\pi f_b\f$, inertia \f$H\f$,
 * damping \f$D\f$, and saturation \f$S_e\f$, the implemented equations are
 * \f[
 * \begin{aligned}
 * \dot\delta &= \omega_b(\omega-1),\\
 * \dot\omega &= \frac{P_m-T_e-D(\omega-1)}{2H},\\
 * T'_{q0}\dot e'_d &= -e'_d
 *  -(x_q-x'_q)\left[\gamma_{q2}(e'_d-e''_q)+\gamma_{q1}I_q\right]
 *  -S_e\gamma_{qd}\psi''_q,\\
 * T'_{d0}\dot e'_q &= E_f-e'_q
 *  +(x_d-x'_d)\left[\gamma_{d1}I_d+\gamma_{d2}(e''_d-e'_q)\right]
 *  -S_e\psi''_d,\\
 * T''_{q0}\dot e''_q &= -e''_q+e'_d-(x'_q-x_l)I_q,\\
 * T''_{d0}\dot e''_d &= -e''_d+e'_q+(x'_d-x_l)I_d.
 * \end{aligned}
 * \f]
 * The GridDyn parameters \c Tqop, \c Tdop, \c Tqopp, and \c Tdopp correspond
 * to \f$T'_{q0}\f$, \f$T'_{d0}\f$, \f$T''_{q0}\f$, and \f$T''_{d0}\f$.
 *
 * @par Quadratic saturation
 * For positive saturation points \f$S_{1.0}\f$ and \f$S_{1.2}\f$, define
 * \f[
 * r=\sqrt{\frac{S_{1.0}}{1.2S_{1.2}}},\qquad
 * A=\frac{1-1.2r}{1-r},\qquad
 * B=\frac{S_{1.0}}{(1-A)^2}.
 * \f]
 * The ANDES ExcQuadSat characteristic is then
 * \f[
 * S_e(\psi'')=
 * \begin{cases}
 * 0, & \psi'' < A,\\
 * \displaystyle\frac{B(\psi''-A)^2}{\psi''}, & \psi''\ge A.
 * \end{cases}
 * \f]
 * Saturation is disabled when either configured saturation value is not
 * positive. The default \f$S_{1.0}=0\f$, \f$S_{1.2}=1\f$ is the ANDES
 * no-saturation convention.
 *
 * @par Initialization
 * Given terminal power \f$P+jQ\f$, initialization first forms
 * \f[
 * \underline V=Ve^{j\theta},\quad
 * \underline I=\frac{P-jQ}{\underline V^*},\quad
 * \underline\psi''=\underline V+(r_a+jx''_d)\underline I.
 * \f]
 * If \f$\chi=\arg(\underline\psi'')-\arg(\underline I)\f$,
 * \f$a=|\underline\psi''|(1+S_e\gamma_{qd})\f$, and
 * \f$b=|\underline I|(x''_q-x_q)\f$, the initial rotor angle is
 * \f[
 * \delta_0=\tan^{-1}\!\left(\frac{b\cos\chi}{b\sin\chi-a}\right)
 *          +\arg(\underline\psi'').
 * \f]
 * The complex flux and current are then rotated into the rotor frame. The
 * remaining states are initialized from the steady-state forms of the six
 * equations above, with
 * \f[
 * E_{f0}=(1+S_e)\psi''_{d0}-(x_d-x''_d)I_{d0},\qquad P_{m0}=T_{e0}.
 * \f]
 *
 * @par GridDyn and ANDES sign conventions
 * To retain compatibility with existing GridDyn generator models, the
 * translation uses
 * \f[
 * \begin{array}{c|rrrrrr}
 * &I_d&V_d&e'_d&e''_q&\psi''_q&\delta\\ \hline
 * \text{GridDyn}/\text{ANDES}&-1&-1&-1&-1&-1&+1
 * \end{array}
 * \f]
 * while \f$I_q,V_q,e'_q,e''_d,\psi''_d\f$, and \f$\omega\f$ have the same
 * signs. This mapping changes representation only; it does not change terminal
 * power, torque, or rotor motion.
 *
 * @par Equation sources
 * - <a
 * href="https://github.com/CURENT/andes/blob/eda5163c9ee8d19945a1dd5d1771fec5da608c27/andes/models/synchronous/genrou.py">ANDES
 * v2.0 GENROU equations and initialization</a>
 * - <a
 * href="https://github.com/CURENT/andes/blob/eda5163c9ee8d19945a1dd5d1771fec5da608c27/andes/models/synchronous/genbase.py">ANDES
 * v2.0 synchronous-generator base equations</a>
 * - <a
 * href="https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/Electrical/Machines/PSSE/GENROU.mo">OpenIPSL
 * PSS/E GENROU initialization reference</a>
 * - <a
 * href="https://docs.andes.app/en/v1.8.10/_examples/verification/andes-ieee14-verification.html">ANDES
 * GENROU verification against PSS/E, OpenIPSL, and TSAT</a>
 * - H. Cui, F. Li, and K. Tomsovic, "Hybrid Symbolic-Numeric Framework for
 *   Power System Modeling and Analysis," IEEE Transactions on Power Systems,
 *   36(2), 1373-1384, 2021,
 *   <a href="https://doi.org/10.1109/TPWRS.2020.3017019">doi:10.1109/TPWRS.2020.3017019</a>.
 *
 * @note PSS/E requires the GENROU d- and q-axis subtransient reactances to be
 * equal. The class retains separate \c Xdpp and \c Xqpp storage to fit the
 * GridDyn architecture; the \c xpp setter assigns both values.
 */
class GenModelGENROU: public GenModel5 {
  protected:
  public:
    explicit GenModelGENROU(const std::string& objName = "genrou_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stringVec localStateNames() const override;
    // dynamics
    virtual void residual(const IOdata& inputs,
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& sD,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  MatrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& sD,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
};

}  // namespace griddyn::genmodels
