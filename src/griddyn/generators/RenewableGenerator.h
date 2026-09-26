/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Generator.h"
#include "../renewables/RenewableComponent.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn {

/** Generator host for independently replaceable renewable dynamic components. */
class RenewableGenerator: public Generator {
  public:
    explicit RenewableGenerator(const std::string& name = "renGen_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    using Generator::add;
    void add(GridSubModel* obj) override;
    void remove(CoreObject* obj) override;
    CoreObject* find(std::string_view object) const override;
    CoreObject* getSubObject(std::string_view typeName, index_t num) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void setState(CoreTime time, const double state[], const double dstateDt[],
                  const SolverMode& sMode) override;
    void guessState(CoreTime time, double state[], double dstateDt[],
                    const SolverMode& sMode) override;
    void residual(const IOdata& inputs,
                  const StateData& stateDataValue,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateDataValue,
                    double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs, const StateData& stateDataValue,
                         double update[], const SolverMode& sMode, double alpha) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateDataValue,
                          MatrixData<double>& matrixDataValue,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    IOdata getOutputs(const IOdata& inputs,
                      const StateData& stateDataValue,
                      const SolverMode& sMode) const override;
    double getRealPower(const IOdata& inputs,
                        const StateData& stateDataValue,
                        const SolverMode& sMode) const override;
    double getReactivePower(const IOdata& inputs,
                            const StateData& stateDataValue,
                            const SolverMode& sMode) const override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const SolverMode& sMode) override;
    void ioPartialDerivatives(const IOdata& inputs,
                              const StateData& stateDataValue,
                              MatrixData<double>& matrixDataValue,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode) override;
    void rootTest(const IOdata& inputs, const StateData& stateDataValue,
                  double roots[], const SolverMode& sMode) override;
    void rootTrigger(CoreTime time, const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;
    ChangeCode rootCheck(const IOdata& inputs, const StateData& stateDataValue,
                         const SolverMode& sMode, CheckLevel level) override;
    count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
    void getStateName(stringVec& stNames, const SolverMode& sMode,
                      const std::string& prefix) const override;

  private:
    static constexpr std::size_t roleCount = static_cast<std::size_t>(RenewableRole::count);
    std::array<RenewableComponent*, roleCount> components{};
    TerminalElectricalModel* electricalModel = nullptr;

    IOdata modelInputs(const RenewableComponent* model,
                       const IOdata& inputs,
                       const StateData& stateDataValue,
                       const SolverMode& sMode) const;
    IOlocs modelInputLocs(const RenewableComponent* model,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) const;
    void validateAssembly() const;
    static std::size_t roleIndex(RenewableRole role);
};

}  // namespace griddyn
