/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <string>
#include <vector>

namespace griddyn::governors {
/**
 * @brief PSS/E HYGOV hydro turbine governor.
 *
 * This implementation follows the OpenIPSL/PowerDynamics PSS/E HYGOV equations:
 * filtered speed error, temporary-droop lead block, gate servo, nonlinear
 * water-column flow/head, no-load flow, and turbine damping.
 *
 * With speed deviation @f$\Delta\omega=\omega-1@f$, the continuous equations are
 * @f[
 * T_f\dot{x}=P_{ref}-\Delta\omega-Rc-x,
 * \quad v=\operatorname{limit}_{-VELM}^{VELM}
 * \left(\frac{T_r\dot{x}+x}{rT_r}\right),
 * @f]
 * with @f$\dot c=v@f$ except when that rate would move @f$c@f$ beyond
 * @f$[G_{min},G_{max}]@f$, where the outward rate is zero, and
 * @f[
 * T_g\dot{G}=c-G,
 * \quad T_w\dot{Q}=h_0-(Q/G)^2,
 * \quad P_m=A_t(Q/G)^2(Q-q_{NL})-D_{turb}\Delta\omega G.
 * @f]
 * The reference is initialized as @f$P_{ref}=R c_0@f$, matching the PSS/E
 * model's internally initialized speed reference rather than an external
 * dispatch input.
 */
class GovernorHygov: public Governor {
  public:
    enum HygovFlags {
        GATE_RATE_LIMITED = OBJECT_FLAG9,
        GATE_RATE_LIMIT_HIGH = OBJECT_FLAG10,
        GATE_POSITION_LIMITED = OBJECT_FLAG11,
        GATE_POSITION_LIMIT_HIGH = OBJECT_FLAG12,
    };

  protected:
    model_parameter temporaryDroop = 0.3;  //!< temporary droop r
    model_parameter Tr = 5.0;  //!< temporary-droop lead time constant
    model_parameter Tf = 0.05;  //!< speed-error filter time constant
    model_parameter Tg = 0.5;  //!< gate servo time constant
    model_parameter Tw = 1.25;  //!< water inertia time constant
    model_parameter VELM = 0.2;  //!< gate velocity magnitude limit
    model_parameter At = 1.2;  //!< turbine gain
    model_parameter Dturb = 0.2;  //!< turbine damping
    model_parameter qNL = 0.08;  //!< no-load flow at nominal head
    model_parameter h0 = 1.0;  //!< initial/head reference used by OpenIPSL

  public:
    explicit GovernorHygov(const std::string& objName = "govHygov_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    ~GovernorHygov() override;

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

    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;
    ChangeCode rootCheck(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode,
                         CheckLevel level) override;

    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

  private:
    static double speedDeviation(const IOdata& inputs);
    double governorError(const IOdata& inputs, const double diffState[]) const;
    double filterDerivative(const IOdata& inputs, const double diffState[]) const;
    double temporaryDroopLeadOutput(const IOdata& inputs, const double diffState[]) const;
    static double regularizedGate(const double diffState[]);
    double turbineHead(const double diffState[]) const;
    double mechanicalPower(const IOdata& inputs, const double diffState[]) const;
    double unlimitedGateRate(const IOdata& inputs, const double diffState[]) const;
    double limitedGateRate(const IOdata& inputs, const double diffState[]) const;
    int gateRateLimitStatus(const IOdata& inputs, const double diffState[]) const;
    int gatePositionLimitStatus(const IOdata& inputs, const double diffState[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double diffState[]);
};
}  // namespace griddyn::governors
