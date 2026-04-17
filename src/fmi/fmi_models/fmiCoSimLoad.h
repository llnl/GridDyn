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
    class fmiCoSimLoad: public fmiCoSimWrapper<Load> {
      public:
        enum threephasefmi_load_flags {
            ignore_voltage_angle = object_flag8,
            complex_voltage = object_flag9,
            current_output = object_flag10,
            complex_output = object_flag11,
        };

      public:
        fmiCoSimLoad(const std::string& objName = "fmiLoad_$");
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
