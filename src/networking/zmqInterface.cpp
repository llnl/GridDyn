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
void loadZMQLibrary()
{
    static const bool loaded = []() {
        static childClassFactory<zmqInterface::ZmqCommunicator, Communicator> zmqCommunicatorFactory(
            std::vector<std::string>{"zmq"});
        return true;
    }();
    (void)loaded;
}

}  // namespace griddyn
