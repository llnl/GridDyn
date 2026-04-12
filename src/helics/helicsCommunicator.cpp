/*
 * LLNS Copyright Start
 * Copyright (c) 2016, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
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

namespace griddyn {
namespace helicsLib {

    helicsCommunicator::helicsCommunicator(const std::string& id): Communicator(id) {}

    helicsCommunicator::helicsCommunicator(const std::string& name, std::uint64_t id):
        Communicator(name, id)
    {
    }

    void helicsCommunicator::set(const std::string& param, const std::string& val)
    {
        if (param == "federate") {
            coordName = val;
        } else if (param == "target") {
            target = val;
        } else {
            Communicator::set(param, val);
        }
    }

    void helicsCommunicator::set(const std::string& param, double val)
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

}  // namespace helicsLib
}  // namespace griddyn
