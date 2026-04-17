/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fskitCommunicator.h"

#include "gridDynFederatedScheduler.h"
#include "griddyn/comms/commMessage.h"
#include "griddyn/comms/controlMessage.h"
#include "griddyn/comms/relayMessage.h"
#include "griddyn/events/Event.h"
#include "griddyn/events/eventQueue.h"
#include "griddyn/gridDynSimulation.h"  // for gridDynSimulation
#include "griddyn/griddyn-config.h"
#include <fskit/granted-time-window-scheduler.h>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#define GRIDDYN_RANK 0
#define NS3_RANK 1
#define NS3_SIMULATOR_NAME "ns3"

// Why is default ctor needed?
FskitCommunicator::FskitCommunicator():
    LogicalProcess(fskit::GlobalLogicalProcessId(fskit::FederatedSimulatorId("RAII-is-broken"),
                                                 GRIDDYN_RANK,
                                                 fskit::LocalLogicalProcessId("RAII-is-broken")))
{
}

FskitCommunicator::FskitCommunicator(std::string id):
    Communicator(id),
    LogicalProcess(fskit::GlobalLogicalProcessId(fskit::FederatedSimulatorId("gridDyn"),
                                                 GRIDDYN_RANK,
                                                 fskit::LocalLogicalProcessId(id)))
{
    assert(GriddynFederatedScheduler::IsFederated());
}

FskitCommunicator::FskitCommunicator(std::string name, std::uint64_t id):
    Communicator(name, id),
    LogicalProcess(fskit::GlobalLogicalProcessId(fskit::FederatedSimulatorId("gridDyn"),
                                                 GRIDDYN_RANK,
                                                 fskit::LocalLogicalProcessId(name)))
{
    assert(GriddynFederatedScheduler::IsFederated());
}

void FskitCommunicator::initialize()
{
    assert(GriddynFederatedScheduler::GetScheduler() != 0);
    // XXX: This assumes that clients are using this class instance
    // as a shared_ptr.
    GriddynFederatedScheduler::GetScheduler()->RegisterLogicalProcess(
        std::static_pointer_cast<FskitCommunicator>(Communicator::shared_from_this()));
}

void FskitCommunicator::disconnect() {}

void FskitCommunicator::ProcessEventMessage(const fskit::EventMessage& eventMessage)
{
    auto gds = griddyn::gridDynSimulation::getInstance();

    // Convert fskit time (ns) to Griddyn time (s)
    double griddynTime = eventMessage.GetTime().GetRaw() * 1.0E-9;

    std::string payload;
    eventMessage.Unpack(payload);

    std::shared_ptr<griddyn::commMessage> message;
    message->from_datastring(payload);

    std::string name = getName();

    // using lambda capture to move the message to the lambda
    // unique ptr capture with message{std::move(m)} failed on gcc 4.9.3; build shared and capture
    // the shared ptr.
    auto event = std::make_unique<griddyn::functionEventAdapter>([this, message]() {
        receive(0, getName(), message);
        return griddyn::change_code::no_change;
    });
    event->m_nextTime = griddynTime;
    gds->add(std::shared_ptr<griddyn::eventAdapter>(std::move(event)));
}

void FskitCommunicator::transmit(const std::string& /*destName*/,
                                 std::shared_ptr<griddyn::commMessage> message)
{
    doTransmit(message);
}

void FskitCommunicator::transmit(std::uint64_t /*destID*/,
                                 std::shared_ptr<griddyn::commMessage> message)
{
    doTransmit(message);
}

void FskitCommunicator::doTransmit(std::shared_ptr<griddyn::commMessage> message)
{
    std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler(
        GriddynFederatedScheduler::GetScheduler());

    griddyn::gridDynSimulation* inst = griddyn::gridDynSimulation::getInstance();
    fskit::Time currentTime;
    if (inst != 0) {
        double currentTimeSeconds = inst->getSimulationTime();
        currentTime = fskit::Time(currentTimeSeconds * 1e9);  // scale current time to nanoseconds
    } else {
        currentTime = scheduler->Next();  // Incorrect.
    }

    // XXX: The time increment needs to be retrieved from somewhere. Griddyn XML file?
    fskit::Time increment(1e7);  // 1/100 of a second, in nanoseconds
                                 // Think about what this delay is representing: microprocessor data
                                 // transfer to comm layer, etc.

    auto msg = message->to_datastring();

    if (msg.size() > 0) {
        scheduler->SendEventMessage(
            fskit::GlobalLogicalProcessId(fskit::FederatedSimulatorId(NS3_SIMULATOR_NAME),
                                          NS3_RANK,
                                          fskit::LocalLogicalProcessId(getName())),
            currentTime + increment,
            message->getMessageType(),
            msg);
    }
}
