/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Exciter.h"
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
    model_parameter Aex = 0.0;  // [pu] parameter saturation function
    model_parameter Bex = 0.0;  // [pu] parameter saturation function
  public:
    explicit ExciterIEEEtype1(const std::string& objName = "exciterIEEEtype1_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stringVec localStateNames() const override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;
    // only called if the genModel is not present
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t level) override;
};

}  // namespace griddyn::exciters
