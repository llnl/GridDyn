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

/** PSS/E WT3E1 electrical controller for a WT3G DFIG interface. */
class WT3E1 final: public RenewableComponent {
  public:
    explicit WT3E1(const std::string& name = "WT3E1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using RenewableComponent::set;
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
    int varFlag = 0, vlrFlag = 1;
    double Tfv = .02, Kpv = 1.0, Kiv = .1, Xc = 0.0, Tfp = .02, Kpp = 1.0, Kip = .1, Pmax = 1.2,
           Pmin = .0, Qmax = 999.0, Qmin = -999.0, Ipmax = 999.0, Trv = .02, Rpmax = 999.0,
           Rpmin = -999.0, Tpower = .02, Kqi = .0, Vmincl = .9, Vmaxcl = 1.2, Kqv = .0,
           Xiqmin = -1.0, Xiqmax = 1.0, Tv = .02, Tp = .02, Fn = 1.0, Wpmin = .7, Wp20 = .8,
           Wp40 = .9, Wp60 = 1.0, PminSpeed = 1.0, Wp100 = 1.1;
    double initialP = 0.0, initialQ = 0.0, vref = 1.0, initialEq = 0.0;
    std::array<double, 3> commands(const IOdata& inputs, const double state[]) const;
    std::array<double, 4> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
