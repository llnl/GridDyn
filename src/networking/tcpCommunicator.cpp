/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcpCommunicator.h"

#include "griddyn/comms/commMessage.h"
#include <memory>
#include <string>

namespace griddyn::tcpLib {

TcpCommunicator::TcpCommunicator(const std::string& name): Communicator(name) {}

TcpCommunicator::TcpCommunicator(const std::string& name, std::uint64_t identifier):
    Communicator(name, identifier)
{
}

TcpCommunicator::TcpCommunicator(std::uint64_t identifier): Communicator(identifier) {}

TcpCommunicator::~TcpCommunicator() = default;

std::unique_ptr<Communicator> TcpCommunicator::clone() const
{
    std::unique_ptr<Communicator> comm = std::make_unique<TcpCommunicator>();
    TcpCommunicator::cloneTo(comm.get());
    return comm;
}

void TcpCommunicator::cloneTo(Communicator* comm) const
{
    Communicator::cloneTo(comm);
    // auto tcpComm = dynamic_cast<TcpCommunicator*>(comm);
    //  if (zmqComm == nullptr) {
    //      return;
    //  }
    //   zmqComm->proxyName = proxyName;
    //  zmqComm->contextName = contextName;
    //   zmqComm->flags = flags;
}

void TcpCommunicator::transmit(std::string_view /*destName*/,
                               const std::shared_ptr<commMessage>& /* message */)
{
    //  zmq::multipart_t txmsg;
    //  if (!flags[no_transmit_dest]) {
    //     txmsg.addstr(destName);
    // }
    // addHeader(txmsg, message);
    // addMessageBody(txmsg, message);
    // txmsg.send(*txSocket);
}

void TcpCommunicator::transmit(std::uint64_t /*destID*/,
                               const std::shared_ptr<commMessage>& /* message */)
{
    //  zmq::multipart_t txmsg;
    // if (!flags[no_transmit_dest]) {
    //    txmsg.addmem(&destID, 8);
    //}
    // addHeader(txmsg, message);
    // addMessageBody(txmsg, message);
    // txmsg.send(*txSocket);
}

// void TcpCommunicator::addHeader(zmq::multipart_t &msg, std::shared_ptr<commMessage> &
// /*message*/)
//{
//    if (!flags[no_transmit_source])
//    {
//        msg.addstr(getName());
//    }
//}

// void TcpCommunicator::addMessageBody(zmq::multipart_t &msg, std::shared_ptr<commMessage>
// &message)
//{
//    msg.addstr(message->to_datastring());
//}

void TcpCommunicator::initialize()
{
    // TcpCommunicator transport setup is currently disabled.
}

void TcpCommunicator::disconnect()
{
    if (!flags[transmit_only]) {
        //    ZmqReactor::getReactorInstance("")->closeSocket(getName() + "_rx");
    }
    // txSocket = nullptr;
}

void TcpCommunicator::set(std::string_view param, std::string_view val)
{
    if ((param == "txconnection") || (param == "rxconnection") || (param == "rxsubscription") ||
        (param == "txsubscription") || (param == "txtype") || (param == "sockettype") ||
        (param == "rxtype")) {
        return;
    }

    if ((param == "proxy") || (param == "proxyname")) {
        proxyName = val;
        setFlag("useproxy", true);
        return;
    }

    Communicator::set(param, val);
}

void TcpCommunicator::set(std::string_view param, double val)
{
    Communicator::set(param, val);
}

void TcpCommunicator::setFlag(std::string_view flag, bool val)
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

/* void TcpCommunicator::messageHandler(const multipart_t& msg)
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

}  // namespace griddyn::tcpLib
