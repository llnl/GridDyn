/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Governor.h"
#include <string>

namespace griddyn::governors {
/** Shared TGOV1 variants. State order: mechanical output, valve, turbine lag. */
class GovernorTgov1Variant: public Governor {
  public:
    GovernorTgov1Variant(bool deadband, bool normalizedReference, const std::string& name);
    explicit GovernorTgov1Variant(const std::string& name);
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

  protected:
    bool useDeadband;
    bool normalized;
    double dbL = 0.0;
    double dbU = 0.0;
    double Dt = 0.0;
    double paux = 0.0;
    double referenceOffset = 0.0;
    double speedSignal(const IOdata& inputs) const;
    double speedSlope(const IOdata& inputs) const;
    double valveCommand(const IOdata& inputs) const;
    double valveRate(const IOdata& inputs, const double* state) const;
    double output(const IOdata& inputs, const double* state) const;
};

class GovernorTgov1DB final: public GovernorTgov1Variant {
  public:
    explicit GovernorTgov1DB(const std::string& name = "govTgov1DB_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class GovernorTgov1N final: public GovernorTgov1Variant {
  public:
    explicit GovernorTgov1N(const std::string& name = "govTgov1N_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class GovernorTgov1NDB final: public GovernorTgov1Variant {
  public:
    explicit GovernorTgov1NDB(const std::string& name = "govTgov1NDB_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
}  // namespace griddyn::governors
