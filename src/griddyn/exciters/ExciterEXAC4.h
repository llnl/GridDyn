/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
#include <string>
#include <vector>

namespace griddyn::exciters {
/** PSS/E EXAC4 fast alternating-current excitation system.
 *
 * The EXAC4 record uses a sensed terminal-voltage lag, a limited input and a
 * lead-lag compensator followed by a regulator:
 * @f[
 * T_R\dot v_m=V_t-v_m,\quad v_i=V_{ref0}-v_m,\quad
 * u=\operatorname{limit}_{[V_{IMIN},V_{IMAX}]}(v_i),
 * @f]
 * @f[
 * T_B\dot x_L=u-x_L,\quad y_L=x_L+\frac{T_C}{T_B}(u-x_L),\quad
 * T_A\dot v_R=K_Ay_L-v_R,
 * @f]
 * @f[
 * E_{fd}=\operatorname{limit}_{[V_{RMIN}-K_CX_{ad}I_{fd},\
 * V_{RMAX}-K_CX_{ad}I_{fd}]}(v_R).
 * @f]
 * @f$V_{ref0}=V_{t0}+E_{fd0}/K_A@f$ is retained after initialization, as in
 * frozen ANDES 2.0.0.  References: IEEE Std 421.5-2016 and the PSS/E Model
 * Library EXAC4 description.
 */
class ExciterEXAC4 final: public Exciter {
  public:
    enum EXAC4Flags {
        INPUT_LIMITED = OBJECT_FLAG5,
        INPUT_LIMIT_HIGH = OBJECT_FLAG6,
        OUTPUT_LIMITED = OBJECT_FLAG7,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG8
    };

  private:
    model_parameter Tr = 0.01;
    model_parameter Vimax = 5.0;
    model_parameter Vimin = -0.1;
    model_parameter Tc = 1.0;
    model_parameter Tb = 1.0;
    model_parameter Kc = 0.0;
    double vref0 = 1.0;

  public:
    explicit ExciterEXAC4(const std::string& objName = "exciterEXAC4_#");
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
    double unlimitedInput(const double state[]) const;
    double limitedInput(const double state[]) const;
    int inputLimitStatus(const double state[]) const;
    int outputLimitStatus(const IOdata& inputs, const double state[]) const;
    double fieldVoltage(const IOdata& inputs, const double state[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);
};
}  // namespace griddyn::exciters
