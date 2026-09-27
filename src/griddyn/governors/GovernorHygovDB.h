/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GovernorHygov.h"
#include <string>
#include <vector>

namespace griddyn::governors {
/** ANDES HYGOVDB: HYGOV with a non-continuous speed dead zone in the controller. */
class GovernorHygovDB final: public GovernorHygov {
  public:
    explicit GovernorHygovDB(const std::string& name = "govHygovDB_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
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
    void set(std::string_view param, double value, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  private:
    double dbL = 0.0;
    double dbU = 0.0;
    double governorSpeedDeviation(const IOdata& inputs) const;
    double governorSpeedSlope(const IOdata& inputs) const;
    double gateRate(const double* x) const;
    double mechanicalPower(const IOdata& inputs, const double* x) const;
};
}  // namespace griddyn::governors
