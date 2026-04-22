/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../events/eventInterface.hpp"
#include "core/helperObject.h"
#include "core/objectOperatorInterface.hpp"
#include "units/units.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>
/** @file
@brief define a classes and information related to data retrieval in griddyn
*/
namespace griddyn {
/** helper data class for defining the information necessary to fully specify and a data grabber*/
class gridGrabberInfo {
  public:
    std::string m_target;  //!< name of the object to target
    std::string field;  //!< the field to record
    std::string rString;  //!< a string defining the recorder
    int column = -1;  //!< (suggestion) which column to stick the data in
    index_t offset = kNullLocation;  //!< the offset to use to numerically pick off the state
    double gain = 1.0;  //!< a multiplier factor for the results
    double bias = 0.0;  //!< a shift factor of the results
    units::unit outputUnits = units::defunit;  //!< which units to output the data
  public:
    gridGrabberInfo() = default;
};

class gridGrabber;
class stateGrabber;

/** base class for capturing and storing data from a grid simulation */
class collector: public helperObject, public eventInterface, public objectOperatorInterface {
  protected:
    count_t warningCount = 0;  //!< counter for the number of warnings
    // there is currently a 4 byte gap here
    std::vector<std::string> warnList;  //!< listing for the number of warnings
    coreTime timePeriod;  //!< the actual period of the collector
    coreTime reqPeriod;  //!< the requested period of the collector
    coreTime startTime = negTime;  //!< the time to start collecting
    coreTime stopTime = maxTime;  //!< the time to stop collecting
    coreTime triggerTime = maxTime;  //!< the next trigger time for the collector
    coreTime lastTriggerTime = negTime;  //!< the last time the collector was triggered

    /** data structure to capture the grabbers and location for a specific grabber*/
    class collectorPoint {
      public:
        std::shared_ptr<gridGrabber>
            dataGrabber;  //!< the grabber for the data from the object directly
        std::shared_ptr<stateGrabber>
            dataGrabberSt;  //!< the grabber for the data from the object state
        int column = -1;  //!< the starting column for the data
        int columnCount = 1;  //!< the number of columns associated with the point
        std::string colname;  //!< the name for the data collected
        collectorPoint(std::shared_ptr<gridGrabber> dg,
                       std::shared_ptr<stateGrabber> sg,
                       int ncol = -1,
                       int ccnt = 1,
                       const std::string& cname = ""):
            dataGrabber(std::move(dg)), dataGrabberSt(std::move(sg)), column(ncol),
            columnCount(ccnt), colname(cname)
        {
        }
    };

    std::vector<collectorPoint> points;  //!< the data grabbers
    std::vector<double> data;  //!< vector to grab store the most recent data
    count_t columns = 0;  //!< the length of the data vector
    bool recheck = false;  //!< flag indicating that the recorder should recheck all the fields
    bool armed = true;  //!< flag indicating if the recorder is armed and ready to go
    bool delayProcess = true;  //!< wait to process recorders until other events have executed
    bool vectorName = false;  //!< indicator to use vector notation for the name
  public:
    collector(coreTime time0 = timeZero, coreTime period = timeOneSecond);
    explicit collector(const std::string& collectorName);

    /** duplicate the collector
    @return a pointer to the clone of the event
    */
    virtual std::unique_ptr<collector> clone() const;
    /** duplicate the collector to a valid event
    @param col a pointer to a collector object
    */
    virtual void cloneTo(collector* col) const;

    virtual void updateObject(coreObject* gco,
                              object_update_mode mode = object_update_mode::direct) override;

    /** function to grab the data to specific location
    @param[out] outputData the location to place the captured values
    @param[in] N the size of the data storage location
    @return the number of data points stored
    */
    count_t grabData(double* outputData, index_t N);
    virtual change_code trigger(coreTime time) override;
    /** do a check to check and assign all columns*/
    void recheckColumns();
    coreTime nextTriggerTime() const override { return triggerTime; }
    event_execution_mode executionMode() const override
    {
        return (delayProcess) ? event_execution_mode::delayed : event_execution_mode::normal;
    }

    virtual void add(std::shared_ptr<gridGrabber> ggb, int requestedColumn = -1);
    virtual void add(std::shared_ptr<stateGrabber> sst, int requestedColumn = -1);
    virtual void add(const gridGrabberInfo& gdRI, coreObject* obj);
    virtual void add(const std::string& field, coreObject* obj);
    virtual void add(std::shared_ptr<gridGrabber> ggb,
                     std::shared_ptr<stateGrabber> sst,
                     int requestedColumn = -1);

    bool isArmed() const override { return armed; }

    virtual void set(std::string_view param, double val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void setFlag(std::string_view flag, bool val) override;

    virtual void setTime(coreTime time);

    virtual coreObject* getObject() const override;

    virtual void getObjects(std::vector<coreObject*>& objects) const override;
    /** flush the buffer to a file or other sink*/
    virtual void flush();
    /** get a name or description associated with the sink of the data*/
    virtual const std::string& getSinkName() const;

    /** the the most recent value associated with a particular column
    @param[in] column the column of the data to get
    @return the most recent value
    */
    double getValue(index_t column) const
    {
        return isValidIndex(column, data) ? data[column] : kNullVal;
    }
    /** get a list of all the descriptions of the columns */
    virtual std::vector<std::string> getColumnDescriptions() const;
    /** get the current warning count*/
    count_t getWarningCount() const { return warningCount; }
    /** get a list of the warnings that were generated on construction
    @return a vector of the warnings
    */
    const std::vector<std::string>& getWarnings() const { return warnList; }
    /** erase the warning list*/
    void clearWarnings()
    {
        warnList.clear();
        warningCount = 0;
    }

    /** clear all grabbers from the collector*/
    void reset();
    /** get the number of points in the collector*/
    count_t numberOfPoints() const { return static_cast<count_t>(points.size()); }

  protected:
    /** callback intended more for derived classes to indicate that a dataPoint has been added*/
    virtual void dataPointAdded(const collectorPoint& cp);
    /** get a column number, the requested column is a request only
    *@param[in] requestedColumn the column that is being requested
    @return the actual column granted*/
    int getColumn(int requestedColumn) const;

    void updateColumns(int requestedColumn);
    void addWarning(const std::string& warnMessage)
    {
        warnList.push_back(warnMessage);
        ++warningCount;
    }
    void addWarning(std::string&& warnMessage)
    {
        warnList.push_back(warnMessage);
        ++warningCount;
    }
};

/** @brief make a collector from a string
@param[in] type the type of collector to create
@return a shared_ptr to a collector object
*/
std::unique_ptr<collector> makeCollector(const std::string& type, const std::string& name = "");

}  // namespace griddyn
