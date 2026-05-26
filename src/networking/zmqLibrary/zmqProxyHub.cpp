/*
Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle
Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for
Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence
Livermore National Security, LLC.
*/
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zmqProxyHub.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace zmqlib {
std::vector<std::shared_ptr<ZmqProxyHub>> ZmqProxyHub::proxies;

static std::mutex proxyCreationMutex;

std::shared_ptr<ZmqProxyHub> ZmqProxyHub::getProxy(const std::string& proxyName,
                                                   const std::string& pairType,
                                                   const std::string& contextName)
{
    const std::scoped_lock proxyLock(proxyCreationMutex);
    for (auto& rct : proxies) {
        if (rct->getName() == proxyName) {
            return rct;
        }
    }
    // if it doesn't find a matching name make a new one with the appropriate name
    auto newProxy = std::shared_ptr<ZmqProxyHub>(new ZmqProxyHub(proxyName, pairType, contextName));
    proxies.push_back(newProxy);
    return newProxy;
}

ZmqProxyHub::~ZmqProxyHub()
{
    try {
        stopProxy();
    }
    catch (const std::exception& ex) {
        std::cerr << "ZmqProxyHub destructor suppressed exception: " << ex.what() << '\n';
    }
    catch (...) {
        std::cerr << "ZmqProxyHub destructor suppressed unknown exception\n";
    }
}

void ZmqProxyHub::startProxy()
{
    proxyThread = std::thread(&ZmqProxyHub::proxyLoop, this);
    int active = 0;
    controllerSocket->recv(zmq::mutable_buffer(&active, 4));
    if (active == 1) {
        proxyRunning = true;
    }
}

void ZmqProxyHub::stopProxy()
{
    if (proxyRunning) {
        controllerSocket->send(zmq::const_buffer("TERMINATE", 9));
        proxyThread.join();
    }
    const std::scoped_lock proxyLock(proxyCreationMutex);
    for (auto px = proxies.begin(); px != proxies.end(); ++px) {
        if ((*px)->name == name) {
            proxies.erase(px);
            break;
        }
    }
}

void ZmqProxyHub::modifyIncomingConnection(SocketOperation operation, const std::string& connection)
{
    incoming.addOperation(operation, connection);
}
void ZmqProxyHub::modifyOutgoingConnection(SocketOperation operation, const std::string& connection)
{
    outgoing.addOperation(operation, connection);
}

ZmqProxyHub::ZmqProxyHub(const std::string& proxyName,
                         const std::string& pairType,
                         const std::string& context): name(proxyName)
{
    contextManager = ZmqContextManager::getContextPointer(context);
    if ((pairType == "pubsub") || (pairType == "sub") || (pairType == "pub")) {
        incoming.type = zmq::socket_type::xsub;
        outgoing.type = zmq::socket_type::xpub;
    } else if ((pairType == "router") || (pairType == "dealer")) {
        incoming.type = zmq::socket_type::router;
        outgoing.type = zmq::socket_type::dealer;
    } else if ((pairType == "pull") || (pairType == "push") || (pairType == "pushpull")) {
        incoming.type = zmq::socket_type::pull;
        outgoing.type = zmq::socket_type::push;
    }
    controllerSocket =
        std::make_unique<zmq::socket_t>(ZmqContextManager::getContext(contextManager->getName()),
                                        zmq::socket_type::pair);
    controllerSocket->bind(std::string("inproc://proxy_" + name).c_str());
}

void ZmqProxyHub::proxyLoop()
{
    zmq::socket_t inputSocket =
        incoming.makeSocket(ZmqContextManager::getContext(contextManager->getName()));
    zmq::socket_t outputSocket =
        outgoing.makeSocket(ZmqContextManager::getContext(contextManager->getName()));

    zmq::socket_t control(ZmqContextManager::getContext(contextManager->getName()),
                          zmq::socket_type::pair);

    control.connect(std::string("inproc://proxy_" + name).c_str());

    int active = 1;
    control.send(zmq::const_buffer(&active, sizeof(int)));

    zmq::proxy_steerable(inputSocket, outputSocket, zmq::socket_ref(nullptr), control);

    // if we exit the proxy function we are no longer running
    proxyRunning = false;
}

}  // namespace zmqlib
