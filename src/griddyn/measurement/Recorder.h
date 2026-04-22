/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "collector.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
/** class to store and save data from the grid, based on a collector */
class Recorder: public collector {
  protected:
    coreTime lastSaveTime = negTime;  //!< the last time the recorder saved to file
    gmlc::utilities::TimeSeriesMulti<double, coreTime> dataset;  //!< the actual time series data
    std::string fileName_;  //!< the fileName to store the data
    std::string directory_;  //!< the directory to generate the specified file

    bool binaryFile = true;  //!< flag indicating if the file is binary
    bool firstTrigger = true;  //!< flag indicating that the recorder has not been triggered yet
    std::int16_t precision = -1;  //!< precision for writing text files.
    count_t autosave =
        0;  //!< flag indicating the recorder should autosave after the given number of points
  public:
    Recorder(coreTime time0 = timeZero, coreTime period = 1.0);
    explicit Recorder(const std::string& name);
    /** destructor will attempt to save the data*/
    ~Recorder();

    virtual std::unique_ptr<collector> clone() const override;
    /** duplicate the collector to a valid event
    @param a pointer to a collector object
    */
    virtual void cloneTo(collector* col) const override;

    virtual change_code trigger(coreTime time) override;

    /** save the data to a file
    @param[in] fileName the name of the file to save the data to
    */
    void saveFile(const std::string& fileName = "");
    /** set the total number of points the recorder has allocated space for
    @param span the total time period the recorder can save space for
    */
    void setSpace(coreTime span);
    /** tell the recorder to allocate space for an additional period of time
    @param span the total time period the recorder can save space for
    */
    void addSpace(coreTime span);

    void set(std::string_view param, double val) override;
    void set(std::string_view param, std::string_view val) override;

    virtual void flush() override;
    virtual const std::string& getSinkName() const override;
    /** get the current filename
    @return a const string reference to the name of the file*/
    const std::string& getFileName() const { return fileName_; }
    /** get the current target directory
    @return a const string reference to the name of the directory*/
    const std::string& getDirectory() const { return directory_; }
    /** reset the recorder
    @details clears all the stored data
    */
    void reset();

    /** get the underlying timeSeries object*/
    const auto& getTimeSeries() const { return dataset; }
    /** get a vector of the time data*/
    const std::vector<coreTime>& getTime() const { return dataset.time(); }

    /** get a vector of the stored data for a particular column
    @param[in] col the column of data to request
    */
    const std::vector<double>& getData(count_t col) const
    {
        return dataset.data((col < columns) ? col : 0);
    }

  private:
    /** generate the field names for the data set*/
    void fillDatasetFields();
};

}  // namespace griddyn

