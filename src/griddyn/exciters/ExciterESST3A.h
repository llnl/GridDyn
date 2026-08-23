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
/** IEEE/PSS/E ESST3A static excitation system.
 *
 * The implementation follows ANDES 2.0.0, including the potential-source
 * phasor calculation, piecewise rectifier regulation, input and feedback
 * limiters, and the two anti-windup regulator lags.
 */
class ExciterESST3A: public Exciter {
  public:
    enum ESST3AFlags {
        VR_LIMITED = OBJECT_FLAG5,
        VR_LIMIT_HIGH = OBJECT_FLAG6,
        VM_LIMITED = OBJECT_FLAG7,
        VM_LIMIT_HIGH = OBJECT_FLAG8,
    };

  protected:
    model_parameter Tr = 0.01;
    model_parameter Vimax = 0.8;
    model_parameter Vimin = -0.1;
    model_parameter Km = 500.0;
    model_parameter Tc = 3.0;
    model_parameter Tb = 15.0;
    model_parameter Kg = 1.0;
    model_parameter Kp = 4.0;
    model_parameter Ki = 0.1;
    model_parameter Vbmax = 18.0;
    model_parameter Kc = 0.1;
    model_parameter Xl = 0.01;
    model_parameter Vgmax = 4.0;
    model_parameter ThetaP = 0.0;
    model_parameter Tm = 0.1;
    model_parameter Vmmax = 1.0;
    model_parameter Vmmin = 0.1;

  public:
    explicit ExciterESST3A(const std::string& objName = "exciterESST3A_#");
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
    double rectifierVoltage(const IOdata& inputs) const;
    double leadLagOutput(double voltageMeasurement, double leadLag) const;
    double vrDrive(double voltageMeasurement, double leadLag, double regulatorVoltage) const;
    double vmDrive(double fieldVoltage, double regulatorVoltage, double fieldRegulator) const;
    bool hasDynamicVoltageRegulator() const;
};
}  // namespace griddyn::exciters
