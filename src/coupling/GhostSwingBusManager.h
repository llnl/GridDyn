/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GhostSwingBusMessageTypes.h"
#include "griddyn/griddyn-config.h"

#ifndef GRIDDYN_ENABLE_MPI
#    include <functional>
#endif

#include <complex>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
namespace mpi {
    class MpiService;
}

class MPIRequests;

class GhostSwingBusManager {
  public:
    /**
     * class destructor must be public so it can be used with shared_ptr
     */
    ~GhostSwingBusManager();  // destructor

    using cvec = std::vector<std::complex<double>>;

    /**
     * Create a new instance of the GhostSwingBusManager.  This must be
     * called at the program launch with the command line arguments
     * since this method invoke MPI_Init.
     *
     * Note that argc and argv may NOT be the same on return, just as in
     * MPI_Init.
     */
    static std::shared_ptr<GhostSwingBusManager>
        initialize(int* argc, char** argv[]);  // only constructor that creates instance

    /**
     * Return the current instance of the singleton.
     *
     * This will fail if initialize has not been called.
     */
    static std::shared_ptr<GhostSwingBusManager> instance();

    /**
     * Returns true if instance of the manager exists.
     */
    static bool isInstance();

    /**
     * Creates a new GridLab-D instance using
     * string sent as the command line to launch
     * the GridLAB-D instance.
     *
     * Returns taskId for the new instance.
     */
    int createGridlabDInstance(std::string_view arguments);

    /**
     * End the simulation.
     *
     * Must be called at the end of the simulation to ensure that
     * tasks launched by the manager are correctly ended.
     */
    void endSimulation();

    /**
     * Advances time in distribution model by deltaTime with the
     * provided.N voltages on the swing bus.
     * This will asynchronously execute the distribution model.
     */
    void sendVoltageStep(int taskId, cvec& voltage, unsigned int deltaTime);

    /**
     * Returns the computed currents from the distribution system for
     * deltaTime and voltages provided by the sendVoltageStep method
     * call.
     *
     * This is currently a blocking operation.  Likely will want to
     * have receiving of currents be processed in a non-blocking
     * way.
     */
    void getCurrent(int taskId, cvec& current);

    /**
     * Returns number of tasks.
     */
    int getNumTasks() const { return m_numTasks; }

    static void SetDebug(bool debug) { g_printStuff = debug; }

#ifndef GRIDDYN_ENABLE_MPI
    /**
     * sets the dummy load function
     */
    void setDummyLoadFunction(int taskId,
                              std::function<void(VoltageMessage* vm, CurrentMessage* cm)> dfunc)
    {
        dummy_load_eval[taskId] = dfunc;
    }
#endif

  private:
    static bool g_printStuff;  //!< public boolean to change whether things are printed or not
#ifdef GRIDDYN_ENABLE_MPI
    mpi::MpiService* servicer;  //!< pointer to the global MpiService
#endif
    /**
     * Singleton so prevent external construction and copying of this
     * class.
     */
    GhostSwingBusManager() {}

    /*
     * Private so cannot be called
     */
    GhostSwingBusManager(int* argc, char** argv[]);

    /**
     * End the distribution system model.
     *
     * Should be called for each distribution model.
     */
    void sendStopMessage(int taskId);

    /**
     * Singleton instance.
     */
    static std::shared_ptr<GhostSwingBusManager> m_pInstance;

    int m_numTasks = 20;
#ifdef GRIDDYN_ENABLE_MPI
    int m_taskId = 0;
    /*
     * Async send requests
     would have preferred unique ptr but compilers complain about not knowing the definition in the
     destructor
     */
    MPIRequests* requests{nullptr};

    /*
     * True if initialization send has completed.
     */
    std::vector<bool> m_initializeCompleted;
    std::vector<std::string> m_modelSpecificationMessages;

#else
    /**
     * a function call for a dummy load to execute on a windows system
     */
    std::vector<std::function<void(VoltageMessage* vm, CurrentMessage* cm)>> dummy_load_eval;

#endif

    /**
     * Next taskID to assign, keeps track of the
     * number of distribution models that have
     * been created.
     */
    int m_nextTaskId = 1;

    /**
     * Message buffer space for asynchronous MPI calls.
     */
    std::vector<VoltageMessage> m_voltSendMessage;
    std::vector<CurrentMessage> m_currReceiveMessage;
};

}  // namespace griddyn
