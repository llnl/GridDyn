/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Stabilizer.h"
#include <string>
#include <vector>

namespace griddyn::stabilizers {
/**
 * @brief PSS/E ST2CUT dual-input power-system stabilizer.
 *
 * This is the frozen ANDES v2.0.0 realization in
 * `andes/models/pss/st2cut.py`, using the PSS/E DYR field order declared in
 * `andes/io/psse-dyr.yaml`. It implements two transducer lags, their sum, a
 * washout (or lag when @f$T_3=0@f$), three lead-lag stages, the
 * @f$[LSMIN,LSMAX]@f$ output limiter, and the initialized-voltage-relative
 * @f$[VCL,VCU]@f$ output gate.
 *
 * Let @f$u_1,u_2@f$ be the selected measurement signals; @f$x_1,x_2@f$ the
 * transducer states; @f$x_w@f$ the washout/lag state; and
 * @f$x_5,x_7,x_9@f$ the three lead-lag lag states. The local differential
 * equations are
 *
 * @f[
 * T_1\dot{x}_1=K_1u_1-x_1,\qquad T_2\dot{x}_2=K_2u_2-x_2,\qquad
 * T_4\dot{x}_w=x_1+x_2-x_w.
 * @f]
 *
 * The washout-or-lag output is
 *
 * @f[
 * w=\begin{cases}
 * \dfrac{T_3}{T_4}(x_1+x_2-x_w), & T_3>0,\\
 * x_w, & T_3=0,
 * \end{cases}
 * @f]
 *
 * and each lead-lag block @f$(T_a,T_b,x,y)@f$ obeys
 *
 * @f[
 * T_b\dot{x}=u-x,\qquad y=x+\dfrac{T_a}{T_b}(u-x).
 * @f]
 *
 * The three blocks are cascaded as
 * @f$(w,T_5,T_6,x_5,y_5)@f$, @f$(y_5,T_7,T_8,x_7,y_7)@f$, and
 * @f$(y_7,T_9,T_{10},x_9,y_9)@f$. Finally, with initialized terminal-voltage
 * reference @f$V_{t0}@f$,
 *
 * @f[
 * V_{SS}=\begin{cases}
 * \operatorname{limit}_{[LSMIN,LSMAX]}(y_9), &
 * V_{t0}+VCL\leq V_t\leq V_{t0}+VCU,\\
 * 0, & \text{otherwise}.
 * \end{cases}
 * @f]
 *
 * Initialization sets @f$x_1=K_1u_1@f$, @f$x_2=K_2u_2@f$,
 * @f$x_w=x_1+x_2@f$, and the three lead-lag states and @f$V_{SS}@f$ to zero,
 * so the washout path contributes no steady-state stabilizing signal.
 *
 * @par References
 * - ANDES v2.0.0, `andes/models/pss/st2cut.py` (equations and initialization).
 * - ANDES v2.0.0, `andes/io/psse-dyr.yaml` (DYR schema and field order).
 * - Siemens PTI, <em>PSS/E Model Library</em>, ST2CUT model (published PSS/E
 *   block-model source).
 *
 * The DYR adapter maps the exact fields
 * `BUS, ID, MODE, BUSR, MODE2, BUSR2, K1, K2, T1, T2, T3, T4, T5, T6, T7,
 * T8, T9, T10, LSMAX, LSMIN, VCU, VCL`. Following ANDES, a zero `VCU` or
 * `VCL` is expanded to `+999` or `-999`, respectively, to disable that side
 * of the voltage gate.
 *
 * The GridDyn PSS signal contract exposes rotor speed, terminal voltage,
 * mechanical power, and electrical power.  It therefore supports local
 * MODE/MODE2 values 0 (disabled), 1 (rotor speed), 3 (electrical power),
 * 4 (accelerating power), and 5 (terminal voltage).  ANDES modes 2
 * (BusFreq) and 6 (voltage derivative), and nonzero remote BUSR/BUSR2,
 * require cross-bus measurement routing that GridDyn does not yet provide;
 * they are rejected at initialization rather than silently approximated.
 * In particular, MODE 1 is @f$\omega-1@f$, MODE 3 is electrical power,
 * MODE 4 is @f$P_m-P_{m0}@f$, and MODE 5 is terminal voltage. MODE 0 supplies
 * zero to its transducer.
 *
 * The implementation requires positive @f$T_1,T_2,T_4,T_6,T_8,T_{10}@f$.
 * Nonnegative @f$T_3,T_5,T_7,T_9@f$ are accepted; @f$T_3=0@f$ selects the
 * documented lag form above. Other zero-denominator block bypasses are not
 * implemented and are rejected during parameter validation.
 */
class StabilizerST2CUT: public Stabilizer {
  public:
    enum ST2CUTFlags {
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
    double T1 = 1.0;
    double T2 = 1.0;
    double T3 = 1.0;
    double T4 = 0.2;
    double T5 = 1.0;
    double T6 = 0.5;
    double T7 = 1.0;
    double T8 = 1.0;
    double T9 = 1.0;
    double T10 = 0.2;
    double Lsmax = 0.3;
    double Lsmin = -0.3;
    double Vcu = 999.0;
    double Vcl = -999.0;
    double initialVoltage = 1.0;
    double initialPmech = 0.0;

  public:
    explicit StabilizerST2CUT(const std::string& objName = "pssST2CUT_#");
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
    double selectedInput(const IOdata& inputs, int mode) const;
    double washoutOutput(const double state[]) const;
    static double leadLagOutput(double input, double state, double leadTime, double lagTime);
    double unlimitedOutput(const double state[]) const;
    bool voltageEnabled(const IOdata& inputs) const;
    double output(const IOdata& inputs, const double state[]) const;
    int outputLimitStatus(const double state[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);
    static bool supportedMode(int mode);
};
}  // namespace griddyn::stabilizers
