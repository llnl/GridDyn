/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zmqCommunicator.h"

#include "cppzmq/zmq_addon.hpp"
#include "griddyn/comms/commMessage.h"
#include "zmqLibrary/zmqContextManager.h"
#include "zmqLibrary/zmqHelper.h"
#include "zmqLibrary/zmqProxyHub.h"
#include "zmqLibrary/zmqReactor.h"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::zmqInterface {

zmqCommunicator::zmqCommunicator(const std::string& name):
    Communicator(name), txDescriptor(name + "_tx"), rxDescriptor(name + "_rx")
{
}

zmqCommunicator::zmqCommunicator(const std::string& name, std::uint64_t identifier):
    Communicator(name, identifier), txDescriptor(name + "_tx"), rxDescriptor(name + "_rx")
{
}

zmqCommunicator::zmqCommunicator(std::uint64_t identifier): Communicator(identifier) {}

zmqCommunicator::~zmqCommunicator() = default;

std::unique_ptr<Communicator> zmqCommunicator::clone() const
{
    std::unique_ptr<Communicator> comm = std::make_unique<zmqCommunicator>();
    zmqCommunicator::cloneTo(comm.get());
    return comm;
}

void zmqCommunicator::cloneTo(Communicator* comm) const
{
    Communicator::cloneTo(comm);
    auto* zmqComm = dynamic_cast<zmqCommunicator*>(comm);
    if (zmqComm == nullptr) {
        return;
    }
    zmqComm->txDescriptor = txDescriptor;
    zmqComm->rxDescriptor = rxDescriptor;
    zmqComm->proxyName = proxyName;
    zmqComm->contextName = contextName;
    zmqComm->flags = flags;
}

void zmqCommunicator::transmit(std::string_view destName,
                               const std::shared_ptr<commMessage>& message)
{
    zmq::multipart_t txmsg;
    if (!flags[no_transmit_dest]) {
        txmsg.addstr(std::string{destName});
    }
    addHeader(txmsg, message);
    addMessageBody(txmsg, message);
    txmsg.send(*txSocket);
}

void zmqCommunicator::transmit(std::uint64_t destID, const std::shared_ptr<commMessage>& message)
{
    zmq::multipart_t txmsg;
    if (!flags[no_transmit_dest]) {
        txmsg.addmem(&destID, 8);
    }
    addHeader(txmsg, message);
    addMessageBody(txmsg, message);
    txmsg.send(*txSocket);
}

void zmqCommunicator::addHeader(zmq::multipart_t& msg,
                                const std::shared_ptr<commMessage>& /*message*/)
{
    if (!flags[no_transmit_source]) {
        msg.addstr(getName());
    }
}

void zmqCommunicator::addMessageBody(zmq::multipart_t& msg,
                                     const std::shared_ptr<commMessage>& message)
{
    msg.addstr(message->to_datastring());
}

void zmqCommunicator::initialize()
{
    // don't initialize twice if we already initialized
    if (txSocket) {
        return;
    }

    // set up transmission sockets and information

    if (flags[use_tx_proxy]) {
        auto localProxy = zmqlib::zmqProxyHub::getProxy(proxyName);
        if (!localProxy->isRunning()) {
            localProxy->startProxy();
        }
        txDescriptor.addOperation(zmqlib::SocketOperation::CONNECT,
                                  localProxy->getIncomingConnection());
    }

    if (flags[use_rx_proxy]) {
        auto localProxy = zmqlib::zmqProxyHub::getProxy(proxyName);
        if (!localProxy->isRunning()) {
            localProxy->startProxy();
        }
        rxDescriptor.addOperation(zmqlib::SocketOperation::CONNECT,
                                  localProxy->getIncomingConnection());
    }

    txDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE, getName());
    rxDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE, getName());

    auto messageId = getID();
    txDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE,
                              std::string(reinterpret_cast<char*>(&messageId),
                                          sizeof(messageId)));  // I know this is ugly
    rxDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE,
                              std::string(reinterpret_cast<char*>(&messageId),
                                          sizeof(messageId)));  // I know this is ugly
    decltype(messageId) broadcastId = 0;
    txDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE,
                              std::string(reinterpret_cast<char*>(&broadcastId),
                                          sizeof(broadcastId)));  // I know this is ugly
    rxDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE,
                              std::string(reinterpret_cast<char*>(&broadcastId),
                                          sizeof(broadcastId)));  // I know this is ugly

    rxDescriptor.callback = [this](const zmq::multipart_t& msg) { messageHandler(msg); };
    // set up the rx socket reactor
    if (!flags[transmit_only]) {
        zmqlib::zmqReactor::getReactorInstance("", contextName)->addSocket(rxDescriptor);
    }

    txSocket = txDescriptor.makeSocketPtr(zmqlib::zmqContextManager::getContext(contextName));
}

void zmqCommunicator::disconnect()
{
    if (!flags[transmit_only]) {
        zmqlib::zmqReactor::getReactorInstance("")->closeSocket(getName() + "_rx");
    }
    txSocket = nullptr;
}

void zmqCommunicator::set(std::string_view param, std::string_view val)
{
    if (param == "txconnection") {
        txDescriptor.addOperation(zmqlib::SocketOperation::CONNECT, std::string{val});
    } else if (param == "rxconnection") {
        rxDescriptor.addOperation(zmqlib::SocketOperation::CONNECT, std::string{val});
    } else if (param == "rxsubscription") {
        rxDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE, std::string{val});
    } else if (param == "txsubscription") {
        txDescriptor.addOperation(zmqlib::SocketOperation::SUBSCRIBE, std::string{val});
    } else if ((param == "proxy") || (param == "proxyname")) {
        proxyName = val;
        setFlag("useproxy", true);
    } else if ((param == "txtype") || (param == "sockettype")) {
        txDescriptor.type = zmqlib::socketTypeFromString(std::string{val});
    } else if (param == "rxtype") {
        rxDescriptor.type = zmqlib::socketTypeFromString(std::string{val});
    } else {
        Communicator::set(param, val);
    }
}

void zmqCommunicator::set(std::string_view param, double val)
{
    Communicator::set(param, val);
}

void zmqCommunicator::setFlag(std::string_view flag, bool val)
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

void zmqCommunicator::messageHandler(const zmq::multipart_t& msg)
{
    const auto messageSize = msg.size();
    // size should be either 2 or 3
    const auto* msgBody = (messageSize == 2U) ? msg.peek(1) : msg.peek(2);

    const std::string msgString(static_cast<const char*>(msgBody->data()), msgBody->size());
    auto gdMsg = std::make_shared<commMessage>();
    gdMsg->from_datastring(msgString);

    // call the lower level receive function
    receive(0, getName(), gdMsg);
}

}  // namespace griddyn::zmqInterface
