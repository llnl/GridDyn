/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zmqCommunicator.h"

#include "cppzmq/zmq_addon.hpp"
#include "griddyn/comms/CommMessage.h"
#include "zmqLibrary/zmqContextManager.h"
#include "zmqLibrary/zmqHelper.h"
#include "zmqLibrary/zmqProxyHub.h"
#include "zmqLibrary/zmqReactor.h"
#include <memory>
#include <string>
#include <utility>

namespace griddyn::zmqInterface {

ZmqCommunicator::ZmqCommunicator(const std::string& name):
    Communicator(name), txDescriptor(name + "_tx"), rxDescriptor(name + "_rx")
{
}

ZmqCommunicator::ZmqCommunicator(const std::string& name, std::uint64_t identifier):
    Communicator(name, identifier), txDescriptor(name + "_tx"), rxDescriptor(name + "_rx")
{
}

ZmqCommunicator::ZmqCommunicator(std::uint64_t identifier): Communicator(identifier) {}

ZmqCommunicator::~ZmqCommunicator() = default;

std::unique_ptr<Communicator> ZmqCommunicator::clone() const
{
    std::unique_ptr<Communicator> comm = std::make_unique<ZmqCommunicator>();
    ZmqCommunicator::cloneTo(comm.get());
    return comm;
}

void ZmqCommunicator::cloneTo(Communicator* comm) const
{
    Communicator::cloneTo(comm);
    auto* zmqComm = dynamic_cast<ZmqCommunicator*>(comm);
    if (zmqComm == nullptr) {
        return;
    }
    zmqComm->txDescriptor = txDescriptor;
    zmqComm->rxDescriptor = rxDescriptor;
    zmqComm->proxyName = proxyName;
    zmqComm->contextName = contextName;
    zmqComm->flags = flags;
}

void ZmqCommunicator::transmit(std::string_view destName,
                               const std::shared_ptr<CommMessage>& message)
{
    zmq::multipart_t txmsg;
    if (!flags[NO_TRANSMIT_DEST]) {
        txmsg.addstr(std::string{destName});
    }
    addHeader(txmsg, message);
    addMessageBody(txmsg, message);
    txmsg.send(*txSocket);
}

void ZmqCommunicator::transmit(std::uint64_t destID, const std::shared_ptr<CommMessage>& message)
{
    zmq::multipart_t txmsg;
    if (!flags[NO_TRANSMIT_DEST]) {
        txmsg.addmem(&destID, 8);
    }
    addHeader(txmsg, message);
    addMessageBody(txmsg, message);
    txmsg.send(*txSocket);
}

void ZmqCommunicator::addHeader(zmq::multipart_t& msg,
                                const std::shared_ptr<CommMessage>& /*message*/)
{
    if (!flags[NO_TRANSMIT_SOURCE]) {
        msg.addstr(getName());
    }
}

void ZmqCommunicator::addMessageBody(zmq::multipart_t& msg,
                                     const std::shared_ptr<CommMessage>& message)
{
    msg.addstr(message->toDataString());
}

void ZmqCommunicator::initialize()
{
    // don't initialize twice if we already initialized
    if (txSocket) {
        return;
    }

    // set up transmission sockets and information

    if (flags[useTxProxy]) {
        auto localProxy = zmqlib::ZmqProxyHub::getProxy(proxyName);
        if (!localProxy->isRunning()) {
            localProxy->startProxy();
        }
        txDescriptor.addOperation(zmqlib::SocketOperation::CONNECT,
                                  localProxy->getIncomingConnection());
    }

    if (flags[useRxProxy]) {
        auto localProxy = zmqlib::ZmqProxyHub::getProxy(proxyName);
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
    if (!flags[TRANSMIT_ONLY]) {
        zmqlib::ZmqReactor::getReactorInstance("", contextName)->addSocket(rxDescriptor);
    }

    txSocket = txDescriptor.makeSocketPtr(zmqlib::ZmqContextManager::getContext(contextName));
}

void ZmqCommunicator::disconnect()
{
    if (!flags[TRANSMIT_ONLY]) {
        zmqlib::ZmqReactor::getReactorInstance("")->closeSocket(getName() + "_rx");
    }
    txSocket = nullptr;
}

void ZmqCommunicator::set(std::string_view param, std::string_view val)
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

void ZmqCommunicator::set(std::string_view param, double val)
{
    Communicator::set(param, val);
}

void ZmqCommunicator::setFlag(std::string_view flag, bool val)
{
    if ((flag == "txonly") || (flag == "transmitonly") || (flag == "transmit_only")) {
        flags.set(TRANSMIT_ONLY, val);
    } else if (flag == "transmitsource") {
        flags.set(NO_TRANSMIT_SOURCE, !val);
    } else if (flag == "notransmitsource") {
        flags.set(NO_TRANSMIT_SOURCE, val);
    } else if (flag == "transmitdest") {
        flags.set(NO_TRANSMIT_DEST, !val);
    } else if (flag == "notransmitdest") {
        flags.set(NO_TRANSMIT_DEST, val);
    } else if (flag == "useproxy") {
        flags.set(useRxProxy, val);
        flags.set(useTxProxy, val);
    } else if (flag == "usetxproxy") {
        flags.set(useTxProxy, val);
    } else if (flag == "userxproxy") {
        flags.set(useRxProxy, val);
    } else {
        Communicator::setFlag(flag, val);
    }
}

void ZmqCommunicator::messageHandler(const zmq::multipart_t& msg)
{
    const auto messageSize = msg.size();
    // size should be either 2 or 3
    const auto* msgBody = (messageSize == 2U) ? msg.peek(1) : msg.peek(2);

    const std::string msgString(static_cast<const char*>(msgBody->data()), msgBody->size());
    auto gdMsg = std::make_shared<CommMessage>();
    gdMsg->fromDataString(msgString);

    // call the lower level receive function
    receive(0, getName(), gdMsg);
}

}  // namespace griddyn::zmqInterface
