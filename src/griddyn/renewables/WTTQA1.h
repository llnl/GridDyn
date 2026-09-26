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

/** Wind torque controller producing an absolute REECA1 active reference. */
class WTTQA1 final: public RenewableComponent {
  public:
    explicit WTTQA1(const std::string& name = "WTTQA1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    RenewableRole role() const override { return RenewableRole::torqueControl; }
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
    using RenewableComponent::getOutput;
    double getOutput(const IOdata& inputs,
                     const StateData& stateData,
                     const SolverMode& sMode,
                     index_t outputNum) const override;
    index_t getOutputLoc(const SolverMode& sMode, index_t outputNum) const override;
    stringVec localStateNames() const override;

  private:
    int Tflag = 0;
    double Kpp = 0.0, Kip = 0.1, Tp = 0.05, Twref = 30.0, Temax = 1.2, Temin = 0.0, TRATE = 999.0;
    std::array<double, 4> power{0.2, 0.4, 0.6, 0.8};
    std::array<double, 4> speed{0.58, 0.72, 0.86, 1.0};
    double initialPower = 0.0;
    double curve(double electricalPower) const;
    double error(const IOdata& inputs, const double state[]) const;
    double reference(const IOdata& inputs, const double state[]) const;
    std::array<double, 3> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
