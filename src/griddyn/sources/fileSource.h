/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gmlc/utilities/TimeSeries.hpp"
#include "rampSource.h"
#include <string>

namespace griddyn::sources {
/** Source getting its data from a file*/
class fileSource: public rampSource {
  public:
    /** enumerations of flags used in the file source*/
    enum file_load_flags {
        use_absolute_time_flag =
            object_flag7,  //!< flag indicating use of an absolute time reference in the file
        use_step_change_flag =
            object_flag8,  //!< flag indicating a step function change on the output
    };

  private:
    std::string fileName_;  //!< name of the file
    gmlc::utilities::TimeSeries<double, coreTime>
        schedLoad;  //!< time series containing the output schedule
    index_t currIndex = 0;  //!< the current location in the file
    count_t count = 0;  //!< the total number of elements in the file
    index_t m_column = 0;  //!< the column of the file to use
    // 4 byte structure hole here
  public:
    fileSource(const std::string& fileName = "", int column = 0);

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void setFlag(std::string_view flag, bool val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    int setFile(const std::string& fileName, index_t column);
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void updateA(coreTime time) override;
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    // let predict fall through to ramp function

  private:
    /** @brief load the file*/
    int loadFile();
};
}  // namespace griddyn::sources

