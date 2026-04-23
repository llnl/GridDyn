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
    model_parameter Trh;  //!< [s] steam reheat chest time constant
    model_parameter Tco;  //!< [s] steam reheat chest time constant
    model_parameter Fch;  //!< [s] steam reheat chest time constant
    model_parameter Fip;  //!< [s] steam reheat chest time constant
    model_parameter Flp;  //!< [s] steam reheat chest time constant
  public:
    GovernorSteamTCSR(const std::string& objName = "govSteamTCSR_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual ~GovernorSteamTCSR();

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const override;

    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
};

}  // namespace griddyn::governors
