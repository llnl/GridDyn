/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../ExcitationLimiter.h"
#include <array>
#include <string>

namespace griddyn::limiters {
/** IEEE UEL1 circular characteristic with the AVR stabilizing input disabled. */
class ExcitationLimiterUEL1 final: public ExcitationLimiter {
  public:
    explicit ExcitationLimiterUEL1(const std::string& objName = "uel1_#");
    using ExcitationLimiter::set;
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    bool supportsRole(Role role) const override { return role == Role::UNDER; }
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
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

  private:
    struct Characteristic {
        double error = 0.0;
        std::array<double, excitationLimiterInputCount> derivatives{};
    };
    struct Control {
        double action = 0.0;
        double errorGain = 0.0;
        double integralGain = 0.0;
        double firstLagGain = 0.0;
        double secondLagGain = 0.0;
    };
    Characteristic characteristic(const IOdata& inputs) const;
    Control control(const IOdata& inputs, double integral, double lag1, double lag2) const;
    double integralRate(double integral, double error) const;

    double kur = 1.95;
    double kuc = 1.38;
    double vurMax = 5.8;
    double vucMax = 5.8;
    double kuI = 0.0;
    double kuL = 100.0;
    double vuiMax = 18.0;
    double vuiMin = 0.0;
    double tu1 = 0.0;
    double tu2 = 0.05;
    double tu3 = 0.0;
    double tu4 = 0.0;
    double vulMax = 18.0;
    double vulMin = 0.0;
    double kuf = 0.0;
    CoreTime prevTime = 0.0;
};
}  // namespace griddyn::limiters
