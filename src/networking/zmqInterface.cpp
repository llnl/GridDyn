/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zmqInterface.h"

#include "core/factoryTemplates.hpp"
#include "core/objectFactory.hpp"
#include "dimeCollector.h"
#include "dimeCommunicator.h"
#include "zmqCommunicator.h"
#include <string>

namespace griddyn {

static childClassFactory<zmqInterface::zmqCommunicator, Communicator>
    zmqComm(std::vector<std::string>{"zmq"});

void loadZMQLibrary()
{
    static int loaded = 0;

    if (loaded == 0) {
        loaded = 1;
    }
}

}  // namespace griddyn
