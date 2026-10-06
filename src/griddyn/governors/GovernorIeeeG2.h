/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** IEEE Type 2 hydraulic speed-governing model (PSS/E IEEEG2).
 *
 * The implementation follows the OpenIPSL IEEEG2 block diagram.  The first
 * lead-lag uses T1 as its denominator time constant and T2 as its numerator
 * time constant, followed by a T3 lag, the PMECH0 summing junction, the
 * [PMIN, PMAX] limiter, and the water-starting-time lead-lag.
 * The PSS/E DYR field order is K, T1, T2, T3, PMAX, PMIN, T4; this differs
 * from the order of the model parameters in the OpenIPSL declaration.
 */
class GovernorIeeeG2: public Governor {
  protected:
    model_parameter T4 = 1.5;
    index_t leadLagState = kInvalidLocation;
    index_t simpleLagState = kInvalidLocation;
    index_t waterState = kInvalidLocation;

  public:
    explicit GovernorIeeeG2(const std::string& objName = "govIeeeG2_#");
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

    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    const std::vector<stringVec>& outputNames() const override;

  private:
    double leadLagOutput(const IOdata& inputs, const double state[]) const;
    double speedLagOutput(const IOdata& inputs, const double state[]) const;
    double limitedInput(const IOdata& inputs, const double state[]) const;
    double waterOutput(const IOdata& inputs, const double state[]) const;
    double limiterSlope(const IOdata& inputs, const double state[]) const;
};
}  // namespace griddyn::governors
