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

#include "zmqReactor.h"

#include "zmqContextManager.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace zmqlib {
std::vector<std::shared_ptr<zmqReactor>> zmqReactor::reactors;

static std::mutex reactorCreationLock;

zmqReactor::zmqReactor(const std::string& reactorName, const std::string& context):
    name(reactorName)
{
    contextManager = zmqContextManager::getContextPointer(context);
    notifier =
        std::make_unique<zmq::socket_t>(zmqContextManager::getContext(contextManager->getName()),
                                        zmq::socket_type::pair);
    const std::string constring = "inproc://reactor_" + name;
    notifier->bind(constring.c_str());

    loopThread = std::thread(&zmqReactor::reactorLoop, this);
}

static const int zero(0);

zmqReactor::~zmqReactor()
{
    try {
        terminateReactor();
    }
    catch (const std::exception& ex) {
        std::cerr << "zmqReactor destructor suppressed exception: " << ex.what() << '\n';
    }
    catch (...) {
        std::cerr << "zmqReactor destructor suppressed unknown exception\n";
    }
}

void zmqReactor::terminateReactor()
{
    if (reactorLoopRunning) {
        updates.emplace(ReactorInstruction::TERMINATE, std::string());
        notifier->send(zmq::const_buffer(&zero, sizeof(int)));
        loopThread.join();
    }
    const std::scoped_lock creationLock(reactorCreationLock);
    for (auto rct = reactors.begin(); rct != reactors.end(); ++rct) {
        if ((*rct)->name == name) {
            reactors.erase(rct);
            break;
        }
    }
}

std::shared_ptr<zmqReactor> zmqReactor::getReactorInstance(const std::string& reactorName,
                                                           const std::string& contextName)
{
    const std::scoped_lock creationLock(reactorCreationLock);
    for (auto& rct : reactors) {
        if (rct->getName() == reactorName) {
            return rct;
        }
    }
    // if it doesn't find a matching name make a new one with the appropriate name
    // can't use make_shared since constructor is private
    auto newReactor = std::shared_ptr<zmqReactor>(new zmqReactor(reactorName, contextName));
    reactors.push_back(newReactor);
    return newReactor;
}

void zmqReactor::addSocket(const zmqSocketDescriptor& desc)
{
    updates.emplace(ReactorInstruction::NEW_SOCKET, desc);
    notifier->send(zmq::const_buffer(&zero, sizeof(int)));
}

void zmqReactor::modifySocket(const zmqSocketDescriptor& desc)
{
    updates.emplace(ReactorInstruction::MODIFY, desc);
    notifier->send(zmq::const_buffer(&zero, sizeof(int)));
}

void zmqReactor::closeSocket(const std::string& socketName)
{
    updates.emplace(ReactorInstruction::CLOSE, socketName);
    notifier->send(zmq::const_buffer(&zero, sizeof(int)));
}

void zmqReactor::addSocketBlocking(const zmqSocketDescriptor& desc)
{
    unsigned int one(1);
    updates.emplace(ReactorInstruction::NEW_SOCKET, desc);
    notifier->send(zmq::const_buffer(&one, sizeof(int)));
    notifier->recv(zmq::mutable_buffer(&one, 4));
}

void zmqReactor::modifySocketBlocking(const zmqSocketDescriptor& desc)
{
    unsigned int one(1);

    updates.emplace(ReactorInstruction::MODIFY, desc);
    notifier->send(zmq::const_buffer(&one, sizeof(int)));

    notifier->recv(zmq::mutable_buffer(&one, 4));
}

void zmqReactor::closeSocketBlocking(const std::string& socketName)
{
    unsigned int one(1);
    updates.emplace(ReactorInstruction::CLOSE, socketName);
    notifier->send(zmq::const_buffer(&one, sizeof(int)));
    notifier->recv(zmq::mutable_buffer(&one, 4));
}

// this is not a member function but a helper function for the reactorLoop
static auto findSocketByName(const std::string& socketName,
                             const std::vector<std::string>& socketNames)
{
    int index = 0;
    for (const auto& socketLabel : socketNames) {
        if (socketLabel == socketName) {
            return index;
        }
        ++index;
    }
    return index;
}

