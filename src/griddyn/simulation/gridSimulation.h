/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

// header files
#include "../Area.h"
#include "griddyn/griddyn-config.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace helics {
class Logger;
class LoggerNoThread;
}  // namespace helics

namespace griddyn {
class Link;
class gridBus;

#define SINGLE (1)
#define MULTICORE (2)
#define MULTIPROCESSOR (3)

// define ERROR codes for the error state
#define GS_NO_ERROR 0
#define GS_FILE_LOAD_ERROR (-1)
#define GS_INVALID_FILE_ERROR (-2)

// define PRINT LEVELS

class collector;
class Event;
class Relay;
class eventQueue;
class eventAdapter;
class functionEventAdapter;

/** @brief Grid simulation object
 GridSimulation is a base simulation class its intention is to handle some of the basic
simulation bookkeeping tasks that would be common to all simulations in GridDyn including things
like logging, errors,  recording, and a few other topics.  Doesn't really do anything much as far as
simulating goes the class isn't abstract but it is intended to be built upon for more usable
simulation objects
*/
class gridSimulation: public Area {
  public:
    std::string sourceFile;  //!< main source file name
    /** @brief enumeration describing the state of the GridDyn simulation*/
    enum class gridState_t : int {
        HALTED = -2,  //!< the system is halted for some reason
        GD_ERROR = -1,  //!< the system has an error
        STARTUP = 0,  //!< the system is starting up either loaded or unloaded
        INITIALIZED = 1,  //!< the system has completed a power flow initialization step and is
                          //!< ready to proceed to
        //! power flow calculations
        POWERFLOW_COMPLETE = 2,  //!< the system has completed a power flow calculation successfully
        DYNAMIC_INITIALIZED = 3,  //!< the system has completed a dynamic initialization step
        DYNAMIC_COMPLETE =
            4,  //!< the system has completed a dynamic system calculation and advanced to the
        //! desired time successfully
        DYNAMIC_PARTIAL = 5,  //!< the system has completed a dynamic system calculation but has not
                              //!< reached the desired time
    };
    print_level consolePrintLevel =
        print_level::summary;  //!< logging level for printing to the console
    print_level logPrintLevel =
        print_level::summary;  //!< logging level for saving to a file (if a file was specified)
  protected:
    std::string logFile;  //!< log file name

    std::string stateFile;  //!< record file for the state
    std::string recordDirectory;  //!< folder location for storing recorded files
    std::string version;  //!< storage for the version string
    coreTime state_record_period = negTime;  //!< how often to record the state
#ifdef ENABLE_MULTITHREADING
    std::unique_ptr<helics::Logger> gridLog;  //!< the object that does the logging
#else
    std::unique_ptr<helics::LoggerNoThread> gridLog;  //!< the object that does the logging
#endif
    std::function<void(int, const std::string&)>
        customLogger;  //!< callback for a custom logging function
    std::shared_ptr<functionEventAdapter>
        stateRecorder;  //!< a recorder for recording the state on a periodic basis
    gridState_t pState =
        gridState_t::STARTUP;  //!< the system state keeps track of which state the solver is in
    int errorCode =
        GS_NO_ERROR;  //!< for storage of an ERROR code if one exists (intended to be expandable to
    //! children objects so using int instead of enum)
    count_t alertCount = 0;  //!< count the number of alerts
    count_t warnCount = 0;  //!<  count the number of warnings from objects
    count_t errorCount = 0;  //!< count the number of logged warnings
    // ---------------- clock ----------------

    coreTime startTime = timeZero;  //!< [s]  start time
    coreTime stopTime = 30.0;  //!< [s]  end time
    coreTime currentTime = timeZero;  //!< [s] the current time
    std::atomic<coreTime> simulationTime;  //!< [s]  current time
    coreTime stepTime = coreTime(0.05);  //!< [s]  time step
    coreTime timeReturn = timeZero;  //!< [s]  time returned by the solver
    coreTime nextStopTime = negTime;  //!< next time to stop the dynamic Simulation

    coreTime minUpdateTime = coreTime(0.0001);  //!< minimum time period to go between updates; for
                                                //!< the hybrid simultaneous partitioned solution
    coreTime maxUpdateTime = maxTime;  //!<(s) max time period to go between updates
    double absTime = 0;  //!< [s] seconds in unix time of the system start time;

