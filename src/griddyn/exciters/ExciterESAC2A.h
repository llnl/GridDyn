/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include "ExciterSignalHelper.h"
#include "utilities/Saturation.h"
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::exciters {
/** IEEE/PSS/E AC2A rotating excitation system.
 *
 * The routed field-current quantity Ifd is the machine's XadIfd. With
 * effective reference V0=Vref+Vbias+Vset-1, sensed/compensated voltage
 * ECOMP, and feedback lag state xF:
 * \f[
 * T_R\dot V_C=E_{COMP}-V_C,\quad
 * V_{FE}=K_EV_E+V_E S_E(V_E)+K_DI_{fd},\quad
 * T_F\dot x_F=V_{FE}-x_F,
 * \f]
 * \f[
 * e=V_0+V_{SS}-V_C-(K_F/T_F)(V_{FE}-x_F),\quad
 * T_B\dot x_{LL}=e-x_{LL},\quad
 * y_{LL}=x_{LL}+(T_C/T_B)(e-x_{LL}),
 * \f]
 * \f[
 * T_A\dot V_A=K_Ay_{LL}-V_A,\quad
 * V_R=\operatorname{clamp}(K_B(V_A-K_HV_{FE}),V_{RMIN},V_{RMAX}),
 * \quad T_E\dot V_E=V_R-V_{FE}.
 * \f]
 * VA is bounded by [VAMIN,VAMAX]. EFD=VE*FEX(KC*Ifd/VE), using every
 * rectifier segment. VE is bounded below by zero and above by the implicit
 * bound VEmax*(KE+SE(EFD(VEmax)))=VFEMAX-KD*Ifd. Ceiling saturation uses
 * rectified EFD, as wired in OpenIPSL; internal VFE uses unrectified VE.
 * The stored saturation curve is E*SE(E), a quadratic cutoff; SE1=0 disables it.
 *
 * Zero TR, TB=TC=0, and TA bypass their states. TE/TF/KA/KB/KE must be
 * positive; VFEMAX-KD*Ifd must remain positive. Outward integration at
 * regulator/field bounds is blocked and roots project overshoots. Downstream
 * signals use clipped states. This bounded realization does not establish
 * parity with OpenIPSL's internal limiter states at transitions.
 *
 * Optional uel_enabled/oel_enabled apply absolute high/low selectors after
 * KB and before the VR clamp; both default to disabled. Additive GridDyn
 * limiter actions are not interchangeable with these absolute levels.
 * Initialization solves the rectifier inverse and uses
 * VA0=(KH+1/KB)*VFE0 to satisfy nondefault gains, correcting OpenIPSL's
 * VA0=VFE0 initialization. Inconsistent selector equilibria are rejected.
 *
 * @par Equation source
 * OpenIPSL Electrical.Controls.PSSE.ES.ESAC2A, rotating-exciter,
 * rectifier, and SE blocks, commit 8155c73f. Independent external
 * trajectories remain unverified. Reader schemas and restrictions are in
 * docs/developer-guide/pss2b-esac2a-models.md.
 */
class ExciterESAC2A final: public Exciter {
  public:
    const ModelDescription& getModelDescription() const override;
    void set(std::string_view param, std::string_view val) override;
    explicit ExciterESAC2A(const std::string& objName = "exciterESAC2A_#");
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
    using Exciter::set;
    bool supportsLimiterSignal(ExciterLimiterSignal signal) const override;

  private:
    static constexpr index_t maximumStates = 5;
    using Signal = detail::ExciterSignal<maximumStates>;
    struct Layout {
        index_t voltage = kNullLocation;
        index_t lead = kNullLocation;
        index_t regulator = kNullLocation;
        index_t exciter = kNullLocation;
        index_t feedback = kNullLocation;
        index_t count = 0;
    };
    struct Evaluation {
        Signal field;
        Signal ceiling;
        Signal regulatorDrive;
        Signal exciterDrive;
        std::array<Signal, maximumStates> rates{};
    };
    Layout layout() const;
    Evaluation evaluate(const IOdata& inputs, const double state[]) const;
    Signal rectified(const Signal& voltage, const Signal& current) const;
    Signal ceiling(const Signal& current) const;
    Signal saturated(const Signal& voltage) const;
    bool projectLimits(const IOdata& inputs);
    model_parameter Tr = 0.0;
    model_parameter Tb = 0.0;
    model_parameter Tc = 0.0;
    model_parameter Te = 0.8;
    model_parameter Vamax = 9.0;
    model_parameter Vamin = -5.43;
    model_parameter Kb = 1.0;
    model_parameter Vfemax = 4.4;
    model_parameter Kh = 0.0;
    model_parameter Kf = 0.03;
    model_parameter Tf = 1.0;
    model_parameter Kc = 0.2;
    model_parameter Kd = 0.48;
    model_parameter Ke = 1.0;
    model_parameter E1 = 5.25;
    model_parameter Se1 = 0.03;
    model_parameter E2 = 7.0;
    model_parameter Se2 = 0.1;
    bool underSelectorEnabled = false;
    bool overSelectorEnabled = false;
    utilities::Saturation saturation{utilities::Saturation::SaturationType::CUTOFF_QUADRATIC};
};
}  // namespace griddyn::exciters
