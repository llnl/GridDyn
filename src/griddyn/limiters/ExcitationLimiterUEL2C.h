/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../ExcitationLimiter.h"
#include <array>

namespace griddyn::limiters {
/**
 * UEL2C piecewise P-Q characteristic and PI limiter.
 * This implementation is restricted to the unfiltered, fixed-gain profile.
 */
class ExcitationLimiterUEL2C final: public ExcitationLimiter {
  public:
    explicit ExcitationLimiterUEL2C(const std::string& objName = "uel2c_#");
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
    struct CurveEvaluation {
        double qLimit = 0.0;
        double slope = 0.0;
    };
    struct Evaluation {
        double error = 0.0;
        double output = 0.0;
        std::array<double, excitationLimiterInputCount> errorDerivatives{};
        double outputErrorSlope = 0.0;
        double outputIntegralSlope = 0.0;
    };
    CurveEvaluation curve(double normalizedP) const;
    Evaluation evaluate(const IOdata& inputs, double integral) const;
    double integralRate(double integral, double error) const;

    std::array<double, 7> pPoints{};
    std::array<double, 7> qPoints{};
    std::size_t pointCount = 0;
    int k1 = 0;
    int k2 = 0;
    double kuI = 0.5;
    double kuL = 0.8;
    double vuiMax = 0.25;
    double vuiMin = 0.0;
    double vulMax1 = 0.25;
    double vulMin1 = 0.0;
    double vulMax2 = 0.25;
    double vulMin2 = 0.0;
    CoreTime prevTime = 0.0;
};
}  // namespace griddyn::limiters
