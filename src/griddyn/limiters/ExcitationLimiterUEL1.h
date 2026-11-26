/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "LimiterDynamics.h"
#include <array>
#include <string>

namespace griddyn::limiters {
/** IEEE UEL1 circular characteristic, excitation feedback, and limited PI/lead-lags. */
class ExcitationLimiterUEL1 final: public detail::LimiterDynamics<3> {
  public:
    const ModelDescription& getModelDescription() const override;
    explicit ExcitationLimiterUEL1(const std::string& objName = "uel1_#");
    using ExcitationLimiter::set;
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    bool supportsRole(Role role) const override { return role == Role::UNDER; }
    bool requiresExciterInput(index_t input) const override
    {
        return input == limiterStabilizingFeedbackInLocation && kuf != 0.0;
    }
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

  private:
    detail::Signal<3> characteristic(const IOdata& inputs) const;
    detail::Evaluation<3> evaluate(const IOdata& inputs, const double states[]) const override;

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
