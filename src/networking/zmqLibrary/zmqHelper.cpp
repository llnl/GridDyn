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

#include "zmqHelper.h"

#include "../cppzmq/zmq.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
/*
req = ZMQ_REQ,
rep = ZMQ_REP,
dealer = ZMQ_DEALER,
router = ZMQ_ROUTER,
pub = ZMQ_PUB,
sub = ZMQ_SUB,
xpub = ZMQ_XPUB,
xsub = ZMQ_XSUB,
push = ZMQ_PUSH,
pull = ZMQ_PULL,
#if ZMQ_VERSION_MAJOR < 4
    pair = ZMQ_PAIR
#else
    pair = ZMQ_PAIR,
    stream = ZMQ_STREAM
    */

/* *INDENT-OFF* */
namespace zmqlib {
static const std::map<std::string, zmq::SocketType> socketMap{{"req", zmq::SocketType::req},
                                                               {"request", zmq::SocketType::req},
                                                               {"rep", zmq::SocketType::rep},
                                                               {"reply", zmq::SocketType::rep},
                                                               {"dealer", zmq::SocketType::dealer},
                                                               {"router", zmq::SocketType::router},
                                                               {"pub", zmq::SocketType::pub},
                                                               {"publish", zmq::SocketType::pub},
                                                               {"sub", zmq::SocketType::sub},
                                                               {"subscribe", zmq::SocketType::sub},
                                                               {"xpub", zmq::SocketType::xpub},
                                                               {"xsub", zmq::SocketType::xsub},
                                                               {"push", zmq::SocketType::push},
                                                               {"pull", zmq::SocketType::pull},
                                                               {"pair", zmq::SocketType::pair},
                                                               {"stream",
                                                                zmq::SocketType::stream}};
/* *INDENT-ON* */

zmq::SocketType socketTypeFromString(const std::string& socketType)
{
    auto fnd = socketMap.find(socketType);
    if (fnd != socketMap.end()) {
        return fnd->second;
    }

    /* try making it lower case*/
    std::string lowerCase(socketType);
    std::transform(socketType.cbegin(), socketType.cend(), lowerCase.begin(), ::tolower);
    fnd = socketMap.find(lowerCase);
    if (fnd != socketMap.end()) {
        return fnd->second;
    }
    assert(false);  // NEED to make this a throw operation instead once exceptions are integrated
    return zmq::SocketType::req;
}

}  // namespace zmqlib

