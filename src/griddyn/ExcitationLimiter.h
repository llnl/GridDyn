/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "GridSubModel.h"
#include "ControllerSignals.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn {

inline constexpr index_t limiterFieldCurrentInLocation = 0;
inline constexpr index_t limiterIdInLocation = 1;
inline constexpr index_t limiterIqInLocation = 2;
inline constexpr index_t limiterVdInLocation = 3;
inline constexpr index_t limiterVqInLocation = 4;
inline constexpr count_t excitationLimiterInputCount = 5;

/** A generic, instantaneous limiter action; this is not a WECC OEL/UEL model. */
class ExcitationLimiter: public GridSubModel {
  public:
    enum class Role { UNDER, OVER };

    explicit ExcitationLimiter(const std::string& objName = "limiter_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    Role role() const { return limiterRole; }
    virtual bool supportsRole(Role /*role*/) const { return true; }
    void setRole(Role role);
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param,
               units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs, const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs, const StateData& stateData, double resid[],
                  const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs, const StateData& stateData,
                         double update[], const SolverMode& sMode, double alpha) override;
    void jacobianElements(const IOdata& inputs, const StateData& stateData,
                          MatrixData<double>& matrixData, const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    const std::vector<stringVec>& inputNames() const override;
    const std::vector<stringVec>& outputNames() const override;

  private:
    struct Evaluation {
        double action = 0.0;
        std::array<double, excitationLimiterInputCount> derivatives{};
    };
    Evaluation evaluate(const IOdata& inputs) const;
    Role limiterRole = Role::OVER;
    double threshold = 0.0;
    double gain = 1.0;
    double maximumAction = 10.0;
    bool thresholdSet = false;
    bool roleInitialized = false;
};
}  // namespace griddyn
