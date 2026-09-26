/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "RenewableComponent.h"
#include <string>
#include <array>

namespace griddyn {

/** Two-mass wind turbine and generator shaft. Powers use machine base. */
class WTDTA1 final: public RenewableComponent {
  public:
    explicit WTDTA1(const std::string& name = "WTDTA1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    RenewableRole role() const override { return RenewableRole::driveTrain; }
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
    void set(std::string_view param,double val,units::unit unitType=units::defunit) override;
    double get(std::string_view param,units::unit unitType=units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0,std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,const StateData& stateData,double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,const StateData& stateData,double deriv[],
                    const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,const StateData& stateData,
                          MatrixData<double>& matrixData,const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time,const IOdata& inputs,const SolverMode& sMode) override;
    stringVec localStateNames() const override;

  private:
    double H=3.0,DAMP=0.0,Htfrac=0.5,Freq1=1.0,Dshaft=1.0,w0=1.0;
    double initialPower=0.0,operatingSpeed=1.0;
    std::array<double,3> rates(const IOdata& inputs,const double state[]) const;
};

} // namespace griddyn
