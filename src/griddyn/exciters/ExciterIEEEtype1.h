/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
#include "utilities/Saturation.h"
#include <string>
namespace griddyn::exciters {

/** @brief IEEE Type 1 exciter
 */
class ExciterIEEEtype1: public Exciter {
  protected:
    model_parameter Ke = 1.0;  // [pu] self-excited field
    model_parameter Te = 1.0;  // [s]    exciter time constant
    model_parameter Kf = 0.03;  // [pu] stabilizer gain
    model_parameter Tf = 1.0;  // [s]    stabilizer time constant
    model_parameter Tr = 0.0;  // [s] terminal-voltage transducer time constant
    model_parameter E1 = 0.0;  // [pu] first saturation voltage
    model_parameter Se1 = 0.0;  // [pu] saturation at E1
    model_parameter E2 = 1.0;  // [pu] second saturation voltage
    model_parameter Se2 = 0.0;  // [pu] saturation at E2
    utilities::Saturation saturation{utilities::Saturation::SaturationType::CUTOFF_QUADRATIC};
    // Retained for the legacy IEEE Type 2 implementation, which has its own
    // saturation path. IEEET1 and the DC models use the two-point curve above.
    model_parameter Aex = 0.0;
    model_parameter Bex = 0.0;
  public:
    explicit ExciterIEEEtype1(const std::string& objName = "exciterIEEEtype1_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param, units::unit unitType = units::defunit) const override;

    virtual stringVec localStateNames() const override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& sD,
                            double deriv[],
                            const SolverMode& sMode) override;
    // only called if the genModel is not present
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  MatrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& sD,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

  protected:
    void configureSaturation();
    double saturationFeedback(double fieldVoltage) const;
    double saturationDerivative(double fieldVoltage) const;
    double regulatorUpperLimit() const;
    double measuredVoltage(const IOdata& inputs, const double state[]) const;
};

}  // namespace griddyn::exciters
