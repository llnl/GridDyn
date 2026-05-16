/*
Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle
Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for
Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence
Livermore National Security, LLC.
*/
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../cppzmq/zmq_addon.hpp"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace zmqlib {
/** enumeration of possible operations on a socket*/
enum class SocketOperation {
    BIND,
    CONNECT,
    UNBIND,
    DISCONNECT,
    SUBSCRIBE,
    UNSUBSCRIBE,
};

typedef std::pair<SocketOperation, std::string>
    SocketOperationEntry;  //!< easy definition for operation instruction

/** data class describing a socket and some operations on it*/
class ZmqSocketDescriptor {
  public:
    std::string name;  //!< name of the socket for later reference
    zmq::socket_type type = zmq::socket_type::sub;  //!< the socket type
    std::vector<SocketOperationEntry> ops;  //!< a list of connections of make through bind
    std::function<void(const zmq::multipart_t& res)> callback;  //!< the message handler
    ZmqSocketDescriptor(const std::string& socketName = ""):
        name(socketName) {}  // purposefully implicit
    ZmqSocketDescriptor(const std::string& socketName, zmq::socket_type stype):
        name(socketName), type(stype)
    {
    }
    inline void addOperation(SocketOperation op, const std::string& desc)
    {
        ops.emplace_back(op, desc);
    }
    zmq::socket_t makeSocket(zmq::context_t& ctx) const;
    std::unique_ptr<zmq::socket_t> makeSocketPtr(zmq::context_t& ctx) const;
    void modifySocket(zmq::socket_t& sock) const;
};

}  // namespace zmqlib
