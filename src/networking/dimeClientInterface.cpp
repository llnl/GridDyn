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
}  // namespace

dimeClientInterface::dimeClientInterface(const std::string& dimeName,
                                         const std::string& dimeAddress):
    name(dimeName), address(dimeAddress)
{
    if (address.empty()) {
#ifdef WIN32
        address = "tcp://127.0.0.1:5000";
#else
        address = "ipc:///tmp/dime";
#endif
    }
}

dimeClientInterface::~dimeClientInterface() = default;

void dimeClientInterface::init()
{
    auto context = zmqlib::zmqContextManager::getContextPointer();

    socket = std::make_unique<zmq::socket_t>(context->getBaseContext(), zmq::socket_type::req);
    socket->connect(address);

    JsonValue outgoing;
    outgoing["command"] = "connect";
    outgoing["name"] = name;
    outgoing["listen_to_events"] = false;

    socket->send(toJsonString(outgoing));

    char buffer[3] = {};
    auto sz = socket->recv(buffer, 3, 0);
    if ((sz != 2) || (strncmp(buffer, "OK", 3) != 0)) {
        throw initFailure();
    }
}

void dimeClientInterface::close()
{
    if (socket) {
        JsonValue outgoing;
        outgoing["command"] = "exit";
        outgoing["name"] = name;
        socket->send(toJsonString(outgoing));

        socket->close();
    }
    socket = nullptr;
}

void dimeClientInterface::sync() {}

void encodeVariableMessage(JsonValue& data, double val)
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
void dimeClientInterface::send_var(const std::string& varName,
                                   double val,
                                   const std::string& recipient)
{
    // outgoing = { 'command': 'send', 'name' : self.name, 'args' : var_name }
    JsonValue outgoing;

    outgoing["command"] = (recipient.empty()) ? "broadcast" : "send";

    outgoing["name"] = name;
    outgoing["args"] = varName;
    socket->send(toJsonString(outgoing));

    char buffer[3];
    auto sz = socket->recv(buffer, 3, 0);
    // TODO(phlpt): Check recv value.

    JsonValue outgoingData;
    outgoingData["command"] = "response";
    outgoingData["name"] = name;
    if (!recipient.empty()) {
        outgoingData["meta"]["recipient_name"] = recipient;
    }

    outgoingData["meta"]["var_name"] = varName;
    encodeVariableMessage(outgoingData, val);
    socket->send(toJsonString(outgoingData));

    sz = socket->recv(buffer, 3, 0);
    if (sz != 2)  // TODO(phlpt): Check for "OK".
    {
        throw(sendFailure());
    }
}

void dimeClientInterface::broadcast(const std::string& varName, double val)
{
    send_var(varName, val);
}

void dimeClientInterface::get_devices() {}
