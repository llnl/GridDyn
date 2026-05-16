/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiMEWrapper.hpp"
#include "griddyn/Governor.h"
#include <string>

namespace griddyn::fmi {
class FmiMESubModel;
/** class defining a governor with the dynamics through an FMI object*/
class FmiGovernor: public FmiMEWrapper<Governor> {
  public:
    FmiGovernor(const std::string& objName = "fmiExciter_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
};

}  // namespace griddyn::fmi
