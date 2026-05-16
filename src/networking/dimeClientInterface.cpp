/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dimeClientInterface.h"

#include "nlohmann/json.hpp"
#include "zmqLibrary/zmqContextManager.h"
#include <cstring>
#include <memory>
#include <string>

namespace {
using JsonValue = nlohmann::ordered_json;

std::string toJsonString(const JsonValue& value)
{
    return value.dump();
}

bool receivedOkResponse(zmq::socket_t& socket)
{
    char buffer[3] = {};
    const auto receivedSize =
        socket.recv(zmq::mutable_buffer(buffer, 2), zmq::recv_flags::none);
    return receivedSize && (*receivedSize == 2) && (std::strncmp(buffer, "OK", 2) == 0);
}

static void encodeVariableMessage(JsonValue& data, double val)
{
    JsonValue content;
    content["stdout"] = "";
    content["figures"] = "";
    content["datadir"] = "/tmp MatlabData/";

    JsonValue response;
    response["content"] = content;
    response["result"] = val;
    response["success"] = true;
    data["args"] = response;
    // response = { 'content': {'stdout': '', 'figures' : [], 'datadir' : '/tmp MatlabData/'},
    // 'result' : value, 'success' : True }     outgoing = { 'command': 'response', 'name' :
    // self.name, 'meta' : {'var_name': var_name},
    //'args' : self.matlab.json_encode(response) }
}
}  // namespace

DimeClientInterface::DimeClientInterface(const std::string& dimeName,
                                         const std::string& dimeAddress):
    mName(dimeName), mAddress(dimeAddress)
{
    if (mAddress.empty()) {
#ifdef WIN32
        mAddress = "tcp://127.0.0.1:5000";
#else
        mAddress = "ipc:///tmp/dime";
#endif
    }
}

DimeClientInterface::~DimeClientInterface() = default;

void DimeClientInterface::init()
{
    auto context = zmqlib::ZmqContextManager::getContextPointer();

    mSocket = std::make_unique<zmq::socket_t>(context->getBaseContext(), zmq::socket_type::req);
    mSocket->connect(mAddress);

    JsonValue outgoing;
    outgoing["command"] = "connect";
    outgoing["name"] = mName;
    outgoing["listen_to_events"] = false;

    mSocket->send(toJsonString(outgoing));

    if (!receivedOkResponse(*mSocket)) {
        throw InitFailure();
    }
}

void DimeClientInterface::close()
{
    if (mSocket) {
        JsonValue outgoing;
        outgoing["command"] = "exit";
        outgoing["name"] = mName;
        mSocket->send(toJsonString(outgoing));

        mSocket->close();
    }
    mSocket = nullptr;
}

void DimeClientInterface::sync() {}

void DimeClientInterface::sendVar(const std::string& varName,
                                  double val,
                                  const std::string& recipient)
{
    // outgoing = { 'command': 'send', 'name' : self.name, 'args' : var_name }
    JsonValue outgoing;

    outgoing["command"] = (recipient.empty()) ? "broadcast" : "send";

    outgoing["name"] = mName;
    outgoing["args"] = varName;
    mSocket->send(toJsonString(outgoing));

    if (!receivedOkResponse(*mSocket)) {
        throw SendFailure();
    }

    JsonValue outgoingData;
    outgoingData["command"] = "response";
    outgoingData["name"] = mName;
    if (!recipient.empty()) {
        outgoingData["meta"]["recipient_name"] = recipient;
    }

    outgoingData["meta"]["var_name"] = varName;
    encodeVariableMessage(outgoingData, val);
    mSocket->send(toJsonString(outgoingData));

    if (!receivedOkResponse(*mSocket)) {
        throw SendFailure();
    }
}

void DimeClientInterface::broadcast(const std::string& varName, double val)
{
    sendVar(varName, val);
}

void DimeClientInterface::getDevices() {}
