/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "networkingInterface.h"

#include "griddyn/griddyn-config.h"

#ifdef GRIDDYN_ENABLE_ZMQ
#    include "zmqInterface.h"
#endif
#ifdef GRIDDYN_ENABLE_TCP
#    include "tcpInterface.h"
#endif

namespace griddyn {
void loadNetworkingLibrary()
{
#ifdef GRIDDYN_ENABLE_ZMQ
    loadZMQLibrary();
#endif

#ifdef GRIDDYN_ENABLE_TCP
    loadTcpLibrary();
#endif
}

}  // namespace griddyn
