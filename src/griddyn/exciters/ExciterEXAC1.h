/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
#include "utilities/Saturation.h"
#include <string>
#include <vector>

namespace griddyn::exciters {
/** PSS/E EXAC1 alternating-current excitation system.
 *
 * This class implements the EXAC1 record in the PSS/E Model Library and the
 * frozen ANDES 2.0.0 parameter contract.  With the measured voltage
 * @f$v_m@f$, lead-lag state @f$x_L@f$, limited regulator state @f$v_A@f@,
 * exciter voltage @f$v_E@f@, and washout state @f$x_F@f@, the equations are
 * @f[
 * T_R\dot v_m=V_t-v_m,\quad T_B\dot x_L=v_i-x_L,\quad
 * y_L=x_L+\frac{T_C}{T_B}(v_i-x_L),
 * @f]
 * @f[
 * T_A\dot v_A=K_Ay_L-v_A,\quad
 * T_E\dot v_E=v_R-v_{FE},\quad T_F\dot x_F=v_{FE}-x_F,
 * @f]
 * where @f$v_i=V_{ref}-v_m-K_F(v_{FE}-x_F)/T_F@f$,
 * @f$v_{FE}=K_Ev_E+S_E(v_E)+K_DX_{ad}I_{fd}@f$, and
 * @f$E_{fd}=v_EF_{EX}(K_CX_{ad}I_{fd}/v_E)@f$.  The regulator state has an
 * anti-windup limiter @f$[V_{RMIN},V_{RMAX}]@f$.  When @f$T_R=0@f$, the
 * transducer is bypassed: @f$v_m=V_t@f$ and no measured-voltage state is
 * allocated.
 *
 * Frozen ANDES constructs the @f$T_R@f$ transducer but subtracts raw terminal
 * voltage from the regulator input.  GridDyn intentionally uses @f$v_m@f$ as
 * required by the published AC1 block diagram; see
 * docs/developer-guide/andes-compatibility.md for the compatibility note and
 * an issue draft.  References: IEEE Std 421.5-2016 and the PSS/E Model
 * Library EXAC1 description.
 */
class ExciterEXAC1: public Exciter {
  public:
    enum EXACFlags {
        REGULATOR_LIMITED = OBJECT_FLAG5,
        REGULATOR_LIMIT_HIGH = OBJECT_FLAG6,
    };

  protected:
    model_parameter Tr = 0.01;
    model_parameter Tb = 1.0;
    model_parameter Tc = 1.0;
    model_parameter Te = 0.8;
    model_parameter Kf = 0.1;
    model_parameter Tf = 1.0;
    model_parameter Kc = 0.1;
    model_parameter Kd = 0.0;
    model_parameter Ke = 1.0;
    model_parameter E1 = 0.0;
    model_parameter Se1 = 0.0;
    model_parameter E2 = 1.0;
    model_parameter Se2 = 0.0;
    utilities::Saturation saturation{utilities::Saturation::SaturationType::QUADRATIC};

  public:
    explicit ExciterEXAC1(const std::string& objName = "exciterEXAC1_#");
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

  protected:
    virtual double regulatorTarget(const IOdata& inputs, const double state[]) const;
    virtual double regulatorUpperLimit() const;
    virtual double regulatorLowerLimit() const;
    virtual double initialRegulatorState(double vfe) const;
    virtual double referenceOffset(double vfe) const;
    virtual void regulatorTargetDerivatives(const IOdata& inputs,
                                            const double state[],
                                            double& regulatorDerivative,
                                            double& exciterDerivative,
                                            double& fieldCurrentDerivative) const;
    double referenceInput(const IOdata& inputs) const;
    double vfe(const IOdata& inputs, const double state[]) const;
    double rectifierFactor(const IOdata& inputs, double exciterVoltage) const;
    double fieldVoltage(const IOdata& inputs, const double state[]) const;
    int regulatorLimitStatus(const double state[]) const;
    bool updateLimitFlags(const double state[]);
};
}  // namespace griddyn::exciters
