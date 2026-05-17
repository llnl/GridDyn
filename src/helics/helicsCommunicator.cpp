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

HelicsCommunicator::HelicsCommunicator(const std::string& name): Communicator(name) {}

HelicsCommunicator::HelicsCommunicator(const std::string& name, std::uint64_t identifier):
    Communicator(name, identifier)
{
}

void HelicsCommunicator::set(std::string_view param, std::string_view val)
{
    if (param == "federate") {
        coordName = val;
    } else if (param == "target") {
        target = val;
    } else {
        Communicator::set(param, val);
    }
}

void HelicsCommunicator::set(std::string_view param, double val)
{
    Communicator::set(param, val);
}

void HelicsCommunicator::initialize()
{
    coord = HelicsCoordinator::findCoordinator(coordName);
    if (coord == nullptr) {
        auto obj = gridDynSimulation::getInstance()->find("helics");
        coord = dynamic_cast<HelicsCoordinator*>(obj);
    }
    if (coord == nullptr) {
        throw(griddyn::executionFailure(nullptr, "unable to connect with HELICS coordinator"));
    }
    index = coord->addEndpoint(getName(), std::string(), target);
}

void HelicsCommunicator::disconnect() {}

void HelicsCommunicator::transmit(const std::string& destName,
                                  std::shared_ptr<griddyn::commMessage> message)
{
    auto mdata = message->to_string();
    if (destName.empty()) {
        coord->sendMessage(index, mdata.data(), static_cast<count_t>(mdata.size()));
    } else {
        coord->sendMessage(index, destName, mdata.data(), static_cast<count_t>(mdata.size()));
    }
}

void HelicsCommunicator::transmit(std::uint64_t /*destID*/,
                                  std::shared_ptr<griddyn::commMessage> message)
{
    auto mdata = message->to_string();
    coord->sendMessage(index, mdata.data(), static_cast<count_t>(mdata.size()));
}

}  // namespace griddyn::helicsLib
