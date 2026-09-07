/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
/** IEEE/PSS/E ESST2A static excitation system.
 *
 * ESST2A is a compound-source controlled-rectifier exciter.  The source term is
 * \f$V_B=V_EF_{EX}(K_CX_{ad}I_{fd}/V_E)\f$, where
 * \f$V_E=|K_PV_T+jK_II_T|\f$; if both \f$K_P\f$ and \f$K_I\f$ are nonpositive,
 * the OpenIPSL/PSS/E bypass uses \f$V_B=1\f$.  With measured voltage
 * \f$V_m\f$, regulator output \f$V_A\f$, feedback state \f$x_F\f$, and field
 * state \f$E_{fd}\f$,
 * \f[
 * T_R\dot V_m=V_T-V_m,\quad V_F=K_F(E_{fd}-x_F)/T_F,\quad
 * V_i=V_{ref}+V_{bias}+V_{set}-1+V_{SS}-V_m-V_F,
 * \f]
 * \f[
 * T_A\dot V_A=K_AV_i-V_A,\quad
 * T_E\dot E_{fd}=V_BV_A-K_EE_{fd},\quad
 * T_F\dot x_F=E_{fd}-x_F.
 * \f]
 * \f$V_A\f$ is limited by \f$[V_{RMIN},V_{RMAX}]\f$ and \f$E_{fd}\f$ by
 * \f$[0,E_{FDMAX}]\f$ with event-aware non-windup behavior.
 *
 * This is the 13-parameter OpenIPSL core model. GridDyn's standard
 * stabilizer input supplies the normal summed supplementary path. Separate
 * VUEL/VOEL inputs and the high-value gate are not available in the current
 * exciter interface and are therefore outside this implementation.
 *
 * @par Equation source
 * OpenIPSL `Electrical.Controls.PSSE.ES.ESST2A` and its shared rectifier,
 * derivative-feedback, limited-lag, and limited-integrator blocks, commit
 * 8155c73f.
 */
class ExciterESST2A final: public Exciter {
  public:
    enum ESST2AFlags {
        REGULATOR_LIMITED = OBJECT_FLAG5,
        REGULATOR_LIMIT_HIGH = OBJECT_FLAG6,
        FIELD_LIMITED = OBJECT_FLAG7,
        FIELD_LIMIT_HIGH = OBJECT_FLAG8,
    };

    explicit ExciterESST2A(const std::string& objName = "exciterESST2A_#");
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
    static constexpr index_t stateCount = 4;
    struct Evaluation {
        double fieldOutput = 0.0;
        double algebraicDerivative = 0.0;
        std::array<double, stateCount> fieldStateDerivatives{};
        std::array<double, exciterInputCount> fieldInputDerivatives{};
        std::array<double, stateCount> rates{};
        std::array<double, stateCount> rateAlgebraicDerivatives{};
        std::array<std::array<double, stateCount>, stateCount> rateStateDerivatives{};
        std::array<std::array<double, exciterInputCount>, stateCount> rateInputDerivatives{};
        double regulatorDrive = 0.0;
        double fieldDrive = 0.0;
    };
    Evaluation evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const;
    double rectifierVoltage(const IOdata& inputs) const;
    bool updateLimitFlags(const IOdata& inputs, double state[], bool projectStates);

    model_parameter Tr = 0.01;
    model_parameter Kp = 0.7;
    model_parameter Ki = 1.0;
    model_parameter Kc = 0.03;
    model_parameter Kf = 0.05;
    model_parameter Tf = 0.7;
    model_parameter Ke = 1.0;
    model_parameter Te = 0.5;
    model_parameter Efdmax = 5.0;
};
}  // namespace griddyn::exciters
