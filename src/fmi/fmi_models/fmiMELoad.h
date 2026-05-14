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
class fmiMESubModel;

class fmiMELoad: public fmiMEWrapper<Load> {
  public:
    enum ThreePhaseFmiLoadFlags {
        IGNORE_VOLTAGE_ANGLE = object_flag8,
        COMPLEX_VOLTAGE = object_flag9,
        CURRENT_OUTPUT = object_flag10,
        COMPLEX_OUTPUT = object_flag11,
    };

  public:
    fmiMELoad(const std::string& objName = "fmiLoad_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& stateDataRef,
                                  const solverMode& sMode) override;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;

  protected:
    IOdata outputTranslation(const IOdata& fmiOutput, const IOdata& busV);
};

}  // namespace griddyn::fmi
