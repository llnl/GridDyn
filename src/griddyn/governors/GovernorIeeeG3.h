/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "GovernorSignalHelper.h"
#include <string>
namespace griddyn::governors {
/** PSS/E IEEEG3: IEEE Type 3 governor and linear a11/a13/a21/a23 hydraulic turbine.
 * The OpenIPSL PSAT TGTypeIII equations provide the matching block realization.
 * This is distinct from both nonlinear HYGOV4 and PSLF IEEEG3_GE.
 */
class GovernorIeeeG3 final: public Governor {
  public:
    explicit GovernorIeeeG3(const std::string& name = "govIeeeG3_#");

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
    double mTg = 0.2, mTp = 0.04, mUo = 0.1, mUc = -0.1;
    double mRperm = 0.05, mRtemp = 0.35, mTr = 5.0, mTw = 1.0;
    double mA11 = 0.5, mA13 = 1.0, mA21 = 1.5, mA23 = 1.0;
    index_t mWashState = 1, mPilotState = 2, mWaterState = 3;
    detail::GovernorEvaluation<4> evaluate(const IOdata& inputs, const double* states) const;
};
}  // namespace griddyn::governors
