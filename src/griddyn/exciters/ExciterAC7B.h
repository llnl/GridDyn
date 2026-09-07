/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include "utilities/Saturation.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
/** IEEE/PSS/E AC7B alternator-rectifier excitation system.
 *
 * AC7B uses the AC8B-style PID voltage regulator and rotating exciter, with an
 * additional inner PI loop and excitation-system stabilizer feedback.  The
 * implemented equations are
 * \f[
 * V_i=V_{ref}+V_{bias}+V_{set}-1+V_{SS}-V_m-
 *      K_{F3}(V_{FE}-x_{F3})/T_{F3},
 * \f]
 * \f[
 * T_R\dot V_m=V_T-V_m,\quad T_{DR}\dot x_D=V_i-x_D,\\
 * V_P^*=K_{PR}V_i+x_I+K_{DR}(V_i-x_D)/T_{DR},\\
 * V_P=\operatorname{lim}(V_P^*,V_{RMIN},V_{RMAX}),\quad \dot x_I=K_{IR}V_i,\\
 * e_A=V_P-K_{F1}E_{fd}-K_{F2}V_{FE},\\
 * V_A^*=K_{PA}e_A+x_A,\quad
 * V_A=\operatorname{lim}(V_A^*,V_{AMIN},V_{AMAX}),\quad \dot x_A=K_{IA}e_A,
 * \f]
 * \f[
 * I_C=\max(K_PV_TV_A,-K_LV_{FE}),\quad
 * T_E\dot V_E=I_C-\{(K_E+S_E(V_E))V_E+K_DI_f\},\quad
 * E_{fd}=V_EF_{EX}(K_CI_f/V_E).
 * \f]
 * where \f$T_{F3}\dot x_{F3}=V_{FE}-x_{F3}\f$ and
 * \f$S_E(V_E)=B(V_E-A)^2/V_E\f$ above its fitted cutoff (zero below).
 * The PID and PI integrators are held only when their complete pre-limit
 * outputs are saturated outward. The \f$V_E\f$ state is held at
 * \f$V_{EMIN}\f$ or the implicit \f$V_{FE}=V_{FEMAX}\f$ boundary; the
 * control input \f$I_C\f$ is not clipped to those state bounds.
 *
 * @par Equation source
 * OpenIPSL `Electrical.Controls.PSSE.ES.AC7B` and its shared rotating
 * exciter, rectifier, and no-windup PID/PI blocks, commit 8155c73f.
 */
class ExciterAC7B final: public Exciter {
  public:
    enum AC7BFlags {
        PID_LIMITED = OBJECT_FLAG5,
        PID_LIMIT_HIGH = OBJECT_FLAG6,
        PI_LIMITED = OBJECT_FLAG7,
        PI_LIMIT_HIGH = OBJECT_FLAG8,
        EXCITER_LIMITED = OBJECT_FLAG9,
        EXCITER_LIMIT_HIGH = OBJECT_FLAG10,
    };

    explicit ExciterAC7B(const std::string& objName = "exciterAC7B_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
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
    stringVec localStateNames() const override;
    index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  private:
    static constexpr index_t stateCount = 6;
    struct Evaluation {
        double fieldOutput = 0.0;
        double algebraicDerivative = 0.0;
        std::array<double, stateCount> fieldStateDerivatives{};
        std::array<double, exciterInputCount> fieldInputDerivatives{};
        std::array<double, stateCount> rates{};
        std::array<double, stateCount> rateAlgebraicDerivatives{};
        std::array<std::array<double, stateCount>, stateCount> rateStateDerivatives{};
        std::array<std::array<double, exciterInputCount>, stateCount> rateInputDerivatives{};
        double pidDrive = 0.0;
        double pidIntegratorDrive = 0.0;
        double piDrive = 0.0;
        double piIntegratorDrive = 0.0;
        double fieldFeedback = 0.0;
        double exciterDrive = 0.0;
    };
    Evaluation evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const;
    double solveExciterVoltage(double fieldVoltage, double fieldCurrent) const;
    double solveFieldFeedbackLimit(double fieldCurrent, double initialVoltage) const;
    bool updateLimitFlags(const IOdata& inputs, double state[], bool projectStates);

    model_parameter Tr = 0.0;
    model_parameter Kpr = 4.24;
    model_parameter Kir = 4.24;
    model_parameter Kdr = 0.0;
    model_parameter Tdr = 0.0;
    model_parameter Kpa = 65.36;
    model_parameter Kia = 59.69;
    model_parameter Vamax = 1.0;
    model_parameter Vamin = -0.95;
    model_parameter Kp = 4.96;
    model_parameter Kl = 10.0;
    model_parameter Te = 1.1;
    model_parameter Kc = 0.18;
    model_parameter Kd = 0.02;
    model_parameter Ke = 1.0;
    model_parameter Kf1 = 0.212;
    model_parameter Kf2 = 0.0;
    model_parameter Kf3 = 0.0;
    model_parameter Tf3 = 0.0;
    model_parameter Vemax = 6.9;
    model_parameter Vemin = -99.0;
    model_parameter E1 = 6.3;
    model_parameter Se1 = 0.44;
    model_parameter E2 = 4.725;
    model_parameter Se2 = 0.075;
    utilities::Saturation saturation{
        utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC};
};
}  // namespace griddyn::exciters
