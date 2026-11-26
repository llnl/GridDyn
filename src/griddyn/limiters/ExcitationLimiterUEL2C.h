/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "LimiterDynamics.h"
#include <array>
#include <bitset>
#include <string>

namespace griddyn::limiters {
/**
 * IEEE UEL2C P-Q table, voltage bias, measurement/reference filters,
 * selectable gain reduction, excitation feedback, and limited PI/lead-lags.
 */
class ExcitationLimiterUEL2C final: public detail::LimiterDynamics<9> {
  public:
    const ModelDescription& getModelDescription() const override;
    explicit ExcitationLimiterUEL2C(const std::string& objName = "uel2c_#");
    using ExcitationLimiter::set;
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    bool supportsRole(Role role) const override { return role == Role::UNDER; }
    bool requiresExciterInput(index_t input) const override
    {
        return (input == limiterStabilizingFeedbackInLocation && kuf != 0.0) ||
            (input == limiterExciterFeedbackInLocation && kfb != 0.0);
    }
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

  private:
    struct CurveEvaluation {
        double qLimit = 0.0;
        double slope = 0.0;
    };
    CurveEvaluation curve(double normalizedP) const;
    detail::Evaluation<9> evaluate(const IOdata& inputs, const double states[]) const override;

    std::array<double, 10> pPoints{};
    std::array<double, 10> qPoints{};
    std::bitset<10> pConfigured;
    std::bitset<10> qConfigured;
    std::size_t pointCount = 0;
    int k1 = 0;
    int k2 = 0;
    int sw1 = 1;
    double tup = 0.0;
    double tuq = 0.0;
    double tuv = 0.0;
    double vBias = 1.0;
    double kuf = 0.0;
    double tqref = 0.0;
    double kfix = 1.0;
    double tadj = 0.0;
    double kfb = 0.0;
    double tul = 0.0;
    double xq = 1.0;
    double tu1 = 0.0;
    double tu2 = 0.0;
    double tu3 = 0.0;
    double tu4 = 0.0;
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
