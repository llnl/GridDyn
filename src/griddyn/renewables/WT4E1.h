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

/** PSS/E WT4E1 electrical controller for a full-converter wind turbine. */
class WT4E1 final: public RenewableComponent {
  public:
    explicit WT4E1(const std::string& name = "WT4E1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    RenewableRole role() const override { return RenewableRole::electricalControl; }
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
    IOdata getOutputs(const IOdata& inputs,
                      const StateData& stateData,
                      const SolverMode& sMode) const override;
    using RenewableComponent::getOutput;
    double getOutput(const IOdata& inputs,
                     const StateData& stateData,
                     const SolverMode& sMode,
                     index_t outputNum) const override;
    index_t getOutputLoc(const SolverMode& sMode, index_t outputNum) const override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    int pfFast = 0, windVar = 0, pqPriority = 1, psseMatch = 0;
    double Tfv = 0.02, Kpv = 1.0, Kiv = 0.1, Kpp = 1.0, Kip = 0.1, Kf = 0.0, Tf = 0.02;
    double Qmax = 999.0, Qmin = -999.0, Ipmax = 999.0, Trv = 0.02, dPmax = 999.0, dPmin = -999.0;
    double Tpower = 0.02, Tiq = 0.02, Kqi = 0.0, Vmincl = 0.9, Vmaxcl = 1.1, Kvi = 0.0, Tv = 0.02,
           Tp = 0.02;
    double Imax = 999.0, Iphl = 999.0, Iqhl = 999.0;
    double initialP = 0.0, initialQ = 0.0, vref = 1.0;
    std::array<double, 2> commands(const IOdata& inputs, const double state[]) const;
    std::array<double, 5> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
