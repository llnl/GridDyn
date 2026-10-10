/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../ExcitationLimiter.h"

namespace griddyn::limiters {
/**
 * Restricted OEL3C summing-point limiter. Uses the machine field-current
 * route, fixed pickup reference, and the IEEE linear/quadratic pickup law.
 */
class ExcitationLimiterOEL3C final: public ExcitationLimiter {
  public:
    explicit ExcitationLimiterOEL3C(const std::string& objName = "oel3c_#");
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
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    struct Evaluation {
        double error = 0.0;
        double errorSlope = 0.0;
        double action = 0.0;
        double actionErrorSlope = 0.0;
        double actionIntegralSlope = 0.0;
    };
    Evaluation evaluate(double scaledFieldCurrent, double integral) const;
    double integralRate(double integral, double error) const;
    double fieldCurrent(const IOdata& inputs) const;

    double itfpu = 1.05;
    double kscale = 1.0;
    double tf = 0.02;
    int k1 = 1;
    double koel = 1.0;
    double toel = 24.0;
    double kpoel = 1.0;
    double voelMax1 = 0.66;
    double voelMin1 = -1.0;
    double voelMax2 = 0.0;
    double voelMin2 = -1.0;
    CoreTime prevTime = 0.0;
};
}  // namespace griddyn::limiters
