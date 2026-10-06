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
 * @brief PSS/E IEE2ST dual-input power-system stabilizer.
 *
 * The implementation follows the OpenIPSL IEE2ST block diagram: two input
 * lags, a washout, three lead-lag stages, and the output limiter/voltage gate.
 * A zero input-lag time or a zero lead-lag denominator with a zero numerator
 * is represented as an algebraic bypass, which is required by real PSS/E
 * records.
 *
 * GridDyn supports local MODE values 0, 1, 2, 3, 4, and 5. Modes 1 and 2
 * both use the local rotor-speed/frequency deviation; mode 2 is equivalent to
 * the local bus frequency for a generator-attached stabilizer. Modes 3, 4,
 * and 5 use electrical power, accelerating/mechanical power deviation, and
 * terminal voltage. Nonzero BUSR values and mode 6 require cross-bus signal
 * routing and are rejected rather than silently approximated.
 */
class StabilizerIee2st: public Stabilizer {
  public:
    enum Iee2stFlags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
        VOLTAGE_GATED = OBJECT_FLAG7,
    };

  protected:
    int mode1 = 1;
    int remoteBus1 = 0;
    int mode2 = 0;
    int remoteBus2 = 0;
    double K1 = 1.0;
    double K2 = 1.0;
    double T1 = 0.005;
    double T2 = 0.005;
    double T3 = 10.0;
    double T4 = 10.0;
    double T5 = 0.16;
    double T6 = 0.02;
    double T7 = 0.16;
    double T8 = 0.02;
    double T9 = 0.16;
    double T10 = 0.02;
    double Lsmax = 0.1;
    double Lsmin = -0.1;
    double Vcu = 999.0;
    double Vcl = -999.0;
    double initialVoltage = 1.0;
    double initialPmech = 0.0;

    index_t filter1State = kNullLocation;
    index_t filter2State = kNullLocation;
    index_t washoutState = kNullLocation;
    index_t leadLag1State = kNullLocation;
    index_t leadLag2State = kNullLocation;
    index_t leadLag3State = kNullLocation;

  public:
    explicit StabilizerIee2st(const std::string& objName = "pssIEE2ST_#");
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
        std::array<double, 6> stateGain{};
        double input1Gain = 0.0;
        double input2Gain = 0.0;
    };

    [[nodiscard]] LinearValue selectedInput(const IOdata& inputs, int mode, bool first) const;
    [[nodiscard]] LinearValue leadLagOutput(const double state[],
                                             const LinearValue& input,
                                             index_t stateIndex,
                                             double leadTime,
                                             double lagTime) const;
    [[nodiscard]] LinearValue outputExpression(const IOdata& inputs,
                                                const double state[]) const;
    [[nodiscard]] double output(const IOdata& inputs, const double state[]) const;
    [[nodiscard]] bool voltageEnabled(const IOdata& inputs) const;
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
