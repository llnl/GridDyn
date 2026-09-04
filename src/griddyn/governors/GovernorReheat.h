/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** IEEE Standard Governor / PSS/E IEESGO steam-turbine governor.
 *
 * The model uses the per-unit speed input \f$\omega\f$ and mechanical-power
 * reference \f$P_{set}\f$.  Let \f$x_f\f$, \f$x_g\f$, \f$x_c\f$,
 * \f$x_r\f$, and \f$x_x\f$ be respectively the speed-filter, governor,
 * steam-chest, reheater, and crossover states.  The implemented realization
 * is
 * \f[
 * \begin{aligned}
 * T_1\dot{x}_f &= P_{set}+K_1(\omega_{ref}-\omega)-x_f,\\
 * T_3\dot{x}_g &= x_f-x_g,\\
 * T_4\dot{x}_c &= \operatorname{limit}\!\left(
 *     \frac{T_2}{T_3}x_f+\left(1-\frac{T_2}{T_3}\right)x_g,
 *     P_{min},P_{max}\right)-x_c,\\
 * T_5\dot{x}_r &= K_2x_c-x_r,\qquad
 * T_6\dot{x}_x=K_3x_r-x_x,\\
 * P_m &= (1-K_2)x_c+(1-K_3)x_r+x_x.
 * \end{aligned}
 * \f]
 *
 * The valve limit is an algebraic clamp; its derivative is zero outside the
 * open interval.  `T2` is a lead numerator coefficient and may be zero;
 * `T1`, `T3`--`T6` must be positive.  DYR parameters are `T1`--`T6`,
 * `K1`--`K3`, `PMAX`, and `PMIN`, in that order.
 */
class GovernorReheat: public Governor {
  public:
  protected:
    model_parameter T4 = 0.5;  //!< [s] steam-inlet/steam-chest delay
    model_parameter T5 = 10.0;  //!< [s] reheater delay
    model_parameter T6 = 0.5;  //!< [s] IP-LP crossover delay
    model_parameter K2 = 1.0;  //!< fraction entering the reheater stage
    model_parameter K3 = 1.0;  //!< fraction entering the crossover stage
  public:
    explicit GovernorReheat(const std::string& objName = "govReheat_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorReheat();
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
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

    // virtual void setTime (CoreTime time) const{prevTime=time;};
};

}  // namespace griddyn::governors
