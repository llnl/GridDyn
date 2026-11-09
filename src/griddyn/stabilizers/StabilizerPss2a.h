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
 * The local MODE subset is 0: zero, 1/2: machine speed deviation,
 * 3: electrical torque, 4: mechanical-power deviation from initialization,
 * and 5: terminal voltage. MODE 2 uses speed as a local frequency proxy;
 * MODE 4 does not subtract electrical power. Nonzero
 * BUSR values and mode 6 require cross-bus signal routing and are rejected.
 */
class StabilizerPss2a: public Stabilizer {
  public:
    enum Pss2aFlags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
    };

  protected:
    // Conservative storage for M,N<=8 (M*N<=8) plus six branch states and
    // three lead-lags in the PSS2B extension; actual active layouts are smaller.
    static constexpr index_t maximumRampStates = 16;
    static constexpr index_t maxDifferentialStates = 25;
    int rampM = 5;
    int rampN = 1;
    double T10 = 0.0;
    double T11 = 0.0;

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
    std::array<index_t, maximumRampStates> rampFilterState{};
    index_t leadLag1State = kNullLocation;
    index_t leadLag2State = kNullLocation;
    index_t leadLag3State = kNullLocation;

  public:
    explicit StabilizerPss2a(const std::string& objName = "pssPSS2A_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    const ModelDescription& getModelDescription() const override;

    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    using Stabilizer::set;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;

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

  protected:
    struct LinearValue {
        double value = 0.0;
        std::array<double, maxDifferentialStates> stateGain{};
        double input1Gain = 0.0;
        double input2Gain = 0.0;
    };

    [[nodiscard]] virtual LinearValue
        selectedInput(const IOdata& inputs, int mode, bool first) const;
    [[nodiscard]] index_t rampStateCount() const;
    [[nodiscard]] static LinearValue derivativeBranch(const LinearValue& input,
                                                      const double state[],
                                                      index_t firstState,
                                                      index_t secondState,
                                                      index_t lagState,
                                                      double firstTime,
                                                      double secondTime,
                                                      double lagGain);
    [[nodiscard]] LinearValue rampFilter(const LinearValue& input, const double state[]) const;
    [[nodiscard]] static LinearValue leadLagOutput(const LinearValue& input,
                                                   const double state[],
                                                   index_t stateIndex,
                                                   double leadTime,
                                                   double lagTime);
    [[nodiscard]] LinearValue outputExpression(const IOdata& inputs, const double state[]) const;
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
