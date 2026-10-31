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
/** PSS/E STAB2A power-sensitive stabilizing unit (ASEA).
 *
 * Follows OpenIPSL STAB2A at commit 9a595ebf: three washouts with gains
 * -K2, K2, K2 feed parallel K3/(1+s*T3) and K4 paths, followed by two
 * K5/(1+s*T5) lags and a symmetric +/-HLIM output clamp. The input is
 * terminal electrical power on the machine base. Zero T3/T5 bypass their
 * lags; zero T2 makes the washout chain identically zero.
 */
class StabilizerStab2a: public Stabilizer {
  public:
    enum Stab2aFlags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
    };

    explicit StabilizerStab2a(const std::string& objName = "pssSTAB2A_#");
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
    MachineControllerSignal electricalPowerSignal() const override
    {
        return MachineControllerSignal::ELECTRICAL_POWER;
    }
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
    double K2 = 1.0;
    double T2 = 1.0;
    double K3 = 1.0;
    double T3 = 1.0;
    double K4 = 1.0;
    double K5 = 1.0;
    double T5 = 1.0;
    double Hlim = 5.0;
    std::array<index_t, 6> filterStates{kNullLocation,
                                        kNullLocation,
                                        kNullLocation,
                                        kNullLocation,
                                        kNullLocation,
                                        kNullLocation};

    struct LinearValue {
        double value = 0.0;
        std::array<double, 6> stateGain{};
        double inputGain = 0.0;
    };
    struct Signals {
        std::array<LinearValue, 6> targets;
        LinearValue output;
    };
    std::array<double, 6> timeConstants() const;
    Signals signals(const IOdata& inputs, const double state[]) const;
    int outputLimitStatus(double value) const;
    bool updateLimitFlags(double value);
};
}  // namespace griddyn::stabilizers
