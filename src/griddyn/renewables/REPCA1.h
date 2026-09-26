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

/** REPCA1 local plant reactive/voltage controller; frequency and remote modes fail closed. */
class REPCA1 final: public RenewableComponent {
  public:
    explicit REPCA1(const std::string& name = "REPCA1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    RenewableRole role() const override { return RenewableRole::plantControl; }
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
    int VCFlag = 0, RefFlag = 1, Fflag = 0, PLflag = 1;
    double Tfltr = 0.02, Kp = 1.0, Ki = 0.1, Tft = 1.0, Tfv = 1.0;
    double Vfrz = 0.8, Rc = 0.0, Xc = 0.0, Kc = 0.0;
    double emax = 999.0, emin = -999.0, dbd1 = -0.1, dbd2 = 0.1;
    double Qmax = 999.0, Qmin = -999.0;
    double Kpg = 1.0, Kig = 0.1, Tp = 0.02;
    double fdbd1 = -0.0002833, fdbd2 = 0.0002833;
    double femax = 0.05, femin = -0.05, Pmax = 999.0, Pmin = -999.0;
    double Tg = 0.02, Ddn = 10.0, Dup = 10.0;
    double vReference = 1.0, qReference = 0.0;
    double controllerError(double v, const double state[]) const;
    double piOutput(double v, const double state[]) const;
    double reactiveIncrement(double v, const double state[]) const;
    std::array<double, 4> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
