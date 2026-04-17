/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcpCommunicator.h"

#include "AsioServiceManager.h"
#include "TcpHelperClasses.h"
#include "griddyn/comms/commMessage.h"
#include <memory>
#include <string>

namespace griddyn {
namespace tcpLib {

    tcpCommunicator::tcpCommunicator(const std::string& name): Communicator(name) {}

    tcpCommunicator::tcpCommunicator(const std::string& name, std::uint64_t id):
        Communicator(name, id)
    {
    }

    tcpCommunicator::tcpCommunicator(std::uint64_t id): Communicator(id) {}

    tcpCommunicator::~tcpCommunicator() = default;

    std::unique_ptr<Communicator> tcpCommunicator::clone() const
    {
        std::unique_ptr<Communicator> comm = std::make_unique<tcpCommunicator>();
        tcpCommunicator::cloneTo(comm.get());
        return comm;
    }

    void tcpCommunicator::cloneTo(Communicator* comm) const
    {
        Communicator::cloneTo(comm);
        // auto zmqComm = dynamic_cast<tcpCommunicator*>(comm);
        //  if (zmqComm == nullptr) {
        //      return;
        //  }
        //   zmqComm->proxyName = proxyName;
        //  zmqComm->contextName = contextName;
        //   zmqComm->flags = flags;
    }

    void tcpCommunicator::transmit(const std::string& destName,
                                   std::shared_ptr<commMessage> /* message */)
    {
        //  zmq::multipart_t txmsg;
        //  if (!flags[no_transmit_dest]) {
        //     txmsg.addstr(destName);
        // }
        // addHeader(txmsg, message);
        // addMessageBody(txmsg, message);
        // txmsg.send(*txSocket);
    }

    void tcpCommunicator::transmit(std::uint64_t destID, std::shared_ptr<commMessage> /* message */)
    {
        //  zmq::multipart_t txmsg;
        // if (!flags[no_transmit_dest]) {
        //    txmsg.addmem(&destID, 8);
        //}
        // addHeader(txmsg, message);
        // addMessageBody(txmsg, message);
        // txmsg.send(*txSocket);
    }

    // void tcpCommunicator::addHeader(zmq::multipart_t &msg, std::shared_ptr<commMessage> &
    // /*message*/)
    //{
    //    if (!flags[no_transmit_source])
    //    {
    //        msg.addstr(getName());
    //    }
    //}

    // void tcpCommunicator::addMessageBody(zmq::multipart_t &msg, std::shared_ptr<commMessage>
    // &message)
    //{
    //    msg.addstr(message->to_datastring());
    //}

    void tcpCommunicator::initialize()
    {
        // don't initialize twice if we already initialized
        //    if (txSocket)
        {
            return;
        }

        // set up transmission sockets and information

        if (flags[use_tx_proxy]) {
            //    auto localProxy = zmqProxyHub::getProxy(proxyName);
            //    if (!localProxy->isRunning())
            //    {
            //        localProxy->startProxy();
            //    }
            //    txDescriptor.addOperation(socket_ops::connect,
            //    localProxy->getIncomingConnection());
        }

        if (flags[use_rx_proxy]) {
            // auto localProxy = zmqProxyHub::getProxy(proxyName);
            // if (!localProxy->isRunning())
            //{
            //    localProxy->startProxy();
            //}
            // rxDescriptor.addOperation(socket_ops::connect, localProxy->getIncomingConnection());
        }

        // txDescriptor.addOperation(socket_ops::subscribe, getName());
        // rxDescriptor.addOperation(socket_ops::subscribe, getName());

        // auto id = getID();
        // txDescriptor.addOperation(socket_ops::subscribe, std::string(reinterpret_cast<char
        // *>(&id), sizeof(id)));
        // //I know this is ugly rxDescriptor.addOperation(socket_ops::subscribe,
        // std::string(reinterpret_cast<char
        // *>(&id), sizeof(id)));  //I know this is ugly decltype(id) broadcastId = 0;
        // txDescriptor.addOperation(socket_ops::subscribe, std::string(reinterpret_cast<char
        // *>(&broadcastId), sizeof(broadcastId)));  //I know this is ugly
        // rxDescriptor.addOperation(socket_ops::subscribe, std::string(reinterpret_cast<char
        // *>(&broadcastId), sizeof(broadcastId)));  //I know this is ugly

        // rxDescriptor.callback = [this](const multipart_t &msg) {messageHandler(msg); };
        // set up the rx socket reactor
        // if (!flags[transmit_only])
        //{
        //    zmqReactor::getReactorInstance("", contextName)->addSocket(rxDescriptor);
        //}

        // txSocket = txDescriptor.makeSocketPtr(zmqContextManager::getContext(contextName));
    }

    void tcpCommunicator::disconnect()
    {
        if (!flags[transmit_only]) {
            //    zmqReactor::getReactorInstance("")->closeSocket(getName() + "_rx");
        }
        // txSocket = nullptr;
    }

    void tcpCommunicator::set(const std::string& param, const std::string& val)
    {
        if (param == "txconnection") {
            // txDescriptor.addOperation(socket_ops::connect, val);
        } else if (param == "rxconnection") {
            //    rxDescriptor.addOperation(socket_ops::connect, val);
        } else if (param == "rxsubscription") {
            // rxDescriptor.addOperation(socket_ops::subscribe, val);
        } else if (param == "txsubscription") {
            //    txDescriptor.addOperation(socket_ops::subscribe, val);
        } else if ((param == "proxy") || (param == "proxyname")) {
            proxyName = val;
            setFlag("useproxy", true);
        } else if ((param == "txtype") || (param == "sockettype")) {
            //    txDescriptor.type = socketTypeFromString(val);
        } else if (param == "rxtype") {
            //    rxDescriptor.type = socketTypeFromString(val);
        } else {
            Communicator::set(param, val);
        }
    }

    void tcpCommunicator::set(const std::string& param, double val)
    {
        Communicator::set(param, val);
    }

    void tcpCommunicator::setFlag(const std::string& flag, bool val)
    {
        if ((flag == "txonly") || (flag == "transmitonly") || (flag == "transmit_only")) {
            flags.set(transmit_only, val);
        } else if (flag == "transmitsource") {
            flags.set(no_transmit_source, !val);
        } else if (flag == "notransmitsource") {
            flags.set(no_transmit_source, val);
        } else if (flag == "transmitdest") {
            flags.set(no_transmit_dest, !val);
        } else if (flag == "notransmitdest") {
            flags.set(no_transmit_dest, val);
        } else if (flag == "useproxy") {
            flags.set(use_rx_proxy, val);
            flags.set(use_tx_proxy, val);
        } else if (flag == "usetxproxy") {
            flags.set(use_tx_proxy, val);
        } else if (flag == "userxproxy") {
            flags.set(use_rx_proxy, val);
        } else {
            Communicator::setFlag(flag, val);
        }
    }

    /* void tcpCommunicator::messageHandler(const multipart_t& msg)
     {
         auto sz = msg.size();
         // size should be either 2 or 3
         auto msgBody = (sz == 2) ? msg.peek(1) : msg.peek(2);

         std::string msgString(static_cast<const char*>(msgBody->data()), msgBody->size());
         std::shared_ptr<commMessage> gdMsg;
         gdMsg->from_datastring(msgString);

         // call the lower level receive function
         receive(0, getName(), std::move(gdMsg));
     }
     */

}  // namespace tcpLib
}  // namespace griddyn
