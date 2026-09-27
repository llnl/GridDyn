/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "RenewableComponent.h"
#include <array>
#include <string>

namespace griddyn {

/** Continuous bus-angle frequency and ROCOF measurement for renewable controls. */
class BusROCOF final: public RenewableComponent {
  public:
    explicit BusROCOF(const std::string& name = "BusROCOF_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    RenewableRole role() const override { return RenewableRole::measurement; }
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
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
    double Tf = 0.02;
    double Tw = 0.1;
    double Tr = 0.1;
    double fn = 60.0;
    double initialAngle = 0.0;
    double lastAngle = 0.0;
    double deviation(const double state[]) const;
    std::array<double, 3> rates(double angle, const double state[]) const;
};

}  // namespace griddyn
