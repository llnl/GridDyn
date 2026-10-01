/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Sensor.h"
#include <vector>

namespace griddyn {
class DynamicGenerator;

/** Area-scoped inertia-weighted synchronous-machine frequency and angle measurement. */
class COISensor final: public Sensor {
  public:
    explicit COISensor(const std::string& name = "COI_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    double getOutput(const IOdata& inputs,
                     const StateData& stateData,
                     const SolverMode& sMode,
                     index_t outNum = 0) const override;
    double getOutput(index_t outNum = 0) const override;
    index_t getOutputLoc(const SolverMode& sMode, index_t outNum) const override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
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
    stringVec localStateNames() const override;

  private:
    struct Machine {
        DynamicGenerator* generator;
        double weight;
    };
    std::vector<Machine> machines;
};

}  // namespace griddyn
