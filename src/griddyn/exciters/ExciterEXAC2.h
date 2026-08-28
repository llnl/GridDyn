/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ExciterEXAC1.h"
#include <string>

namespace griddyn::exciters {
/** PSS/E EXAC2 excitation system.
 *
 * EXAC2 shares the EXAC1 transducer, lead-lag, field integrator, washout,
 * saturation, and rectifier equations.  It replaces the regulator path by
 * @f[
 * v_H=v_A-K_Hv_{FE},\quad v_L=K_L(V_{LR0}-v_{FE}),\quad
 * v_R=\operatorname{limit}\{K_B\min(v_H,v_L)\}.
 * @f]
 * The @f$K_A@f$ lag is anti-windup limited by @f$[V_{AMIN},V_{AMAX}]@f$ and
 * @f$V_{LR0}=\max(V_{LR},v_{FE0}+v_{FE0}/(K_LK_B))@f$ is retained after
 * initialization.  The same intentional sensed-voltage correction as EXAC1
 * applies; see ExciterEXAC1 and the ANDES compatibility guide.
 */
class ExciterEXAC2 final: public ExciterEXAC1 {
  protected:
    model_parameter Vamax = 8.0;
    model_parameter Vamin = 0.0;
    model_parameter Vlr = 0.0;
    model_parameter Kl = 1.0;
    model_parameter Kh = 1.0;
    model_parameter Kb = 1.0;
    double vlr0 = 0.0;

  public:
    explicit ExciterEXAC2(const std::string& objName = "exciterEXAC2_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

  protected:
    double regulatorTarget(const IOdata& inputs, const double state[]) const override;
    double regulatorUpperLimit() const override;
    double regulatorLowerLimit() const override;
    double initialRegulatorState(double vfe) const override;
    double referenceOffset(double vfe) const override;
    void regulatorTargetDerivatives(const IOdata& inputs,
                                    const double state[],
                                    double& regulatorDerivative,
                                    double& exciterDerivative,
                                    double& fieldCurrentDerivative) const override;
};
}  // namespace griddyn::exciters
