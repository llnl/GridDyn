/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "RenewableComponent.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn {

/** REECA1 electrical control: constant-Q branch with active reference filtering. */
class REECA1: public RenewableComponent {
  public:
    explicit REECA1(const std::string& name = "REECA1_#");
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
    stringVec localStateNames() const override;

  protected:
    virtual bool useVoltageInjection(double voltage) const { return dipMode(voltage); }

  private:
    double Vdip = 0.8, Vup = 1.2, Trv = 0.02;
    double dbd1 = -0.02, dbd2 = 0.02, Kqv = 1.0;
    double Iqh1 = 999.0, Iql1 = -999.0, Vref0 = 0.0;
    double Tp = 0.02, Tiq = 0.02, Tpord = 0.02, Tpfilt = 0.02;
    double dPmax = 999.0, dPmin = -999.0, PMAX = 999.0, PMIN = 0.0;
    double Imax = 999.0;
    double QMax = 999.0, QMin = -999.0;
    double VMAX = 999.0, VMIN = -999.0;
    double Vref1 = 1.0;
    double Kqp = 1.0, Kqi = 0.1, Kvp = 1.0, Kvi = 0.1;
    double Iqfrz = 0.0, Thld = 0.0, Thld2 = 0.0;
    int PFFLAG = 0, VFLAG = 1, QFLAG = 0, PFLAG = 0, PQFLAG = 1;
    std::array<double, 4> Vq{0.2, 0.4, 0.8, 1.0};
    std::array<double, 4> Iq{2.0, 4.0, 8.0, 10.0};
    std::array<double, 4> Vp{0.2, 0.4, 0.8, 1.0};
    std::array<double, 4> Ip{2.0, 4.0, 8.0, 12.0};
    double initialP = 0.0, initialQ = 0.0;
    double initialVref = 1.0;
    bool voltageDipActive = false;
    bool activeLimitHeld = false;
    CoreTime activeLimitRelease = 0.0;
    double heldActiveLimit = 0.0;
    double heldPowerOrder = 0.0;
    bool voltageDip(double v) const { return v < Vdip || v > Vup; }
    bool dipMode(double v) const { return Thld2 > 0.0 ? voltageDipActive : voltageDip(v); }
    static double curve(double voltage,
                        const std::array<double, 4>& points,
                        const std::array<double, 4>& currents);
    double activeLimit(double voltage, const double state[]) const;
    void transition(CoreTime time, double voltage, bool entering);
    std::array<double, 2> commands(double v, const double state[]) const;
    std::array<double, 4> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
