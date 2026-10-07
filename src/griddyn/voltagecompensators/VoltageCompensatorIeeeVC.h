/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../VoltageCompensator.h"

#include <string>

namespace griddyn::voltagecompensators {
/** IEEE voltage-regulator current-compensating model (PSS/E IEEEVC). */
class VoltageCompensatorIeeeVC: public VoltageCompensator {
  protected:
    model_parameter RC = 0.0;
    model_parameter XC = 0.0;

  public:
    explicit VoltageCompensatorIeeeVC(const std::string& objName = "ieeevc_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
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
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    double compensatedVoltage(const IOdata& inputs) const;
};
}  // namespace griddyn::voltagecompensators
