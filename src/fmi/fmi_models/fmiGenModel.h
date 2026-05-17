/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiMEWrapper.hpp"
#include "griddyn/GenModel.h"
#include <string>

namespace griddyn::fmi {
class FmiMESubModel;

class FmiGenModel: public FmiMEWrapper<GenModel> {
  public:
    FmiGenModel(const std::string& objName = "fmiGenModel_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // virtual void dynObjectInitializeA (coreTime time0, std::uint32_t flags) override;
    // virtual void dynObjectInitializeB (const IOdata &inputs, const IOdata &desiredOutput,
    // IOdata &fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
};

}  // namespace griddyn::fmi
