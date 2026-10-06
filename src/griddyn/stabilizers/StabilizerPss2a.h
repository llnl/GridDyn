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
 * @brief PSS/E PSS2A dual-input power-system stabilizer.
 *
 * This follows the OpenIPSL/PSS/E v33 realization, including its fixed M=5,
 * N=1 ramp-tracking filter: two derivative-lag branches are combined, passed
 * through the ramp filter, differenced with the second branch, amplified,
 * lead-lag compensated, and limited at the output.
 *
 * Local MODE values 0, 1, 2, 3, 4, and 5 are supported. Modes 1 and 2 use
 * local speed/frequency deviation; modes 3, 4, and 5 use electrical power,
 * accelerating/mechanical power deviation, and terminal voltage. Nonzero
 * BUSR values and mode 6 require cross-bus signal routing and are rejected.
 */
class StabilizerPss2a: public Stabilizer {
  public:
    enum Pss2aFlags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
    };

  protected:
    static constexpr index_t rampLagCount = 5;
    static constexpr index_t maxDifferentialStates = 14;

    int mode1 = 1;
    int remoteBus1 = 0;
    int mode2 = 0;
    int remoteBus2 = 0;
    double Tw1 = 10.0;
    double Tw2 = 10.0;
    double T6 = 1e-9;
    double Tw3 = 10.0;
    double Tw4 = 1e-9;
    double T7 = 10.0;
    double Ks2 = 0.99;
    double Ks3 = 1.0;
    double T8 = 0.5;
    double T9 = 0.1;
    double Ks1 = 20.0;
    double T1 = 0.15;
    double T2 = 0.025;
    double T3 = 0.15;
    double T4 = 0.025;
    double Vstmax = 0.1;
    double Vstmin = -0.1;
    double initialPmech = 0.0;

    index_t branch1Derivative1State = kNullLocation;
    index_t branch1Derivative2State = kNullLocation;
    index_t branch1LagState = kNullLocation;
    index_t branch2Derivative1State = kNullLocation;
    index_t branch2Derivative2State = kNullLocation;
    index_t branch2LagState = kNullLocation;
    std::array<index_t, rampLagCount + 1> rampFilterState{};
    index_t leadLag1State = kNullLocation;
    index_t leadLag2State = kNullLocation;

  public:
    explicit StabilizerPss2a(const std::string& objName = "pssPSS2A_#");
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
        std::array<double, maxDifferentialStates> stateGain{};
        double input1Gain = 0.0;
        double input2Gain = 0.0;
    };

    [[nodiscard]] LinearValue selectedInput(const IOdata& inputs, int mode, bool first) const;
    [[nodiscard]] LinearValue derivativeBranch(const LinearValue& input,
                                                const double state[],
                                                index_t firstState,
                                                index_t secondState,
                                                index_t lagState,
                                                double firstTime,
                                                double secondTime,
                                                double lagGain) const;
    [[nodiscard]] LinearValue rampFilter(const LinearValue& input,
                                         const double state[]) const;
    [[nodiscard]] LinearValue leadLagOutput(const LinearValue& input,
                                             const double state[],
                                             index_t stateIndex,
                                             double leadTime,
                                             double lagTime) const;
    [[nodiscard]] LinearValue outputExpression(const IOdata& inputs,
                                                const double state[]) const;
    [[nodiscard]] double output(const IOdata& inputs, const double state[]) const;
    [[nodiscard]] int outputLimitStatus(const IOdata& inputs, const double state[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);
    static bool supportedMode(int mode);
    void addLinearInput(MatrixData<double>& matrixData,
                        index_t row,
                        const LinearValue& value,
                        double scale,
                        const IOlocs& inputLocs) const;
};
}  // namespace griddyn::stabilizers
