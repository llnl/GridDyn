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
DimeCommunicator::DimeCommunicator() = default;

DimeCommunicator::DimeCommunicator(const std::string& name): ZmqCommunicator(name) {}

DimeCommunicator::DimeCommunicator(const std::string& name, std::uint64_t id):
    ZmqCommunicator(name, id)
{
}

DimeCommunicator::DimeCommunicator(std::uint64_t id): ZmqCommunicator(id) {}

std::unique_ptr<Communicator> DimeCommunicator::clone() const
{
    std::unique_ptr<Communicator> col = std::make_unique<DimeCommunicator>();
    DimeCommunicator::cloneTo(col.get());
    return col;
}

void DimeCommunicator::cloneTo(Communicator* comm) const
{
    ZmqCommunicator::cloneTo(comm);
    auto dc = dynamic_cast<DimeCommunicator*>(comm);
    if (dc == nullptr) {
        return;
    }
}

void DimeCommunicator::messageHandler(const zmq::multipart_t& /*msg */) {}

void DimeCommunicator::addHeader(zmq::multipart_t& msg,
                                 const std::shared_ptr<commMessage>& /* message */)
{
}

void DimeCommunicator::addMessageBody(zmq::multipart_t& /* msg */,
                                      const std::shared_ptr<commMessage>& /* message */)
{
}

void DimeCommunicator::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        ZmqCommunicator::set(param, val);
    }
}

void DimeCommunicator::set(std::string_view param, double val)
{
    ZmqCommunicator::set(param, val);
}

void DimeCommunicator::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        ZmqCommunicator::setFlag(flag, val);
    }
}

}  // namespace griddyn::dimeLib
