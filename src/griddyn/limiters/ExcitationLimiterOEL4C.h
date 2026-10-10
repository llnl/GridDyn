/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../ExcitationLimiter.h"
#include <string>
#include <vector>

namespace griddyn::limiters {
/** Reactive-power delayed PI OEL4C; output is a positive VOEL magnitude. */
class ExcitationLimiterOEL4C final: public ExcitationLimiter {
  public:
    explicit ExcitationLimiterOEL4C(const std::string& objName = "oel4c_#");
    using ExcitationLimiter::set;
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    bool supportsRole(Role role) const override { return role == Role::OVER; }
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
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    struct ReactivePower {
        double value;
        double dId;
        double dIq;
        double dVd;
        double dVq;
    };
    static ReactivePower reactivePower(const IOdata& inputs);
    double piInput(double reactivePowerValue, CoreTime time) const;
    double action(double input, double integral) const;
    double integralRate(double input, double integral) const;
    void updateTimer(bool violation, CoreTime time);

    double ki = 1.0;
    double kp = 1.0;
    double delay = 0.0;
    double minimum = -0.2;
    double qRef = 0.0;
    bool qRefSet = false;
    bool violating = false;
    bool delayElapsed = false;
    CoreTime violationStart = 0.0;
};
}  // namespace griddyn::limiters
