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
        IGNORE_VOLTAGE_ANGLE = OBJECT_FLAG8,
        COMPLEX_VOLTAGE = OBJECT_FLAG9,
        CURRENT_OUTPUT = OBJECT_FLAG10,
        COMPLEX_CURRENT_OUTPUT = OBJECT_FLAG11,
    };

  public:
    FmiCoSimLoad3phase(const std::string& objName = "fmi3phase_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void setFlag(std::string_view flag, bool val) override;

    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;

    virtual const std::vector<stringVec>& getFmiInputNames() const override;

    virtual const std::vector<stringVec>& getFmiOutputNames() const override;
};

}  // namespace griddyn::fmi
