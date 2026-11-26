/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "LimiterDynamics.h"
#include <string>
#include <vector>

namespace griddyn::limiters {
/**
 * IEEE OEL3C summing-point limiter with selectable Ifd/Efd/VFE measurement,
 * power-law pickup, a limited integrator, and its proportional-action switch.
 */
class ExcitationLimiterOEL3C final: public detail::LimiterDynamics<2> {
  public:
    const ModelDescription& getModelDescription() const override;
    explicit ExcitationLimiterOEL3C(const std::string& objName = "oel3c_#");
    using ExcitationLimiter::set;
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    bool supportsRole(Role role) const override { return role == Role::OVER; }
    bool requiresExciterInput(index_t input) const override
    {
        return (oelInput == 1 && input == limiterFieldVoltageInLocation) ||
            (oelInput == 2 && input == limiterExciterFieldCurrentInLocation);
    }
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void rootTest(const IOdata& inputs,
                  const StateData& data,
                  double roots[],
                  const SolverMode& mode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& mode) override;
    ChangeCode rootCheck(const IOdata& inputs,
                         const StateData& data,
                         const SolverMode& mode,
                         CheckLevel level) override;

  private:
    detail::Evaluation<2> evaluate(const IOdata& inputs, const double states[]) const override;
    double measurement(const IOdata& inputs) const;
    index_t measurementLocation() const;

    double itfpu = 1.05;
    double kscale = 1.0;
    double tf = 0.02;
    int k1 = 1;
    int oelInput = 0;
    double koel = 1.0;
    double toel = 24.0;
    double kpoel = 1.0;
    double voelMax1 = 0.66;
    double voelMin1 = -1.0;
    double voelMax2 = 0.0;
    double voelMin2 = -1.0;
    bool proportionalActive = false;
    CoreTime prevTime = 0.0;
};
}  // namespace griddyn::limiters
