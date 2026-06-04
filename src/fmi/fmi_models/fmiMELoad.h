/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiMEWrapper.hpp"
#include "griddyn/Load.h"
#include <string>

namespace griddyn::fmi {
class FmiMESubModel;

class FmiMELoad: public FmiMEWrapper<GridLoad> {
  public:
    enum ThreePhaseFmiLoadFlags {
        IGNORE_VOLTAGE_ANGLE = OBJECT_FLAG8,
        COMPLEX_VOLTAGE = OBJECT_FLAG9,
        CURRENT_OUTPUT = OBJECT_FLAG10,
        COMPLEX_OUTPUT = OBJECT_FLAG11,
    };

  public:
    FmiMELoad(const std::string& objName = "fmiLoad_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateDataRef,
                                  const SolverMode& sMode) override;
    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;

  protected:
    IOdata translateOutput(const IOdata& fmiOutput, const IOdata& busV);
};

}  // namespace griddyn::fmi
