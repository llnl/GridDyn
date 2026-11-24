/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "GovernorSignalHelper.h"
#include <string>
#include <utility>
#include <vector>
namespace griddyn::governors {
/** Woodward PID hydro governor, following OpenIPSL PSSE.TG.WPIDHY. */
class GovernorWpidHy final: public Governor {
  public:
    explicit GovernorWpidHy(const std::string& name = "govWpidHy_#");

    CoreObject* clone(CoreObject* obj = nullptr) const override;
    [[nodiscard]] const ModelDescription& getModelDescription() const override;
    using Governor::set;
    void set(std::string_view param, double value, units::unit unitType = units::defunit) override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,
                  const StateData& data,
                  double resid[],
                  const SolverMode& mode) override;
    void derivative(const IOdata& inputs,
                    const StateData& data,
                    double deriv[],
                    const SolverMode& mode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& data,
                         double update[],
                         const SolverMode& mode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& data,
                          MatrixData<double>& matrix,
                          const IOlocs& inputLocs,
                          const SolverMode& mode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& mode) override;

  private:
    double mTreg = 1.0, mReg = -0.05, mKp = 2.0, mKi = 0.5, mKd = 0.0;
    double mTa = 0.025, mTb = 0.1, mTw = 2.3, mVelmax = 0.1, mVelmin = -0.132;
    double mGatmax = 1.0, mGatmin = 0.0, mD = 2.0;
    double mG0 = 0.08, mG1 = 0.3, mG2 = 0.75, mP1 = 0.25, mP2 = 0.75, mP3 = 1.0;
    double mDerivativeTime = 0.01;  // Modelica.Blocks.Continuous.Derivative default T.
    double mPowerReferenceOffset = 0.0;
    std::vector<std::pair<double, double>> mGateCurve;
    [[nodiscard]] std::pair<double, double> curve(double gate) const;
    detail::GovernorEvaluation<8> evaluate(const IOdata& inputs, const double* states) const;
};
}  // namespace griddyn::governors
