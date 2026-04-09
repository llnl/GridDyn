/*
Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was modified by Pacific Northwest National Laboratory, operated by the Battelle
Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for
Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence
Livermore National Security, LLC.
*/
/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "zmqHelper.h"

#include "../cppzmq/zmq.hpp"
#include <algorithm>
#include <cctype>
#include <map>
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
static const std::map<std::string, zmq::socket_type> socketMap{{"req", zmq::socket_type::req},
                                                               {"request", zmq::socket_type::req},
                                                               {"rep", zmq::socket_type::rep},
                                                               {"reply", zmq::socket_type::rep},
                                                               {"dealer", zmq::socket_type::dealer},
                                                               {"router", zmq::socket_type::router},
                                                               {"pub", zmq::socket_type::pub},
                                                               {"publish", zmq::socket_type::pub},
                                                               {"sub", zmq::socket_type::sub},
                                                               {"subscribe", zmq::socket_type::sub},
                                                               {"xpub", zmq::socket_type::xpub},
                                                               {"xsub", zmq::socket_type::xsub},
                                                               {"push", zmq::socket_type::push},
                                                               {"pull", zmq::socket_type::pull},
                                                               {"pair", zmq::socket_type::pair},
                                                               {"stream", zmq::socket_type::stream}};
/* *INDENT-ON* */

zmq::socket_type socketTypeFromString(const std::string& socketType)
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
    return zmq::socket_type::req;
}

}  // namespace zmqlib
