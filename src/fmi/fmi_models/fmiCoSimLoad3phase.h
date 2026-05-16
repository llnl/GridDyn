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
class FmiCoSimSubModel;

class FmiCoSimLoad3phase: public FmiCoSimWrapper<loads::ThreePhaseLoad> {
  public:
    enum ThreePhaseFmiLoadFlags {
        IGNORE_VOLTAGE_ANGLE = object_flag8,
        COMPLEX_VOLTAGE = object_flag9,
        CURRENT_OUTPUT = object_flag10,
        COMPLEX_CURRENT_OUTPUT = object_flag11,
    };

  public:
    FmiCoSimLoad3phase(const std::string& objName = "fmi3phase_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void setFlag(std::string_view flag, bool val) override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;

    virtual const std::vector<stringVec>& fmiInputNames() const override;

    virtual const std::vector<stringVec>& fmiOutputNames() const override;
};

}  // namespace griddyn::fmi
