/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RampLoad.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include <string>
#include <vector>

namespace griddyn::loads {
/** @brief a load that generates its value from files*/
class FileLoad: public RampLoad {
  public:
    enum FileLoadFlags {
        USE_ABSOLUTE_TIME_FLAG = OBJECT_FLAG7,
        USE_STEP_CHANGE_FLAG = OBJECT_FLAG8,
    };

  protected:
    std::string fileName_;  //!< the name of the file
    gmlc::utilities::TimeSeriesMulti<double, CoreTime>
        schedLoad;  //!< time series containing the load information
    units::unit inputUnits = units::defunit;
    double scaleFactor = 1.0;  //!< scaling factor on the load
    index_t currIndex = 0;  //!< the current index on timeSeries
    count_t count = 0;
    double qratio = kNullVal;
    std::vector<int> columnkey;

  public:
    explicit FileLoad(const std::string& objName = "fileLoad_$");
    FileLoad(const std::string& objName, std::string fileName);
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void updateA(CoreTime time) override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

  private:
    count_t loadFile();
};
}  // namespace griddyn::loads
