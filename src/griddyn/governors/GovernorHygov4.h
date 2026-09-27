/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** ANDES HYGOV4 pilot servo, washout, gate, and water column. */
class GovernorHygov4 final: public Governor {
  public:
    explicit GovernorHygov4(const std::string& name = "govHygov4_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, double value, units::unit unitType = units::defunit) override;
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
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

  private:
    double Rperm = 0.5, Rtemp = 1.0, UO = 1.0, UC = 0.0;
    double Tp = 0.05, Tg = 0.05, Tr = 0.05, Tw = 1.0;
    double At = 1.0, Dturb = 0.0, Hdam = 1.0, qNL = 0.1, paux = 0.0;
    double referenceOffset = 0.0;
    static double regularizedGate(double gate);
    double mechanicalPower(const IOdata& inputs, const double* state) const;
    double gateRate(const double* state) const;
};
}  // namespace griddyn::governors
