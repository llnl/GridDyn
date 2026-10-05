/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** ANDES TG2 single lead-lag governor. */
class GovernorTG2 final: public Governor {
  public:
    explicit GovernorTG2(const std::string& name = "govTG2_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using Governor::set;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, double value, units::unit unitType = units::defunit) override;
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
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

  private:
    double dbL = -0.0001, dbU = 0.0001, dbC = 0.0;
    bool deadbandEnabled = false, hardLimitEnabled = true;
    double speedInput(const IOdata& inputs) const;
    double speedSlope(const IOdata& inputs) const;
    double output(const IOdata& inputs, double state) const;
    double outputSlope(const IOdata& inputs, double state) const;
};
}  // namespace griddyn::governors
