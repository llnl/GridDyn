/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
#include "utilities/Saturation.h"
#include <array>

namespace griddyn::exciters {
/** PSS/E EXPIC1 proportional/integral excitation system.
 *
 * The implementation follows the GridKit EXPIC1 specification and source
 * block diagram. ANDES supplies the canonical 24-parameter PSS/E DYR order,
 * but maps EXPIC1 to SEXS and therefore is not an equation reference.
 * OpenIPSL does not currently provide EXPIC1.
 * GridDyn's present exciter interface supplies the voltage-reference and PSS
 * inputs but has no separate UEL/OEL ports; those two optional diagram inputs
 * are therefore zero in this realization.
 *
 * With measured voltage @f$E_T@f$, PI error @f$e_V@f$, and PI integrator
 * state @f$x_A@f$, the regulator input is
 * @f[
 * e_V=V_{ref}+V_S-E_T-V_F,\qquad
 * V_A=\operatorname{clamp}(x_A+K_AT_{A1}e_V,V_{R2},V_{R1}),
 * \qquad \dot x_A=\operatorname{antiwindup}(x_A,K_Ae_V).
 * @f]
 * The cascaded regulator and stabilizing feedback paths are
 * @f[
 * T_{A2}\dot x_{R1}=V_A-x_{R1},\qquad
 * T_{A4}\dot V_R=x_{R1}+T_{A3}\dot x_{R1}-V_R,
 * @f]
 * @f[
 * T_{F1}\dot V_{F1}=\bar V_R-V_{F1},\qquad
 * T_{F2}\dot V_F=K_F\dot V_{F1}-V_F,
 * @f]
 * where @f$\bar V_R=\operatorname{clamp}(V_R,V_{RMIN},V_{RMAX})@f$.
 * When all three regulator-filter time constants are zero, that filter is an
 * exact bypass. When @f$K_F=0@f$, the feedback path is omitted. Nonzero
 * feedback requires positive @f$T_{F1}@f$ and @f$T_{F2}@f$; partially
 * degenerate regulator filters are rejected because they require a distinct
 * algebraic realization.
 *
 * The potential/current source and PSS/E rectifier curve produce @f$V_B@f$.
 * The field path is
 * @f[
 * E_0=\operatorname{clamp}(V_B\bar V_R,E_{fd}^{min},E_{fd}^{max}),\qquad
 * T_E\dot E_{fd}=E_0-(K_E+S_E(E_{fd}))E_{fd}.
 * @f]
 * The two supplied saturation values fit the coefficient @f$S_E@f$ itself,
 * as specified by GridKit; they are not multiplied by @f$E_{fd}@f$ during the
 * fit. For @f$T_E=0@f$, the source-diagram bypass convention
 * @f$E_{fd}=E_0@f$ is used and the output is algebraic.
 */
class ExciterEXPIC1 final: public Exciter {
  protected:
    model_parameter Tr = 0.0;
    model_parameter Ta1 = 0.0;
    model_parameter Vr1 = 1.0;
    model_parameter Vr2 = -1.0;
    model_parameter Ta2 = 0.0;
    model_parameter Ta3 = 0.0;
    model_parameter Ta4 = 0.0;
    model_parameter Kf = 0.0;
    model_parameter Tf1 = 0.0;
    model_parameter Tf2 = 0.0;
    model_parameter Efdmax = 5.0;
    model_parameter Efdmin = -5.0;
    model_parameter Ke = 0.1;
    model_parameter Te = 0.5;
    model_parameter E1 = 0.0;
    model_parameter Se1 = 0.0;
    model_parameter E2 = 1.0;
    model_parameter Se2 = 0.0;
    model_parameter Kp = 0.0;
    model_parameter Ki = 0.0;
    model_parameter Kc = 0.0;
    utilities::Saturation saturation{utilities::Saturation::SaturationType::CUTOFF_QUADRATIC};

  public:
    explicit ExciterEXPIC1(const std::string& objName = "exciterEXPIC1_#");
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

  private:
    static constexpr index_t maxDifferentialStates = 7;

    struct StateLayout {
        index_t efd = kInvalidLocation;
        index_t sensedVoltage = kInvalidLocation;
        index_t piIntegrator = kInvalidLocation;
        index_t regulatorOne = kInvalidLocation;
        index_t regulator = kInvalidLocation;
        index_t feedbackOne = kInvalidLocation;
        index_t feedback = kInvalidLocation;
        index_t count = 0;
    };

    struct ModelEvaluation {
        double fieldOutput = 0.0;
        std::array<double, maxDifferentialStates> fieldStateDerivatives{};
        std::array<double, exciterInputCount> fieldInputDerivatives{};
        std::array<double, maxDifferentialStates> rates{};
        std::array<std::array<double, maxDifferentialStates>, maxDifferentialStates>
            rateStateDerivatives{};
        std::array<std::array<double, exciterInputCount>, maxDifferentialStates>
            rateInputDerivatives{};
    };

    StateLayout stateLayout() const;
    ModelEvaluation evaluateModel(const IOdata& inputs, const double state[]) const;
    double sourceMultiplier(const IOdata& inputs) const;
    bool hasRegulatorFilter() const;
    bool hasFeedbackFilter() const;
};
}  // namespace griddyn::exciters
