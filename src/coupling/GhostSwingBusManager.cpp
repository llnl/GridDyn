/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GhostSwingBusManager.h"
#ifdef GRIDDYN_ENABLE_MPI
#    include "MpiService.h"
#    include <mpi.h>
#endif
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace griddyn {

#ifdef GRIDDYN_ENABLE_MPI
class MpiRequests {
  public:
    std::vector<MPI_Request> m_mpiSendRequests;
    std::vector<MPI_Request> m_mpiRecvRequests;
};
#endif

bool GhostSwingBusManager::g_printStuff = false;

// Global pointer to ensure single instance of class
std::shared_ptr<GhostSwingBusManager> GhostSwingBusManager::m_pInstance = nullptr;

// This constructor requires instance to exist
std::shared_ptr<GhostSwingBusManager> GhostSwingBusManager::instance()
{
    if (!m_pInstance) {
#ifdef GRIDDYN_ENABLE_MPI
        throw(std::runtime_error("GhostSwingBusManager instance does not exist!"));
#else
        // mainly for convenience on a windows system for testing purposes the GhostSwingBus doesn't
        // do anything without MPI
        m_pInstance =
            std::shared_ptr<GhostSwingBusManager>(new GhostSwingBusManager(nullptr, nullptr));
#endif
    }
    return m_pInstance;
}

// create instance if doesn't exist...requires argc, argv for MPI
std::shared_ptr<GhostSwingBusManager> GhostSwingBusManager::initialize(int* argc, char** argv[])
{
    if (!m_pInstance) {
        m_pInstance = std::shared_ptr<GhostSwingBusManager>(new GhostSwingBusManager(argc, argv));
    }
    return m_pInstance;
}

bool GhostSwingBusManager::isInstance()
{
    return static_cast<bool>(m_pInstance);
}

// NOLINTNEXTLINE(readability-non-const-parameter)
GhostSwingBusManager::GhostSwingBusManager(int* argc, char** argv[])
{
#ifdef GRIDDYN_ENABLE_MPI
    servicer = mpi::MpiService::instance(argc, argv);

    m_numTasks = servicer->getCommSize();

    m_initializeCompleted.resize(m_numTasks);
    m_modelSpecificationMessages.resize(m_numTasks);
    requests = new MpiRequests;
    requests->m_mpiSendRequests.resize(m_numTasks);
    requests->m_mpiRecvRequests.resize(m_numTasks);
#else
    (void)argc;
    (void)argv;
    dummy_load_eval.resize(m_numTasks);
#endif

    m_voltSendMessage.resize(m_numTasks);
    m_currReceiveMessage.resize(m_numTasks);
}

GhostSwingBusManager::~GhostSwingBusManager()
{
#ifdef GRIDDYN_ENABLE_MPI
    delete requests;
#endif
}

// for Transmission
int GhostSwingBusManager::createGridlabDInstance(std::string_view arguments)
{
    assert(arguments.size() <=
           PATH_MAX * 4);  // there is a bug in Visual studio where the sizeof doesn't
    // compile

    const int taskId = m_nextTaskId++;
    if (g_printStuff) {
        std::cout << "Task: " << taskId << " creating new GridLAB-D instance with args "
                  << arguments << '\n';
    }

#ifdef GRIDDYN_ENABLE_MPI
    m_modelSpecificationMessages[taskId] = arguments;
    auto* arguments_c = m_modelSpecificationMessages[taskId].data();
    m_initializeCompleted[taskId] = false;
    auto token = servicer->getToken();
    MPI_Isend(arguments_c,
              static_cast<int>(m_modelSpecificationMessages[taskId].size()),
              MPI_CHAR,
              taskId,
              static_cast<int>(MessageTags::MODEL_SPEC),
              MPI_COMM_WORLD,
              &requests->m_mpiSendRequests[taskId]);

#else
    if (taskId >= m_numTasks) {
        m_numTasks = m_numTasks + 50;
        dummy_load_eval.resize(m_numTasks);
        m_voltSendMessage.resize(m_numTasks);
        m_currReceiveMessage.resize(m_numTasks);
    }
#endif

    return taskId;
}

// Transmission
void GhostSwingBusManager::sendVoltageStep(int taskId, cvec& voltage, unsigned int deltaTime)
{
    // populate message structure
    if (g_printStuff) {
        std::cout << "taskId: " << taskId << " voltage[0]: " << '\n'
                  << "** LEB: vector size = " << voltage.size() << '\n';
    }

    // calculate number of ThreePhaseVoltages in voltage
    const int numThreePhaseVoltage = static_cast<int>(voltage.size()) / 3;

    if (g_printStuff) {
        std::cout << "Sending voltage message deltaTime = " << deltaTime << " to task " << taskId
                  << '\n';
    }

    for (int ii = 0; ii < numThreePhaseVoltage; ++ii) {
        for (int jj = 0; jj < 3; ++jj) {
            if (g_printStuff) {
                std::cout << "\tvoltage[" << ii << "][" << jj << "] = " << voltage[(ii * 3) + jj]
                          << " abs = " << std::abs(voltage[(ii * 3) + jj])
                          << " arg = " << std::arg(voltage[(ii * 3) + jj]) << '\n';
            }
            m_voltSendMessage[taskId].voltage[ii].real[jj] = voltage[(ii * 3) + jj].real();
            m_voltSendMessage[taskId].voltage[ii].imag[jj] = voltage[(ii * 3) + jj].imag();
        }
    }
    m_voltSendMessage[taskId].numThreePhaseVoltage = numThreePhaseVoltage;
    m_voltSendMessage[taskId].deltaTime = deltaTime;

#ifdef GRIDDYN_ENABLE_MPI
    auto token = servicer->getToken();
    if (!m_initializeCompleted[taskId]) {
        MPI_Status status;

        // SGS TODO add timer for this.

        // Make sure async initialize Send has completed.
        MPI_Wait(&requests->m_mpiSendRequests[taskId], &status);

        m_initializeCompleted[taskId] = true;
    }

    MPI_Isend(&m_voltSendMessage[taskId],
              sizeof(VoltageMessage),
              MPI_BYTE,
              taskId,
              static_cast<int>(MessageTags::VOLTAGE_STEP),
              MPI_COMM_WORLD,
              &requests->m_mpiSendRequests[taskId]);

    MPI_Irecv(&m_currReceiveMessage[taskId],
              sizeof(CurrentMessage),
              MPI_BYTE,
              taskId,
              static_cast<int>(MessageTags::CURRENT),
              MPI_COMM_WORLD,
              &requests->m_mpiRecvRequests[taskId]);

#else

    if (dummy_load_eval[taskId]) {
        dummy_load_eval[taskId](&(m_voltSendMessage[taskId]), &(m_currReceiveMessage[taskId]));
    }

#endif
}
// NOLINTNEXTLINE
void GhostSwingBusManager::sendStopMessage(int taskId)
{
    if (g_printStuff) {
        std::cout << "Sending STOP message to Distribution task " << taskId << '\n';
    }
#ifdef GRIDDYN_ENABLE_MPI
    // Blocking send to gridlabd task
    auto token = servicer->getToken();
    MPI_Send(&m_voltSendMessage[taskId],
             1,
             MPI_BYTE,
             taskId,
             static_cast<int>(MessageTags::STOP),
             MPI_COMM_WORLD);
#endif
}

// Transmission
void GhostSwingBusManager::getCurrent(int taskId, cvec& current)
{
    if (g_printStuff) {
        std::cout << "Transmission *waiting* to get current from Task: " << taskId << '\n';
    }

#ifdef GRIDDYN_ENABLE_MPI
    {
        MPI_Status status;
        auto token = servicer->getToken();
        // Make sure async Send has completed.
        MPI_Wait(&requests->m_mpiSendRequests[taskId], &status);

        // Make sure async Recv has completed.
        MPI_Wait(&requests->m_mpiRecvRequests[taskId], &status);
    }
#endif

    const int numThreePhaseCurrent = m_currReceiveMessage[taskId].numThreePhaseCurrent;
    if (g_printStuff) {
        std::cout << "Current received from Task:" << taskId
                  << ", numThreePhaseCurrent = " << numThreePhaseCurrent << '\n';
    }
    const std::size_t currentSize =
        (numThreePhaseCurrent > 0) ? static_cast<std::size_t>(numThreePhaseCurrent) * 3U : 0U;
    current.resize(currentSize);  // resize vector to number of three phase currents received.
    for (int ii = 0; ii < numThreePhaseCurrent; ++ii) {
        for (int jj = 0; jj < 3; ++jj) {
            current[(ii * 3) + jj].real(m_currReceiveMessage[taskId].current[ii].real[jj]);
            current[(ii * 3) + jj].imag(m_currReceiveMessage[taskId].current[ii].imag[jj]);
            if (g_printStuff) {
                std::cout << "\tcurrReceiveMessage, current[" << (ii * 3) + jj
                          << "] = " << current[(ii * 3) + jj]
                          << " abs = " << std::abs(current[(ii * 3) + jj])
                          << " arg = " << std::arg(current[(ii * 3) + jj]) << '\n';
            }
        }
    }
}

// must cleanup MPI
void GhostSwingBusManager::endSimulation()
{
    for (int taskIndex = 1; taskIndex < getNumTasks(); ++taskIndex) {
        sendStopMessage(taskIndex);
    }

#ifdef GRIDDYN_ENABLE_MPI

    if (g_printStuff) {
        std::cout << "end task : " << m_taskId << '\n';
    }
#endif
    // clear the shared_ptr, the object will probably get deleted at this point and will be unable
    // to be called
    m_pInstance.reset();
}

}  // namespace griddyn
