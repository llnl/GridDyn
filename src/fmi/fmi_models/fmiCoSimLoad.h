/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiCoSimWrapper.hpp"
#include "griddyn/Load.h"
#include <string>

namespace griddyn::fmi {
class FmiCoSimLoad: public FmiCoSimWrapper<Load> {
  public:
    enum ThreePhaseFmiLoadFlags {
        IGNORE_VOLTAGE_ANGLE = object_flag8,
        COMPLEX_VOLTAGE = object_flag9,
        CURRENT_OUTPUT = object_flag10,
        COMPLEX_OUTPUT = object_flag11,
    };

  public:
    FmiCoSimLoad(const std::string& objName = "fmiLoad_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
};

}  // namespace griddyn::fmi
