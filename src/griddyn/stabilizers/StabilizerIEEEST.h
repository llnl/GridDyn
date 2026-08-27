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
 * @brief PSS/E IEEEST single-input power-system stabilizer.
 *
 * Implements frozen ANDES v2.0.0 `andes/models/pss/ieeest.py`, using the
 * PSS/E DYR schema in `andes/io/psse-dyr.yaml`. The selected input @f$u@f$
 * passes through the following transfer-function cascade:
 *
 * @f[
 * F_1(s)=\frac{1}{1+A_1s+A_2s^2},\quad
 * F_2(s)=\frac{1+A_5s+A_6s^2}{1+A_3s+A_4s^2},
 * @f]
 * @f[
 * L_1(s)=\frac{1+T_1s}{1+T_2s},\quad
 * L_2(s)=\frac{1+T_3s}{1+T_4s},\quad
 * W(s)=\frac{K_ST_5s}{1+T_6s}.
 * @f]
 *
 * The final output is @f$V_{SS}=\operatorname{limit}_{[LSMIN,LSMAX]}(W)@f$
 * when @f$V_{t0}+VCL\leq V_t\leq V_{t0}+VCU@f$, and zero otherwise. The
 * limiter is purely an output hard limit: it does not apply anti-windup to
 * the filter states.
 *
 * For positive second-order denominators, the realization follows ANDES
 * exactly. For example, @f$F_1@f$ uses
 * @f[
 * A_2\dot{x}_{F1}=u-y_{F1}-A_1x_{F1},\qquad \dot{y}_{F1}=x_{F1},
 * @f]
 * and @f$F_2@f$ uses
 * @f[
 * A_4\dot{x}_{F2}=u-y_{F2}-A_3x_{F2},\quad \dot{y}_{F2}=x_{F2},
 * @f]
 * @f[
 * F_2=y_{F2}+A_5x_{F2}+\frac{A_6}{A_4}
 *       (u-y_{F2}-A_3x_{F2}).
 * @f]
 * Each active first-order lead-lag is realized as
 * @f$T_b\dot{x}=u-x@f$ and
 * @f$y=x+(T_a/T_b)(u-x)@f$. The washout state satisfies
 * @f$T_6\dot{x}_W=K_SL_2-x_W@f$ with output
 * @f$W=T_5(K_SL_2-x_W)/T_6@f$; if @f$T_5=0@f$, frozen ANDES selects the
 * low-pass output @f$W=x_W@f$.
 *
 * Frozen ANDES `zero_out` semantics are retained: @f$A_4\leq0@f$ bypasses
 * @f$F_2@f$, @f$T_2\leq0@f$ bypasses @f$L_1@f$, and @f$T_4\leq0@f$
 * bypasses @f$L_2@f$. @f$A_2=0@f$ reduces @f$F_1@f$ to its first-order form
 * when @f$A_1>0@f$, or bypasses it when @f$A_1=0@f$. These reductions avoid
 * allocating singular dummy states while preserving the same transfer
 * functions and initialized outputs.
 *
 * @par References
 * - ANDES v2.0.0 `andes/models/pss/ieeest.py` and `andes/core/block.py`
 *   (`Lag2ndOrd`, `LeadLag2ndOrd`, `LeadLag`, and `WashoutOrLag`).
 * - ANDES v2.0.0 `andes/io/psse-dyr.yaml`, IEEEST schema.
 * - Siemens PTI, <em>PSS/E Model Library</em>, IEEEST model.
 *
 * GridDyn currently supports local MODE values 0, 1, 3, 4, and 5: disabled,
 * rotor-speed deviation, electrical power, accelerating power, and terminal
 * voltage. MODE 2 (bus frequency), MODE 6 (voltage derivative), and nonzero
 * BUSR require measurement routing not yet available in GridDyn and are
 * rejected rather than approximated.
 */
class StabilizerIEEEST: public Stabilizer {
  public:
    enum IEEESTFlags {
        OUTPUT_LIMITED = OBJECT_FLAG5,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG6,
        VOLTAGE_GATED = OBJECT_FLAG7,
    };

  protected:
    int mode = 1;
    int remoteBus = 0;
    double A1 = 1.0;
    double A2 = 1.0;
    double A3 = 1.0;
    double A4 = 1.0;
    double A5 = 1.0;
    double A6 = 1.0;
    double T1 = 1.0;
    double T2 = 1.0;
    double T3 = 1.0;
    double T4 = 1.0;
    double T5 = 1.0;
    double T6 = 1.0;
    double Ks = 1.0;
    double Lsmax = 0.3;
    double Lsmin = -0.3;
    double Vcu = 999.0;
    double Vcl = -999.0;
    double initialVoltage = 1.0;
    double initialPmech = 0.0;

    index_t f1xState = kNullLocation;
    index_t f1yState = kNullLocation;
    index_t f2xState = kNullLocation;
    index_t f2yState = kNullLocation;
    index_t ll1State = kNullLocation;
    index_t ll2State = kNullLocation;
    index_t washoutState = kNullLocation;

  public:
    explicit StabilizerIEEEST(const std::string& objName = "pssIEEEST_#");
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
        std::array<double, 7> stateGain{};
        double inputGain = 0.0;
    };

    [[nodiscard]] double selectedInput(const IOdata& inputs) const;
    [[nodiscard]] LinearValue f1Output(const double state[], double input) const;
    [[nodiscard]] LinearValue f2Output(const double state[], const LinearValue& input) const;
    [[nodiscard]] static LinearValue leadLagOutput(const double state[],
                                                   const LinearValue& input,
                                                   index_t stateIndex,
                                                   double leadTime,
                                                   double lagTime);
    [[nodiscard]] LinearValue cascadeOutput(const double state[], const IOdata& inputs) const;
    [[nodiscard]] bool voltageEnabled(const IOdata& inputs) const;
    [[nodiscard]] int outputLimitStatus(const double state[], const IOdata& inputs) const;
    [[nodiscard]] double output(const double state[], const IOdata& inputs) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);
    static bool supportedMode(int modeValue);
};
}  // namespace griddyn::stabilizers
