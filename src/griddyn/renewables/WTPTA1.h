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

/** Wind pitch controller with speed and power compensation PI paths. */
class WTPTA1 final: public RenewableComponent {
  public:
    explicit WTPTA1(const std::string& name = "WTPTA1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    RenewableRole role() const override { return RenewableRole::pitchControl; }
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
    stringVec localStateNames() const override;

  private:
    double Kiw = 0.1, Kpw = 0.0, Kic = 0.1, Kpc = 0.0, Kcc = 0.0, Tp = 0.3;
    double thetaMax = 30.0, thetaMin = 0.0, rateMax = 5.0, rateMin = -5.0;
    double initialSpeed = 1.0;
    std::array<double, 3> rates(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn
