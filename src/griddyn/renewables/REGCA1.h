/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "RenewableComponent.h"
#include <string>

namespace griddyn {

/** REGCA1 converter current lag, voltage-dependent current limits and P/Q injection. */
class REGCA1 final: public TerminalElectricalModel {
  public:
    explicit REGCA1(const std::string& name = "REGCA1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
    void set(std::string_view param, double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param,
               units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs, const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs, const StateData& stateData,
                  double resid[], const SolverMode& sMode) override;
    void derivative(const IOdata& inputs, const StateData& stateData,
                    double deriv[], const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs, const StateData& stateData,
                         double update[], const SolverMode& sMode, double alpha) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs, const StateData& stateData,
                          MatrixData<double>& matrixData, const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    IOdata getOutputs(const IOdata& inputs, const StateData& stateData,
                      const SolverMode& sMode) const override;
    void outputPartialDerivatives(const IOdata& inputs, const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    double Tg = 0.1;
    double Rrpwr = 10.0;
    double Brkpt = 1.0;
    double Zerox = 0.5;
    double Lvpl1 = 1.0;
    double Volim = 1.2;
    double Lvpnt1 = 0.8;
    double Lvpnt0 = 0.4;
    double Iolim = -1.5;
    double Tfltr = 0.1;
    double Khv = 0.7;
    double Iqrmax = 1.0;
    double Iqrmin = -1.0;
    double Accel = 0.0;
    double initialReactivePower = 0.0;
    bool Lvplsw = true;
    double heldIpCommand = 0.0;
    double heldIqCommand = 0.0;
    double lowVoltageGain(double voltage) const;
    double lowVoltagePowerLimit(double filteredVoltage) const;
    double activeCommand(const IOdata& inputs) const;
    double reactiveCommand(const IOdata& inputs) const;
    double limitReactiveRate(double rate) const;
    bool reactiveRateFree(double rate) const;
};

}  // namespace griddyn
