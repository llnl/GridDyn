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

/**
 * PSS/E WT3G1 doubly-fed induction generator electrical interface.
 *
 * Unlike REGC models, WT3G1 carries a PLL angle and inserts an equivalent
 * source behind reactance.  Its reactive command is a voltage (Eqcmd), not a
 * directly commanded reactive current.
 */
class WT3G1 final: public TerminalElectricalModel {
  public:
    explicit WT3G1(const std::string& name = "WT3G1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using TerminalElectricalModel::set;
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
    using TerminalElectricalModel::getOutput;
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
    double Xeq = 0.1, Kpll = 30.0, Kipll = 0.0, Pllmax = 0.1, Prated = 100.0;
    double heldIp = 0.0, heldEq = 0.0;
    std::array<double, 2> power(const IOdata& inputs, const double state[]) const;
    std::array<double, 4> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
