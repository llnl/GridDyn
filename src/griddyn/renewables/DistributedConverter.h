/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RenewableComponent.h"
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {

/** Distributed PV, storage, and vehicle current-source converter equations. */
class DistributedConverter: public TerminalElectricalModel {
  public:
    enum class Variant { pv, storage, ev1, ev2 };

    DistributedConverter(Variant type, const std::string& name);
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
    std::string_view sourceName(RenewableSignal signal) const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void set(std::string_view param, std::string_view val) override;
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
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    IOdata getOutputs(const IOdata& inputs,
                      const StateData& stateData,
                      const SolverMode& sMode) const override;
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
    void copyParametersTo(DistributedConverter* target) const;

  private:
    Variant variant;
    double fn = 60.0, xc = 0.0, pqflag = 1.0;
    double qmx = 0.33, qmn = -0.33, pmx = 9999.0, pmn = -9999.0;
    double v0 = 0.8, v1 = 1.1, dqdv = -1.0, fdbd = -0.017, ddn = 0.0;
    double ialim = 1.3, vt0 = 0.88, vt1 = 0.90, vt2 = 1.1, vt3 = 1.2;
    double ft0 = 59.5, ft1 = 59.7, ft2 = 60.3, ft3 = 60.5;
    double vrflag = 0.0, frflag = 0.0, recflag = 1.0;
    double tip = 0.02, tiq = 0.02;
    double pref = 0.0, qref = 0.0, paux = 0.0, pcap = 1.0;
    double tf = 1.0, socmin = 0.0, socmax = 1.0, socinit = 0.5;
    double en = 100.0, etac = 1.0, etad = 1.0;
    double systemMva = 100.0;
    bool blocked = false, voltageLatched = false, frequencyLatched = false;
    bool prefConfigured = false, qrefConfigured = false;
    std::string frequencySensor;
    std::string regulationBus;

    count_t stateCount() const;
    void updateLatches(const IOdata& inputs);
    void evaluate(const IOdata& inputs,
                  const double state[],
                  std::array<double, 2>& power,
                  std::array<double, 3>& rates) const;
};

class PVD1 final: public DistributedConverter {
  public:
    explicit PVD1(const std::string& name = "PVD1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class ESD1 final: public DistributedConverter {
  public:
    explicit ESD1(const std::string& name = "ESD1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class EV1 final: public DistributedConverter {
  public:
    explicit EV1(const std::string& name = "EV1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class EV2 final: public DistributedConverter {
  public:
    explicit EV2(const std::string& name = "EV2_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
}  // namespace griddyn
