/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcpInterface.h"

#include "core/FactoryTemplates.hpp"
#include "core/ObjectFactory.hpp"
#include "tcpCollector.h"
#include "tcpCommunicator.h"
#include <string>
#include <vector>

namespace griddyn {
void loadTcpLibrary()
{
    static const bool loaded = []() {
        static const ChildClassFactory<tcpLib::TcpCollector, collector> tcpCollectorFactory(
            std::vector<std::string>{"tcp"});
        static const ChildClassFactory<tcpLib::TcpCommunicator, Communicator>
            tcpCommunicatorFactory(std::vector<std::string>{"tcp"});
        return true;
    }();
    (void)loaded;
}

}  // namespace griddyn
