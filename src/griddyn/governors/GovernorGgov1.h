/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** GE/PSS/E GGOV1 general-purpose turbine governor.
 *
 * Implements the OpenIPSL electrical-power transducer/reset controller,
 * selectable droop, deadband and PID, acceleration and temperature/load
 * limiters, rate- and position-limited valve, turbine lead-lag, and
 * speed-dependent fuel/power paths. The electrical-power input is terminal
 * active power, @f$P_e=V_d I_d+V_q I_q@f$, supplied by the GridDyn
 * synchronous-machine controller-signal interface. It is distinct from
 * air-gap torque by the stator copper loss when @f$R_s\ne0@f$.
 * The principal control equations are
 * @f[
 * T_{pelec}\dot P_f=P_e-P_f,\qquad
 * e=\operatorname{lim}_{minerr}^{maxerr}
 *   \{\operatorname{deadzone}[-\Delta\omega+P_{ref}+x_{mw}-R y_R]\},
 * @f]
 * @f[
 * FSR_N=K_{pgov}e+x_I+\frac{K_{dgov}}{T_{dgov}}(e-x_D),\qquad
 * FSR=\operatorname{lim}_{Vmin}^{Vmax}\min(FSR_N,FSR_T,FSR_A).
 * @f]
 * Here @f$y_R=P_f@f$ for Rselect=1, true valve stroke for -1, final
 * limited governor request for -2, and zero for 0. The actuator applies
 * Ropen/Rclose rate limits, and the fuel/turbine path is
 * @f$W_f=V@f$ (Flag=0) or @f$W_f=\omega V@f$ (Flag=1), followed by
 * @f$K_{turb}(W_f-W_{fnl})(1+T_c s)/(1+T_b s)@f$.
 *
 * @note A nonzero TENG requests a transport delay and is rejected explicitly;
 * silently omitting it would change the diesel-engine model. TRATE, RUP and
 * RDOWN are retained for DYR compatibility but are unused by OpenIPSL GGOV1.
 * @see OpenIPSL.Electrical.Controls.PSSE.TG.GGOV1
 */
class GovernorGgov1 final: public Governor {
  protected:
    int Rselect = 1;
    int fuelFlag = 1;
    model_parameter R = 0.04;
    model_parameter Tpelec = 1.0;
    model_parameter maxerr = 0.05;
    model_parameter minerr = -0.05;
    model_parameter Kpgov = 10.0;
    model_parameter Kigov = 2.0;
    model_parameter Kdgov = 0.0;
    model_parameter Tdgov = 1.0;
    model_parameter Tact = 0.5;
    model_parameter Kturb = 1.5;
    model_parameter Wfnl = 0.2;
    model_parameter Tb = 0.1;
    model_parameter Tc = 0.0;
    model_parameter Teng = 0.0;
    model_parameter Tfload = 3.0;
    model_parameter Kpload = 2.0;
    model_parameter Kiload = 0.67;
    model_parameter Ldref = 1.0;
    model_parameter Dm = 0.0;
    model_parameter Ropen = 0.1;
    model_parameter Rclose = -0.1;
    model_parameter Kimw = 0.0;
    model_parameter Aset = 0.1;
    model_parameter Ka = 10.0;
    model_parameter TaAccel = 0.1;
    model_parameter Trate = 0.0;
    model_parameter db = 0.0;
    model_parameter Tsa = 4.0;
    model_parameter Tsb = 5.0;
    model_parameter Rup = 99.0;
    model_parameter Rdown = -99.0;
    double powerReference = 0.0;

  public:
    explicit GovernorGgov1(const std::string& objName = "govGgov1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

  private:
    struct Signals;
    Signals evaluate(const IOdata& inputs, const double state[]) const;
};
}  // namespace griddyn::governors
