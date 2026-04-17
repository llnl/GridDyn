/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcpInterface.h"

#include "core/factoryTemplates.hpp"
#include "core/objectFactory.hpp"
#include "tcpCollector.h"
#include "tcpCommunicator.h"
#include <string>

namespace griddyn {

static childClassFactory<tcpLib::tcpCollector, collector> tcpcol(std::vector<std::string>{"tcp"});

static childClassFactory<tcpLib::tcpCommunicator, Communicator>
    tcpComm(std::vector<std::string>{"tcp"});

void loadTcpLibrary()
{
    static int loaded = 0;

    if (loaded == 0) {
        loaded = 1;
    }
}

}  // namespace griddyn
