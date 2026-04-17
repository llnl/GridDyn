#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiMEWrapper.hpp"
#include "griddyn/Governor.h"
#include <string>

namespace griddyn::fmi {
    class fmiMESubModel;
    /** class defining a governor with the dynamics through an FMI object*/
    class fmiGovernor: public fmiMEWrapper<Governor> {
      public:
        fmiGovernor(const std::string& objName = "fmiExciter_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
    };

}  // namespace griddyn::fmi
