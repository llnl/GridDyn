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

#include "zmqContextManager.h"

#include "../cppzmq/zmq.hpp"
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace zmqlib {
/** a storage system for the available core objects allowing references by name to the core
 */
std::map<std::string, std::shared_ptr<ZmqContextManager>> ZmqContextManager::contexts;

/** we expect operations on core object that modify the map to be rare but we absolutely need them
to be thread safe so we are going to use a lock that is entirely controlled by this file*/
static std::mutex contextLock;

std::shared_ptr<ZmqContextManager>
    ZmqContextManager::getContextPointer(const std::string& contextName)
{
    const std::scoped_lock contextGuard(
        contextLock);  // just to ensure that nothing funny happens if you try to
    // get a context while it is being constructed
    auto fnd = contexts.find(contextName);
    if (fnd != contexts.end()) {
        return fnd->second;
    }

    auto newContext = std::shared_ptr<ZmqContextManager>(new ZmqContextManager(contextName));
    contexts.emplace(contextName, newContext);
    return newContext;
    // if it doesn't make a new one with the appropriate name
}

zmq::context_t& ZmqContextManager::getContext(const std::string& contextName)
{
    return getContextPointer(contextName)->getBaseContext();
}

void ZmqContextManager::closeContext(const std::string& contextName)
{
    const std::scoped_lock contextGuard(contextLock);
    auto fnd = contexts.find(contextName);
    if (fnd != contexts.end()) {
        contexts.erase(fnd);
    }
}

bool ZmqContextManager::setContextToLeakOnDelete(const std::string& contextName)
{
    const std::scoped_lock contextGuard(contextLock);
    auto fnd = contexts.find(contextName);
    if (fnd != contexts.end()) {
        fnd->second->leakOnDelete = true;
    }
    return false;
}
ZmqContextManager::~ZmqContextManager()
{
    if (leakOnDelete) {
        // yes I am purposefully leaking this PHILIP TOP
        auto* leakedContext = zcontext.release();
        (void)leakedContext;
    }
}

ZmqContextManager::ZmqContextManager(const std::string& contextName): name(contextName)
{
    zcontext = std::make_unique<zmq::context_t>();
}

}  // namespace zmqlib
