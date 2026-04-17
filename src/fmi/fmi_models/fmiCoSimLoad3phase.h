/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiCoSimWrapper.hpp"
#include "griddyn/loads/ThreePhaseLoad.h"
#include <string>
#include <vector>

namespace griddyn::fmi {
    class fmiCoSimSubModel;

    class fmiCoSimLoad3phase: public fmiCoSimWrapper<loads::ThreePhaseLoad> {
      public:
        enum threephasefmi_load_flags {
            ignore_voltage_angle = object_flag8,
            complex_voltage = object_flag9,
            current_output = object_flag10,
            complex_current_output = object_flag11,
        };

      public:
        fmiCoSimLoad3phase(const std::string& objName = "fmi3phase_$");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual void setFlag(const std::string& flag, bool val) override;

        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;

        virtual const std::vector<stringVec>& fmiInputNames() const override;

        virtual const std::vector<stringVec>& fmiOutputNames() const override;
    };

}  // namespace griddyn::fmi
