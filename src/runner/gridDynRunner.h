/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/gridDynDefinitions.hpp"
#include "griddyn/griddyn-config.h"
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>

#ifndef GRIDDYN_PENDING
#    define GRIDDYN_PENDING (25)
#endif
// forward declaration for CLI::App
namespace CLI {
class App;
}  // namespace CLI

namespace griddyn {
class gridDynSimulation;
class readerInfo;
/**
 * Build and run a GridDyn simulation.
 */
class GriddynRunner {
  public:
    /** constructor*/
    GriddynRunner();
    GriddynRunner(std::shared_ptr<gridDynSimulation> sim);
    /** Destructor*/
    virtual ~GriddynRunner();
    int InitializeFromString(const std::string& cmdargs);

    /**
     * initialize a simulation run from command line arguments.
     @param[in] argc the number of console arguments
     @param[in] argv the actual console arguments
     @param[in] allowUnrecognized set to true to indicate that the unrecognized arguments should be
     allowed
     @return >0 normal stop,  0 normal, <0 error
     */
    virtual int Initialize(int argc, char* argv[], bool allowUnrecognized = false);
    /**
    * initialize a simulation run from command line arguments using a given readerInfo structure
    @param[in] argc the number of console arguments
    @param[in] argv the actual console arguments
    @param[in] ri the readerInfo structure that contains any additional reader information
    @return >0 normal stop,  0 normal, <0 error
    */
    int Initialize(int argc,
                   char* argv[],
                   readerInfo& readerInformation,
                   bool allowUnrecognized = false);
    /** initialization the simulation object so it is ready to run*/
    virtual void simInitialize();
    /**
     * Run simulation to completion
     @return the final time of the simulation
     */
    virtual coreTime Run();

    /**
    * Run simulation to completion but return immediately
    Query the status and result of the asynchronous call with `getStatus`.
    */
    virtual void RunAsync();

    /**
     * Run simulation up to provided time.   Simulation may
     * return early.
     *
     * @param time maximum time simulation may advance to.
     * @return time simulation successfully advanced to.
     */
    virtual coreTime Step(coreTime time);

    /**
     * Run simulation up to provided time.   Simulation may
     * return early.
     *
     * @param time maximum time simulation may advance to.
     */
    virtual void StepAsync(coreTime time);

    /** get the current execution status of the simulation
    @param[out] timeReturn the current simulation time
    @return GRIDDYN_PENDING if an asynchronous operation is ongoing otherwise returns the current
    state of the simulation*/
    virtual int getStatus(coreTime& timeReturn);
    /**
     * Get the next GridDyn Event time
     *
     * @return the next event time
     */
    coreTime getNextEvent() const;

    virtual void Finalize();
    virtual int Reset();
    virtual int Reset(readerInfo& readerInformation);
    /** reset the underlying simulation of a runner*/
    void resetSim(std::shared_ptr<gridDynSimulation> sim) { m_gds = std::move(sim); }
    /** get a pointer to the simulation object*/
    std::shared_ptr<const gridDynSimulation> getSim() const { return m_gds; }

    std::shared_ptr<gridDynSimulation>& getSim();
    /** check if the runner is ready for another command */
    virtual bool isReady() const;

  protected:
    /**
     * Stop any active recording associated with the runner.
     */
    void StopRecording(void);

    std::shared_ptr<gridDynSimulation> m_gds;

    decltype(std::chrono::high_resolution_clock::now()) m_startTime;
    decltype(std::chrono::high_resolution_clock::now()) m_stopTime;
    bool mEventMode = false;

    virtual std::shared_ptr<CLI::App>
        generateLocalCommandLineParser(readerInfo& readerInformation);

    std::shared_ptr<CLI::App> generateBaseCommandLineParser(readerInfo& readerInformation);

    /** actually load the command line arguments*/
    int loadCommandArgument(readerInfo& readerInformation, bool allowUnrecognized);

  protected:
    std::string mExecutablePath;  //!< the executable path from command line arguments
  private:
    std::future<coreTime> mAsyncReturn;  //!< future code for the asynchronous operations
    int mArgcValue{0};
    char** mArgValues = nullptr;
    std::string mArgumentString;
};

}  // namespace griddyn
