/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
/** IEEE/PSS/E ESST1A static excitation system.
 *
 * The differential states are measured voltage \f$V_m\f$, first and second
 * lead-lag lag states \f$x_1,x_2\f$, amplifier output \f$V_A\f$, and rate
 * feedback state \f$R_F\f$.  With \f$V_F=K_FV_U/T_F-R_F\f$ and the selected
 * stabilizer signal \f$V_S\f$, the implemented normal-path equations are
 * \f[
 * \begin{aligned}
 * T_R\dot V_m &= V_T-V_m,\\
 * v_i &= \operatorname{lim}(V_{ref}+V_{bias}+V_{set}-1+V_S-V_m-V_F,
 *                            V_{IMIN},V_{IMAX}),\\
 * T_B\dot x_1&=v_i-x_1,&v_1&=(T_C/T_B)v_i+(1-T_C/T_B)x_1,\\
 * T_{B1}\dot x_2&=v_1-x_2,&v_2&=(T_{C1}/T_{B1})v_1+(1-T_{C1}/T_{B1})x_2,\\
 * T_A\dot V_A&=K_Av_2-V_A,\\
 * T_F\dot R_F&=K_FV_U/T_F-R_F,\\
 * V_U&=V_A-\max(0,K_{LR}(I_{fd}-I_{LR}))+V_{S2},\\
 * E_{fd}&=\operatorname{lim}(V_U,V_{RMIN}V_T,
 *                              V_{RMAX}V_T-K_CI_{fd}).
 * \end{aligned}
 * \f]
 * The amplifier uses non-windup bounds \f$[V_{AMIN},V_{AMAX}]\f$.  Zero
 * \f$T_R\f$, \f$T_B\f$, or \f$T_{B1}\f$ bypasses the associated block while
 * retaining its state slot for a fixed solver layout.
 *
 * GridDyn routes the standard stabilizer input as VOS=1 (before the input
 * limiter) or VOS=2 (after the current limiter).  It supplies the normal
 * inactive UEL/OEL values; selector UEL=2 or 3 requires an auxiliary routing
 * interface that GridDyn does not yet expose and is rejected on initialization.
 *
 * @par Equation sources
 * - OpenIPSL `Electrical.Controls.PSSE.ES.ESST1A`, commit 8155c73f.
 * - PowerDynamics.jl `PSSE_ESST1A`, commit
 *   908306c67b85fb24249955277b97e2e4f3d9b837.
 * - ANDES ESST1A and its PSS/E DYR schema, commit
 *   eda5163c9ee8d19945a1dd5d1771fec5da608c27.
 *
 * @note The cited PowerDynamics.jl port applies \f$-K_CI_{fd}\f$ to both
 * rectifier bounds. OpenIPSL and ANDES apply it only to the upper bound;
 * GridDyn follows those two independent implementations and the PSS/E block
 * diagram.
 */
class ExciterESST1A final: public Exciter {
  public:
    enum ESST1AFlags {
        AMPLIFIER_LIMITED = OBJECT_FLAG5,
        AMPLIFIER_LIMIT_HIGH = OBJECT_FLAG6,
    };

  protected:
    model_parameter Tr = 0.0;
    model_parameter Vimax = 99.0;
    model_parameter Vimin = -99.0;
    model_parameter Tb = 0.0;
    model_parameter Tc = 0.0;
    model_parameter Tb1 = 0.0;
    model_parameter Tc1 = 0.0;
    model_parameter Vamax = 9.0;
    model_parameter Vamin = -5.43;
    model_parameter Ilr = 4.4;
    model_parameter Klr = 4.54;
    model_parameter Kc = 0.2;
    model_parameter Kf = 0.03;
    model_parameter Tf = 1.0;
    int uelSelector = 1;
    int vosSelector = 1;

  public:
    explicit ExciterESST1A(const std::string& objName = "exciterESST1A_#");
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
    struct Signals {
        double measuredVoltage;
        double fieldDrive;
        double feedback;
        double input;
        double limitedInput;
        double leadLagOne;
        double leadLagTwo;
        double amplifierDrive;
        double currentLimit;
        double output;
        bool inputLimited;
        bool outputLimited;
        bool outputUpperLimited;
        bool currentLimited;
    };
    Signals evaluate(const IOdata& inputs, const double state[]) const;
    double leadLagOneOutput(double input, double state) const;
    double leadLagTwoOutput(double input, double state) const;
};

}  // namespace griddyn::exciters
