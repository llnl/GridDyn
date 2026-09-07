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
/** IEEE/PSS/E AC8B alternator-rectifier excitation system.
 *
 * AC8B is implemented as a terminal-voltage transducer, non-windup PID voltage
 * regulator, non-windup regulator lag, and rotating AC exciter with
 * demagnetizing feedback and rectifier commutation-voltage drop.  With
 * measured voltage \f$V_m\f$, PID integral \f$x_I\f$, derivative lag
 * \f$x_D\f$, regulator output \f$V_R\f$, exciter internal voltage \f$V_E\f$,
 * and \f$I_f=X_{ad}I_{fd}\f$,
 * \f[
 * \begin{aligned}
 * T_R\dot V_m &= V_T-V_m,\\
 * V_i &= V_{ref}+V_{bias}+V_{set}-1+V_{SS}-V_m,\\
 * T_D\dot x_D &= V_i-x_D,\\
 * V_P^* &= K_{PR}V_i+x_I+K_{DR}(V_i-x_D)/T_D,\\
 * V_P &= \operatorname{lim}(V_P^*,V_{PMIN},V_{PMAX}),\\
 * \dot x_I &= K_{IR}V_i\quad\hbox{unless }V_P\hbox{ is saturated outward},\\
 * T_A\dot V_R &= K_AV_P-V_R,\\
 * I_N &= K_CI_f/V_E,\qquad E_{fd}=V_EF_{EX}(I_N),\\
 * S_E(V_E)&=\begin{cases}0,&V_E<A,\\B(V_E-A)^2/V_E,&V_E\ge A,\end{cases}\\
 * V_{FE} &= (K_E+S_E(V_E))V_E+K_DI_f,\\
 * T_E\dot V_E &= V_R-V_{FE}.
 * \end{aligned}
 * \f]
 * \f$V_R\f$ is held at \f$[V_{RMIN},V_{RMAX}]\f$. The rotating-exciter
 * state is held at \f$V_E=V_{EMIN}\f$ or at the implicit upper boundary
 * \f$V_{FE}=V_{FEMAX}\f$. The input to its differential equation is not
 * clipped to those state bounds.
 *
 * @par Equation sources
 * - OpenIPSL `Electrical.Controls.PSSE.ES.AC8B` and its shared rotating
 *   exciter, rectifier, and no-windup PID blocks, commit 8155c73f.
 * - ANDES `AC8B` and PSS/E DYR schema, commit
 *   eda5163c9ee8d19945a1dd5d1771fec5da608c27.
 */
class ExciterAC8B final: public Exciter {
  public:
    enum AC8BFlags {
        PID_LIMITED = OBJECT_FLAG5,
        PID_LIMIT_HIGH = OBJECT_FLAG6,
        REGULATOR_LIMITED = OBJECT_FLAG7,
        REGULATOR_LIMIT_HIGH = OBJECT_FLAG8,
        EXCITER_LIMITED = OBJECT_FLAG9,
        EXCITER_LIMIT_HIGH = OBJECT_FLAG10,
    };

    explicit ExciterAC8B(const std::string& objName = "exciterAC8B_#");
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
    static constexpr index_t stateCount = 5;
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
        double regulatorDrive = 0.0;
        double fieldFeedback = 0.0;
        double exciterDrive = 0.0;
    };
    Evaluation evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const;
    double solveExciterVoltage(double fieldVoltage, double fieldCurrent) const;
    double solveFieldFeedbackLimit(double fieldCurrent, double initialVoltage) const;
    bool updateLimitFlags(const IOdata& inputs, double state[], bool projectStates);

    model_parameter Tr = 0.01;
    model_parameter Kpr = 10.0;
    model_parameter Kir = 10.0;
    model_parameter Kdr = 10.0;
    model_parameter Tdr = 0.2;
    model_parameter Vpmax = 999.0;
    model_parameter Vpmin = -999.0;
    model_parameter Vemax = 999.0;
    model_parameter Vemin = -999.0;
    model_parameter Te = 0.8;
    model_parameter Kc = 0.1;
    model_parameter Kd = 0.0;
    model_parameter Ke = 1.0;
    model_parameter E1 = 0.0;
    model_parameter Se1 = 0.0;
    model_parameter E2 = 1.0;
    model_parameter Se2 = 0.0;
    utilities::Saturation saturation{
        utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC};
};
}  // namespace griddyn::exciters
