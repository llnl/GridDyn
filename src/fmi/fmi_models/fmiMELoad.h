#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiMEWrapper.hpp"
#include "griddyn/Load.h"
#include <string>

namespace griddyn::fmi {
    class fmiMESubModel;

    class fmiMELoad: public fmiMEWrapper<Load> {
      public:
        enum threephasefmi_load_flags {
            ignore_voltage_angle = object_flag8,
            complex_voltage = object_flag9,
            current_output = object_flag10,
            complex_output = object_flag11,
        };

      public:
        fmiMELoad(const std::string& objName = "fmiLoad_$");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;
        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;

      protected:
        IOdata outputTranslation(const IOdata& fmiOutput, const IOdata& busV);
    };

}  // namespace griddyn::fmi
