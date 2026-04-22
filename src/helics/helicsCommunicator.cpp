/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsCommunicator.h"

#include "core/coreExceptions.h"
#include "griddyn/comms/commMessage.h"
#include "griddyn/comms/controlMessage.h"
#include "griddyn/comms/schedulerMessage.h"
#include "griddyn/gridDynSimulation.h"
#include "helicsCoordinator.h"
#include <iostream>
#include <memory>
#include <string>

namespace griddyn::helicsLib {

helicsCommunicator::helicsCommunicator(const std::string& id): Communicator(id) {}

helicsCommunicator::helicsCommunicator(const std::string& name, std::uint64_t id):
    Communicator(name, id)
{
}

void helicsCommunicator::set(std::string_view param, std::string_view val)
{
    if (param == "federate") {
        coordName = val;
    } else if (param == "target") {
        target = val;
    } else {
        Communicator::set(param, val);
    }
}

void helicsCommunicator::set(std::string_view param, double val)
{
    Communicator::set(param, val);
}

void helicsCommunicator::initialize()
{
    coord = helicsCoordinator::findCoordinator(coordName);
    if (coord == nullptr) {
        auto obj = gridDynSimulation::getInstance()->find("helics");
        coord = dynamic_cast<helicsCoordinator*>(obj);
    }
    if (coord == nullptr) {
        throw(griddyn::executionFailure(nullptr, "unable to connect with HELICS coordinator"));
    }
    index = coord->addEndpoint(getName(), std::string(), target);
}

void helicsCommunicator::disconnect() {}

void helicsCommunicator::transmit(const std::string& destName,
                                  std::shared_ptr<griddyn::commMessage> message)
{
    auto mdata = message->to_string();
    if (destName.empty()) {
        coord->sendMessage(index, mdata.data(), static_cast<count_t>(mdata.size()));
    } else {
        coord->sendMessage(index, destName, mdata.data(), static_cast<count_t>(mdata.size()));
    }
}

void helicsCommunicator::transmit(std::uint64_t /*destID*/,
                                  std::shared_ptr<griddyn::commMessage> message)
{
    auto mdata = message->to_string();
    coord->sendMessage(index, mdata.data(), static_cast<count_t>(mdata.size()));
}

}  // namespace griddyn::helicsLib

