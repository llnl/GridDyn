/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Stabilizer.h"
#include <array>
#include <string>
#include <vector>

namespace griddyn::stabilizers {
/**
 * @brief PSS/E STAB3 power-sensitive stabilizer.
 *
 * STAB3 uses electrical power only.  The input is passed through an optional
 * transducer lag, an optional low-pass lag after subtraction of the
 * initialized electrical power, and a derivative lag.  The output is limited
 * symmetrically to +/- VLIM.
 */
class StabilizerStab3: public Stabilizer {
  public:
    enum Stab3Flags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
    };

  protected:
    double Tt = 1.0;
    double Tx1 = 1.0;
    double Tx2 = 1.0;
    double Kx = 1.0;
    double Vlim = 5.0;
    double initialElectricalPower = 0.0;

    index_t transducerState = kNullLocation;
    index_t lowPassState = kNullLocation;
    index_t derivativeState = kNullLocation;

  public:
    explicit StabilizerStab3(const std::string& objName = "pssSTAB3_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
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
    void rootTest(const IOdata& inputs,
                  const StateData& stateData,
                  double roots[],
                  const SolverMode& sMode) override;
    void rootTrigger(CoreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const SolverMode& sMode) override;
    ChangeCode rootCheck(const IOdata& inputs,
                         const StateData& stateData,
                         const SolverMode& sMode,
                         CheckLevel level) override;

  private:
    struct LinearValue {
        double value = 0.0;
        std::array<double, 3> stateGain{};
        double inputGain = 0.0;
    };

    [[nodiscard]] LinearValue electricalPowerInput(const IOdata& inputs) const;
    [[nodiscard]] LinearValue unlimitedOutput(const IOdata& inputs,
                                               const double state[]) const;
    [[nodiscard]] double output(const IOdata& inputs, const double state[]) const;
    [[nodiscard]] int outputLimitStatus(const IOdata& inputs, const double state[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);
};
}  // namespace griddyn::stabilizers
