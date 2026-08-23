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
/** PSS/E EXST1 static excitation system.
 *
 * This implementation follows the frozen ANDES 2.0.0 equations in
 * `andes/models/exciter/exst1.py` except for its output-limiter selector. It
 * follows the EXST1 record described by the PSS/E Model Library and the
 * static excitation-system transfer functions in IEEE Std 421.5-2016.
 *
 * With voltage-transducer state @f$v_m@f$, lead-lag state @f$x_{LL}@f$,
 * regulator state @f$v_R@f$, and washout state @f$x_F@f$,
 *
 * @f[
 * T_R \dot v_m = V_t-v_m, \qquad
 * v_i = V_{ref}-v_m-\frac{K_F}{T_F}(v_R-x_F),
 * @f]
 * @f[
 * T_B \dot x_{LL}=\operatorname{limit}(v_i)-x_{LL}, \qquad
 * y_{LL}=x_{LL}+\frac{T_C}{T_B}
 *       (\operatorname{limit}(v_i)-x_{LL}),
 * @f]
 * @f[
 * T_A \dot v_R=K_A y_{LL}-v_R, \qquad
 * T_F \dot x_F=v_R-x_F.
 * @f]
 *
 * The field-voltage bounds are @f$V_{RMAX}-K_C X_{ad}I_{fd}@f$ and
 * @f$V_{RMIN}-K_C X_{ad}I_{fd}@f$. The output hard limiter is selected from
 * @f$v_R@f$. Frozen ANDES 2.0.0 instead selects its limiter flags from the
 * washout output and applies those flags to @f$v_R@f$; GridDyn intentionally
 * differs because the PSS/E block diagram limits the regulator output.
 */
class ExciterEXST1: public Exciter {
  public:
    enum EXST1Flags {
        INPUT_LIMITED = OBJECT_FLAG5,
        INPUT_LIMIT_HIGH = OBJECT_FLAG6,
        OUTPUT_LIMITED = OBJECT_FLAG7,
        OUTPUT_LIMIT_HIGH = OBJECT_FLAG8,
    };

  protected:
    model_parameter Tr = 0.01;
    model_parameter Vimax = 0.2;
    model_parameter Vimin = 0.0;
    model_parameter Tc = 1.0;
    model_parameter Tb = 1.0;
    model_parameter Kc = 0.2;
    model_parameter Kf = 0.1;
    model_parameter Tf = 1.0;

  public:
    explicit ExciterEXST1(const std::string& objName = "exciterEXST1_#");
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
    double referenceInput(const IOdata& inputs) const;
    double washoutOutput(const double state[]) const;
    double unlimitedInput(const IOdata& inputs, const double state[]) const;
    double limitedInput(const IOdata& inputs, const double state[]) const;
    double leadLagOutput(const IOdata& inputs, const double state[]) const;
    double fieldVoltageTarget(const IOdata& inputs, const double state[]) const;
    int inputLimitStatus(const IOdata& inputs, const double state[]) const;
    int outputLimitStatus(const IOdata& inputs, const double state[]) const;
    bool updateLimitFlags(const IOdata& inputs, const double state[]);
};
}  // namespace griddyn::exciters
