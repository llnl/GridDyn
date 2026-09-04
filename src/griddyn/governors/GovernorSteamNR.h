/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GovernorIeeeSimple.h"
#include <string>

namespace griddyn::governors {
/** IEEE non-reheat steam-turbine governor.
 *
 * This model retains the IEEE speed-control realization from
 * GovernorIeeeSimple and adds a steam-chest lag.  With valve state \f$v\f$,
 * speed-filter state \f$x\f$, chest state \f$c\f$, and
 * \f$\Delta\omega=\omega-\omega_{ref}\f$, the equations are
 * \f[
 * \begin{aligned}
 * \dot v &= \operatorname{limit}\left(
 *   \frac{P_{set}-v-Kx-KT_2\Delta\omega/T_1}{T_3},
 *   P_{down},P_{up}\right),\\
 * T_1\dot x &= -x+\left(1-\frac{T_2}{T_1}\right)\Delta\omega,\\
 * T_{ch}\dot c &= v-c,\qquad P_m=c.
 * \end{aligned}
 * \f]
 *
 * The valve equation is rate-limited; its Jacobian follows the unrestricted
 * expression only while the rate limit is inactive.  The valve position must
 * initialize within `PMIN` and `PMAX`.
 */
class GovernorSteamNR: public GovernorIeeeSimple {
  public:
  protected:
    model_parameter Tch = 0.0;  //!< [s] steam-chest time constant
  public:
    GovernorSteamNR(const std::string& objName = "govSteamNR_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorSteamNR();
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
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateData,
                            double deriv[],
                            const SolverMode& sMode) override;
    // only called if the genModel is not present
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
};

}  // namespace griddyn::governors
