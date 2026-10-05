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
 * PSS/E WT4G1 full-converter interface.
 *
 * The model has independent active/reactive current lags, low-voltage active
 * current management (LVPL/LVACL), and high-voltage reactive-current
 * management (HVRCL).  Powers and current commands use machine base.
 */
class WT4G1 final: public TerminalElectricalModel {
  public:
    explicit WT4G1(const std::string& name = "WT4G1_#");
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
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    double TIqcmd = 0.02, TIpCmd = 0.02;
    double Vlvpl1 = 0.4, Vlvpl2 = 0.8, Glvpl = 1.0;
    double Vhvrcr = 1.2, Curhvrcr = 0.0, RIpLvpl = 999.0, TLvpl = 0.02;
    double heldIp = 0.0, heldIq = 0.0;
    double activeLimit(double voltage) const;
    static double activeVoltageLimit(double voltage, double current);
    std::array<double, 3> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
