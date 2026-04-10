/*
 * LLNS Copyright Start
 * Copyright (c) 2017, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "dimeCommunicator.h"

#include "cppzmq/zmq_addon.hpp"
#include "zmqLibrary/zmqHelper.h"
#include <memory>
#include <string>

namespace griddyn {
namespace dimeLib {
    dimeCommunicator::dimeCommunicator() = default;

    dimeCommunicator::dimeCommunicator(const std::string& name): zmqCommunicator(name) {}

    dimeCommunicator::dimeCommunicator(const std::string& name, std::uint64_t id):
        zmqCommunicator(name, id)
    {
    }

    dimeCommunicator::dimeCommunicator(std::uint64_t id): zmqCommunicator(id) {}

    std::unique_ptr<Communicator> dimeCommunicator::clone() const
    {
        std::unique_ptr<Communicator> col = std::make_unique<dimeCommunicator>();
        dimeCommunicator::cloneTo(col.get());
        return col;
    }

    void dimeCommunicator::cloneTo(Communicator* comm) const
    {
        zmqCommunicator::cloneTo(comm);
        auto dc = dynamic_cast<dimeCommunicator*>(comm);
        if (dc == nullptr) {
            return;
        }
    }

    void dimeCommunicator::messageHandler(const zmq::multipart_t& /*msg */) {}

    void dimeCommunicator::addHeader(zmq::multipart_t& msg,
                                     std::shared_ptr<commMessage>& /* message */)
    {
    }

    void dimeCommunicator::addMessageBody(zmq::multipart_t& /* msg */,
                                          std::shared_ptr<commMessage>& /* message */)
    {
    }

    void dimeCommunicator::set(const std::string& param, const std::string& val)
    {
        if (param.empty()) {
        } else {
            zmqCommunicator::set(param, val);
        }
    }

    void dimeCommunicator::set(const std::string& param, double val)
    {
        zmqCommunicator::set(param, val);
    }

    void dimeCommunicator::setFlag(const std::string& flag, bool val)
    {
        if (flag.empty()) {
        } else {
            zmqCommunicator::setFlag(flag, val);
        }
    }

}  // namespace dimeLib
}  // namespace griddyn
