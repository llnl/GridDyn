/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "RenewableComponent.h"
#include <string>

namespace griddyn {

/** One-mass wind drivetrain with a shared generator and turbine speed. */
class WTDS final: public RenewableComponent {
  public:
    explicit WTDS(const std::string& name = "WTDS_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using RenewableComponent::set;
    RenewableRole role() const override { return RenewableRole::driveTrain; }
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
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
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    using RenewableComponent::getOutput;
    double getOutput(const IOdata& inputs,
                     const StateData& stateData,
                     const SolverMode& sMode,
                     index_t outputNum) const override;
    index_t getOutputLoc(const SolverMode& sMode, index_t outputNum) const override;
    stringVec localStateNames() const override;

  private:
    double H = 3.0;
    double D = 1.0;
    double w0 = 1.0;
    double initialPower = 0.0;
    double operatingSpeed = 1.0;
    double rate(const IOdata& inputs, double speed) const;
};

}  // namespace griddyn
