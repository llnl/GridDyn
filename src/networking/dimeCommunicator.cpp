/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dimeCommunicator.h"

#include "cppzmq/zmq_addon.hpp"
#include "zmqLibrary/zmqHelper.h"
#include <memory>
#include <string>

namespace griddyn::dimeLib {
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

void dimeCommunicator::addHeader(zmq::multipart_t& msg, std::shared_ptr<commMessage>& /* message */)
{
}

void dimeCommunicator::addMessageBody(zmq::multipart_t& /* msg */,
                                      std::shared_ptr<commMessage>& /* message */)
{
}

void dimeCommunicator::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        zmqCommunicator::set(param, val);
    }
}

void dimeCommunicator::set(std::string_view param, double val)
{
    zmqCommunicator::set(param, val);
}

void dimeCommunicator::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        zmqCommunicator::setFlag(flag, val);
    }
}

}  // namespace griddyn::dimeLib
