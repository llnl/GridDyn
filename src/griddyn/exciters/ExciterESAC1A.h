/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ExciterEXAC1.h"
#include <string>

namespace griddyn::exciters {
/** PSS/E ESAC1A (IEEE Type AC1A) excitation system.
 *
 * ESAC1A shares the AC exciter, saturation, commutating-reactance loading,
 * transducer, lead-lag, and rate-feedback equations of EXAC1. With sensed
 * voltage \f$v_m\f$, lead-lag state \f$x_L\f$, control-element state
 * \f$v_A\f$, exciter voltage \f$v_E\f$, and washout state \f$x_F\f$:
 * \f[
 * \begin{aligned}
 * T_R\dot v_m &= V_t-v_m, & T_B\dot x_L &= v_i-x_L,\\
 * v_i &= V_{ref}-v_m-\frac{K_F}{T_F}(v_{FE}-x_F),\\
 * T_A\dot v_A &= K_A\left(x_L+\frac{T_C}{T_B}(v_i-x_L)\right)-v_A,\\
 * T_E\dot v_E &= v_A-v_{FE},\\
 * T_F\dot x_F &= v_{FE}-x_F.
 * \end{aligned}
 * \f]
 * Here \f$v_{FE}=K_Ev_E+S_E(v_E)+K_DX_{ad}I_{fd}\f$ and
 * \f$E_{fd}=v_EF_{EX}(K_CX_{ad}I_{fd}/v_E)\f$. The state \f$v_A\f$ has
 * anti-windup limits \f$[V_{AMIN},V_{AMAX}]\f$. The standalone interface has
 * no UEL/OEL inputs, so the supplied `VRMIN`/`VRMAX` values are retained for
 * the future limiter interface rather than applied as an additional clamp.
 */
class ExciterESAC1A final: public ExciterEXAC1 {
  public:
    explicit ExciterESAC1A(const std::string& objName = "exciterESAC1A_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  protected:
    double regulatorUpperLimit() const override;
    double regulatorLowerLimit() const override;

  private:
    model_parameter Vamax = 999.0;  //!< [pu] control-element upper limit
    model_parameter Vamin = -999.0;  //!< [pu] control-element lower limit
};
}  // namespace griddyn::exciters
