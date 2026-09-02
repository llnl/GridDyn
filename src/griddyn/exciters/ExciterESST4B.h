/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../Exciter.h"
#include <string>

namespace griddyn::exciters {
/** IEEE/PSS/E ESST4B potential-source controlled-rectifier exciter.
 *
 * The differential states are terminal-voltage measurement, outer PI
 * integral, regulator lag, and inner PI integral.  The algebraic field voltage
 * is \f$E_{fd}=V_B V_M\f$, where \f$V_B\f$ uses the IEEE commutating-reactance
 * curve shared with ESST3A.  The two PI outputs are
 * \f[
 * V_R=\operatorname{lim}(K_{PR}e_R+x_R),\quad
 * V_M=\operatorname{lim}(K_{PM}e_M+x_M),
 * \f]
 * with \f$e_R=V_{ref}+V_{SS}-V_{meas}\f$ and
 * \f$e_M=V_A-K_GE_{fd}\f$. As in OpenIPSL, the integral states obey
 * \f$V_{RMIN}/K_{PR}\le x_R\le V_{RMAX}/K_{PR}\f$ and
 * \f$V_{MMIN}/K_{PM}\le x_M\le V_{MMAX}/K_{PM}\f$, with outward integration
 * blocked at each bound. OpenIPSL supplies the governing equations; ANDES
 * supplies an independent native implementation and the PSS/E DYR field
 * order.
 *
 * @note GridDyn currently has no routed UEL/OEL inputs. Their normal inactive
 * values are used; VOTHSG is supplied through the standard VSS input.
 * @see OpenIPSL.Electrical.Controls.PSSE.ES.ESST4B
 */
class ExciterESST4B final: public Exciter {
  protected:
    model_parameter Tr = 0.01;
    model_parameter Kpr = 1.0;
    model_parameter Kir = 0.0;
    model_parameter Ta = 0.1;
    model_parameter Kpm = 1.0;
    model_parameter Kim = 0.0;
    model_parameter Vmmax = 8.0;
    model_parameter Vmmin = 0.0;
    model_parameter Kg = 1.0;
    model_parameter Kp = 4.0;
    model_parameter Ki = 0.1;
    model_parameter Vbmax = 18.0;
    model_parameter Kc = 0.1;
    model_parameter Xl = 0.01;
    model_parameter ThetaP = 0.0;

  public:
    explicit ExciterESST4B(const std::string& objName = "exciterESST4B_#");
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

  private:
    double rectifierVoltage(const IOdata& inputs) const;
};
}  // namespace griddyn::exciters
