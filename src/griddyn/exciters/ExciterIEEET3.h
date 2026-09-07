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
/** IEEE/PSS/E Type 3 excitation system.
 *
 * IEEET3 is a compound-source exciter.  GridDyn uses the existing synchronous
 * machine dq signal interface to form
 * \f$V_E=|K_P(V_d+jV_q)+jK_I(I_d+jI_q)|\f$ and then
 * \f$V_{40}=\sqrt{\max(0,V_E^2-(0.78X_{ad}I_{fd})^2)}\f$.  With measured
 * voltage \f$V_m\f$, regulator state \f$V_R\f$, washout state \f$x_F\f$, and
 * exciter output state \f$E_{fd}\f$,
 * \f[
 * T_R\dot V_m=V_T-V_m,\quad V_F=K_F(E_{fd}-x_F)/T_F,\quad
 * V_i=V_{ref}+V_{bias}+V_{set}-1+V_{SS}-V_m-V_F,
 * \f]
 * \f[
 * T_A\dot V_R=K_AV_i-V_R,\quad
 * V_B=\operatorname{lim}(V_R+V_{40},0,V_{BMAX}),\quad
 * T_E\dot E_{fd}=V_B-K_EE_{fd},\quad
 * T_F\dot x_F=E_{fd}-x_F.
 * \f]
 * The regulator uses anti-windup bounds \f$[V_{RMIN},V_{RMAX}]\f$ with
 * PSS/E's convention that \f$V_{RMAX}=0\f$ means an effectively open upper
 * limit. The regulator state is projected to either limit and held only for
 * outward drive.
 *
 * @par Equation source
 * ANDES `IEEET3` equations and PSS/E DYR schema, commit
 * eda5163c9ee8d19945a1dd5d1771fec5da608c27.
 */
class ExciterIEEET3 final: public Exciter {
  public:
    enum IEEET3Flags {
        REGULATOR_LIMITED = OBJECT_FLAG5,
        REGULATOR_LIMIT_HIGH = OBJECT_FLAG6,
    };

    explicit ExciterIEEET3(const std::string& objName = "exciterIEEET3_#");
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
    };
    Evaluation evaluate(const IOdata& inputs, double fieldVoltage, const double state[]) const;
    double sourceVoltage(const IOdata& inputs) const;
    bool updateLimitFlag(const IOdata& inputs, double state[], bool projectState);

    model_parameter Tr = 0.02;
    model_parameter Vbmax = 18.0;
    model_parameter Ke = 1.0;
    model_parameter Te = 1.0;
    model_parameter Kf = 0.1;
    model_parameter Tf = 1.0;
    model_parameter Kp = 4.0;
    model_parameter Ki = 0.1;
};
}  // namespace griddyn::exciters
