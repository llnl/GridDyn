/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** PSS/E GAST gas-turbine governor.
 *
 * OpenIPSL, ANDES, and GridKit GASTPTI use the same three-state realization.
 * For speed deviation @f$\Delta\omega=\omega-1@f$,
 * @f[
 * V_D=P_{ref}-\frac{\Delta\omega}{R},\qquad
 * V_T=A_T+K_T(A_T-x_T),\qquad V=\min(V_D,V_T),
 * @f]
 * @f[
 * T_1\dot x_V=V-x_V,\qquad
 * T_2\dot x_F=x_V-x_F,\qquad
 * T_3\dot x_T=x_F-x_T,
 * @f]
 * @f[
 * P_m=x_F-D_{turb}\Delta\omega.
 * @f]
 * The valve derivative is blocked only when it would move @f$x_V@f$ farther
 * outside its response bounds. If the initialized flow lies outside the
 * entered @f$[V_{min},V_{max}]@f$ range, the corresponding response bound is
 * extended to that initial value, matching GridKit and avoiding an initial
 * discontinuity. Consistent with GridKit GASTPTI, accepted zero time constants
 * are evaluated with a 1 ms floor. GridDyn currently uses the generator/machine
 * base already established by DynamicGenerator; the GridKit-only optional
 * TRATE base conversion is not part of a PSS/E GAST DYR record.
 */
class GovernorGast final: public Governor {
  protected:
    model_parameter R = 0.05;
    model_parameter AT = 1.0;
    model_parameter KT = 2.0;
    model_parameter Dt = 0.0;

  public:
    explicit GovernorGast(const std::string& objName = "govGast_#");
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
    static constexpr double minimumTimeConstant = 1e-3;
    double effectiveT1 = 0.4;
    double effectiveT2 = 0.1;
    double effectiveT3 = 3.0;
    double responseMaximum = 1.0;
    double responseMinimum = 0.0;
    double speedDroopRequest(const IOdata& inputs) const;
    double temperatureRequest(const double state[]) const;
    double valveDerivative(const IOdata& inputs, const double state[]) const;
    double mechanicalPower(const IOdata& inputs, const double state[]) const;
};
}  // namespace griddyn::governors
