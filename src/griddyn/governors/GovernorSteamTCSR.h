/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GovernorSteamNR.h"
#include <string>

namespace griddyn::governors {
class GovernorSteamTCSR: public GovernorSteamNR {
  public:
  protected:
    model_parameter Trh = 0.0;  //!< [s] steam reheat chest time constant
    model_parameter Tco = 0.0;  //!< [s] steam reheat chest time constant
    model_parameter Fch = 0.0;  //!< [s] steam reheat chest time constant
    model_parameter Fip = 0.0;  //!< [s] steam reheat chest time constant
    model_parameter Flp = 0.0;  //!< [s] steam reheat chest time constant
  public:
    GovernorSteamTCSR(const std::string& objName = "govSteamTCSR_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~GovernorSteamTCSR();

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const StateData& sD,
                          double resid[],
                          const SolverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& sD,
                                  MatrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
};

}  // namespace griddyn::governors
