/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../ExcitationLimiter.h"
#include <array>

namespace griddyn::limiters {
/** PSS/E MNLEX2 circular P-Q underexcitation limiter. */
class ExcitationLimiterMNLEX2 final: public ExcitationLimiter {
  public:
    explicit ExcitationLimiterMNLEX2(const std::string& objName = "mnlex2_#");
    using ExcitationLimiter::set;
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    bool supportsRole(Role role) const override { return role == Role::UNDER; }
    void set(std::string_view param, double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param,
               units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs, const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs, const StateData& stateData, double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs, const StateData& stateData, double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs, const StateData& stateData,
                         double update[], const SolverMode& sMode, double alpha) override;
    void jacobianElements(const IOdata& inputs, const StateData& stateData,
                          MatrixData<double>& matrixData, const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    struct Circle {
        double error = 0.0;
        std::array<double, excitationLimiterInputCount> derivatives{};
    };
    Circle circleError(const IOdata& inputs) const;
    double rate(double output, double feedback, double error) const;
    double kF2 = 0.1;
    double tF2 = 1.0;
    double kM = 0.3;
    double tM = 0.5;
    double melMax = 0.1;
    double q0 = 2.5;
    double radius = 3.0;
};
}  // namespace griddyn::limiters
