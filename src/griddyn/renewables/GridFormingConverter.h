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

namespace griddyn {

/** Shared terminal equations for the ANDES voltage-controlled and grid-forming converters. */
class GridFormingConverter: public TerminalElectricalModel {
  public:
    enum class Variant { cv1, cv2, f1, f2, f3 };
    explicit GridFormingConverter(Variant variant = Variant::cv1,
                                  const std::string& name = "gridFormingConverter_#");
    explicit GridFormingConverter(const std::string& name);
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    std::span<const RenewablePort> inputPorts() const override;
    std::span<const RenewablePort> outputPorts() const override;
    std::string_view sourceName(RenewableSignal signal) const override;
    using TerminalElectricalModel::set;
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
    stringVec localStateNames() const override;

  protected:
    Variant variant;
    // Parameters are kept in a fixed table so model variants share validation and DYR binding.
    std::array<double, 39> parameters{};
    std::string pllName;
    double initialActivePower = 0.0;
    double initialReactivePower = 0.0;
    double initialVoltage = 1.0;

  private:
    index_t stateCount() const;
    void evaluate(const IOdata& inputs,
                  const double* alg,
                  const double* state,
                  double* powerResidual,
                  double* rates) const;
};

class REGCV1 final: public GridFormingConverter {
  public:
    explicit REGCV1(const std::string& name = "REGCV1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class REGCV2 final: public GridFormingConverter {
  public:
    explicit REGCV2(const std::string& name = "REGCV2_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class REGF1 final: public GridFormingConverter {
  public:
    explicit REGF1(const std::string& name = "REGF1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class REGF2 final: public GridFormingConverter {
  public:
    explicit REGF2(const std::string& name = "REGF2_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class REGF3 final: public GridFormingConverter {
  public:
    explicit REGF3(const std::string& name = "REGF3_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};

}  // namespace griddyn