    // ---------------- recorders ----------------
    coreTime recordStart = negTime;  //!< [s]  recorder start time
    coreTime recordStop = maxTime;  //!< [s]  recorder stop time
    std::vector<std::shared_ptr<collector>> collectorList;  //!< vector storing recorder objects
    coreTime nextRecordTime = maxTime;  //!< time for the next set of recorders

    coreTime lastStateRecordTime = negTime;  //!< last time the full state was recorded

    // ----------------timestepP -----------------
    std::unique_ptr<eventQueue> EvQ;  //!< the event queue for the simulation system

  public:
    /** @brief constructor*/
    explicit gridSimulation(const std::string& objName = "sim_#");

    /** @brief destructor
     */
    ~gridSimulation();
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    /** @brief get the current state of the simulation
    @return the current state
    */
    gridState_t currentProcessState() const { return pState; }
    /** @brief set the error code,  also sets the state to GD_ERROR
    @param[in] ecode the error code
    */
    void setErrorCode(int ecode);
    /** @brief get the current error code
    @return the current error code
    */
    int getErrorCode() const { return errorCode; }

    // add components
    using Area::add;  // use the add function of Area
    /** @brief function to add collectors to the system
    @param[in] col the collector to add into the simulation
    */
    virtual void add(std::shared_ptr<collector> col);
    /** @brief function to add events to the system
    @param[in] evnt the event to add into the simulation
    */
    virtual void add(std::shared_ptr<Event> evnt);
    /** @brief function to add a list of events to the system
    @param[in] elist a vector of events to add in
    */
    virtual void add(const std::vector<std::shared_ptr<Event>>& elist);
    /** @brief function to add an event Adapter to the event Queue
    @param[in] eA the eventAdpater to add
    */
    virtual void add(std::shared_ptr<eventAdapter> eA);

    // TODO(phlpt): Recheck whether this function is actually needed.
    /** @brief reset all object counters to 0*/
    static void resetObjectCounters();

    /** @brief function to find a specific collector by name
    @param[in] collectorName  the name of the recorder to find
    @return a shared_ptr to the recorder that was found or an empty shared ptr*/
    std::shared_ptr<collector> findCollector(const std::string& collectorName);
    /** @brief get all the objects from the event Queue */
    void getEventObjects(std::vector<coreObject*>& objV) const;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    /** @brief run the simulator
    @param[in] finishTime  the time to run to
    @return return code 0 for success other for failure
    */
    virtual int run(coreTime finishTime = negTime);
    /** @brief have the simulator step forward in time
    @return return code 0 for success other for failure
    */
    virtual int step();

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual std::string getString(const std::string& param) const override;
    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    void alert(coreObject* object, int code) override;
    virtual void log(coreObject* object, print_level level, const std::string& message) override;

    /** @brief save all the recorder data to files
     all the recorders have files associated with them that get automatically saved at certain
    points this function forces them to do a save operation*/
    void saveRecorders();

    /**
     * @brief Gets the current simulation time.
     @details currentTime is atomic and can be used in a multithreaded context to observe the
     current progress of an asyncrhonously running dynamic simulation
     * @return a coreTime representing the current simulation time.

     */
    coreTime getSimulationTime() const { return currentTime; }

    /**
     * @brief Gets the simulation start time.
     * @return a double representing the simulation start time, in seconds.
     */
    coreTime getStartTime() const { return startTime; }
    /**
     * @brief Gets the simulation stop time.
     * @return a double representing the simulation start time, in seconds.
     */
    coreTime getStopTime() const { return stopTime; }
    /**
     * @brief Gets the simulation step time.
     * @return a time representing the simulation start time, in seconds.
     */
    coreTime getStepTime() const { return stepTime; }

    /**
     * @brief gets the next event time.
     * @return a double representing the next scheduled event in GridDyn.
     */
    coreTime getEventTime() const;
    /**
     * @brief gets the next event time.
     * @param[in] eventCode a code corresponding to a specific type of event
     * @return a time representing the next scheduled event in GridDyn.
     */
    coreTime getEventTime(int eventCode) const;

    /** set a custom logging function
     */
    void setLogger(std::function<void(int, const std::string&)> loggingFunction);
};

/** @brief find an object that has the same properties as obj1 located int the tree from src in the
tree given by sec
@param[in] obj1  the object to search for
@param[in] src  the tree the current object is associated with
@param[in] sec the tree to do the search in
@return the located object or nullptr
*/
coreObject* findMatchingObject(coreObject* obj1, gridPrimary* src, gridPrimary* sec);

}  // namespace griddyn