void zmqReactor::reactorLoop()
{
    // make the signaling socket
    // use mostly local variables
    std::vector<zmq::socket_t> sockets;
    unsigned int messageCode = 0;

    sockets.emplace_back(zmqContextManager::getContext(contextManager->getName()),
                         zmq::socket_type::pair);
    sockets[0].connect(std::string("inproc://reactor_" + name).c_str());

    std::vector<zmq_pollitem_t> socketPolls;
    socketPolls.reserve(4);
    socketPolls.resize(1);
    std::vector<std::string> socketNames{name};
    std::vector<std::function<void(zmq::multipart_t res)>> socketCallbacks(1);
    int socketCount = 1;

    socketPolls[0].socket = sockets[0];
    socketPolls[0].fd = 0;
    socketPolls[0].events = ZMQ_POLLIN;
    socketPolls[0].revents = 0;
    reactorLoopRunning = true;
    while (true) {
        const int readySocketCount = zmq::poll(socketPolls);
        if (readySocketCount > 0) {
            // do the callbacks for any socket with a message received
            for (int kk = 1; kk < socketCount; ++kk) {
                const auto pollEvents = static_cast<std::uint16_t>(socketPolls[kk].revents);
                if ((pollEvents & static_cast<std::uint16_t>(ZMQ_POLLIN)) != 0U) {
                    socketCallbacks[kk](zmq::multipart_t(sockets[kk]));
                }
            }
            // deal with any socket updates as triggered by a message on socket 0
            const auto controlPollEvents = static_cast<std::uint16_t>(socketPolls[0].revents);
            if ((controlPollEvents & static_cast<std::uint16_t>(ZMQ_POLLIN)) != 0U) {
                sockets[0].recv(
                    zmq::mutable_buffer(&messageCode, sizeof(unsigned int)));  // clear the message
                auto socketop = updates.pop();
                while (socketop) {
                    int index;
                    auto instruction = (*socketop).first;
                    auto& descriptor = (*socketop).second;
                    switch (instruction) {
                        case ReactorInstruction::CLOSE:
                            index = findSocketByName(descriptor.name, socketNames);
                            if (std::cmp_less(index, sockets.size())) {
                                sockets[index].close();
                                sockets.erase(sockets.begin() + index);
                                socketNames.erase(socketNames.begin() + index);
                                socketCallbacks.erase(socketCallbacks.begin() + index);
                                socketPolls.erase(socketPolls.begin() + index);
                                --socketCount;
                            }
                            break;
                        case ReactorInstruction::NEW_SOCKET:
                            sockets.push_back(descriptor.makeSocket(
                                zmqContextManager::getContext(contextManager->getName())));
                            socketNames.push_back(descriptor.name);
                            socketCallbacks.emplace_back(descriptor.callback);
                            socketPolls.resize(socketPolls.size() + 1);
                            socketPolls.back().socket = sockets.back();
                            socketPolls.back().fd = 0;
                            socketPolls.back().events = ZMQ_POLLIN;
                            socketPolls.back().revents = 0;
                            ++socketCount;
                            break;
                        case ReactorInstruction::MODIFY:
                            index = findSocketByName(descriptor.name, socketNames);
                            if (std::cmp_less(index, sockets.size())) {
                                descriptor.modifySocket(sockets[index]);
                            }
                            // replace the callback if needed
                            if (descriptor.callback) {
                                socketCallbacks[index] = descriptor.callback;
                            }
                            break;
                        case ReactorInstruction::TERMINATE:
                            // jump out of the loop
                            goto REACTOR_HALT;
                        default:
                            break;
                    }
                    socketop = updates.pop();
                }
                if (messageCode > 0) {
                    sockets[0].send(zmq::const_buffer(&messageCode, sizeof(unsigned int)));
                }
            }
        }
    }
REACTOR_HALT:
    reactorLoopRunning = false;
}

}  // namespace zmqlib
