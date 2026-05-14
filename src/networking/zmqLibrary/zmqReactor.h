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

#pragma once

#include "gmlc/containers/SimpleQueue.hpp"
#include "zmqSocketDescriptor.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace zmqlib {
class zmqContextManager;

/** class that manages receive sockets and triggers callbacks
@detail the class starts up a thread that listens for */
class zmqReactor {
  private:
    /** enumeration of possible reactor instructions*/
    enum class ReactorInstruction : int {
        NEW_SOCKET,  //!< add a new socket
        CLOSE,  //!< close an existing socket
        MODIFY,  //!< modify an existing socket
        TERMINATE,  //!< terminate the socket
    };
    static std::vector<std::shared_ptr<zmqReactor>>
        reactors;  //!< container for pointers to all the available contexts

    std::string name;
    std::shared_ptr<zmqContextManager>
        contextManager;  //!< pointer the context the reactor is using

    gmlc::containers::SimpleQueue<std::pair<ReactorInstruction, zmqSocketDescriptor>>
        updates;  //!< the modifications to make the reactor sockets

    std::unique_ptr<zmq::socket_t> notifier;
    std::thread loopThread;
    /** private constructor*/
    zmqReactor(const std::string& reactorName, const std::string& context);
    std::atomic<bool> reactorLoopRunning{false};

  public:
    static std::shared_ptr<zmqReactor> getReactorInstance(const std::string& reactorName,
                                                          const std::string& contextName = "");

    ~zmqReactor();

    void addSocket(const zmqSocketDescriptor& desc);
    void modifySocket(const zmqSocketDescriptor& desc);
    /** asyncrhonous call to close a specific socket
    @param socketName the name of the socket to close
    */
    void closeSocket(const std::string& socketName);

    void addSocketBlocking(const zmqSocketDescriptor& desc);
    void modifySocketBlocking(const zmqSocketDescriptor& desc);
    void closeSocketBlocking(const std::string& socketName);

    const std::string& getName() const { return name; }
    void terminateReactor();

  private:
    void reactorLoop();
};
}  // namespace zmqlib
