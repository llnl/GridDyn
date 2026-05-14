/*
Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was modified by Pacific Northwest National Laboratory, operated by the Battelle
Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for
Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence
Livermore National Security, LLC.
*/

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zmqSocketDescriptor.h"

#include <memory>

namespace zmqlib {
zmq::socket_t zmqSocketDescriptor::makeSocket(zmq::context_t& ctx) const
{
    zmq::socket_t sock(ctx, type);
    modifySocket(sock);
    return sock;
}

std::unique_ptr<zmq::socket_t> zmqSocketDescriptor::makeSocketPtr(zmq::context_t& ctx) const
{
    auto sock = std::make_unique<zmq::socket_t>(ctx, type);
    modifySocket(*sock);
    return sock;
}

void zmqSocketDescriptor::modifySocket(zmq::socket_t& sock) const
{
    for (const auto& socketOperation : ops) {
        switch (socketOperation.first) {
            case SocketOperation::BIND:
                sock.bind(socketOperation.second);
                break;
            case SocketOperation::UNBIND:
                sock.unbind(socketOperation.second);
                break;
            case SocketOperation::CONNECT:
                sock.connect(socketOperation.second);
                break;
            case SocketOperation::DISCONNECT:
                sock.disconnect(socketOperation.second);
                break;
            case SocketOperation::SUBSCRIBE:
                if ((type == zmq::socket_type::pub) || (type == zmq::socket_type::sub)) {
                    sock.setsockopt(ZMQ_SUBSCRIBE, socketOperation.second);
                }
                break;
            case SocketOperation::UNSUBSCRIBE:
                if ((type == zmq::socket_type::pub) || (type == zmq::socket_type::sub)) {
                    sock.setsockopt(ZMQ_UNSUBSCRIBE, socketOperation.second);
                }
                break;
            default:
                break;
        }
    }
}

}  // namespace zmqlib
