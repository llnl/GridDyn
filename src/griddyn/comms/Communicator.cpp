/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Communicator.h"

#include "commMessage.h"
#include "communicationsCore.h"
#include "core/factoryTemplates.hpp"
#include "griddyn/griddyn-config.h"
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
static classFactory<Communicator> commFac(std::vector<std::string>{"comm", "simple", "basic"},
                                          "basic");

Communicator::Communicator(): m_id(getID())
{
    setName("comm_" + std::to_string(m_id));
}
Communicator::Communicator(const std::string& name): helperObject(name), m_id(getID()) {}
Communicator::Communicator(const std::string& name, std::uint64_t id): helperObject(name), m_id(id)
{
}
Communicator::Communicator(std::uint64_t id): m_id(id)
{
    setName("comm_" + std::to_string(m_id));
}
std::unique_ptr<Communicator> Communicator::clone() const
{
    auto comm = std::make_unique<Communicator>();
    Communicator::cloneTo(comm.get());
    return comm;
}

void Communicator::cloneTo(Communicator* comm) const
{
    comm->setName(getName());
    comm->autoPingEnabled = autoPingEnabled;
}

void Communicator::transmit(std::uint64_t destID, std::shared_ptr<commMessage> message)
{
    communicationsCore::instance()->send(m_id, destID, std::move(message));
}

void Communicator::transmit(const std::string& destName, std::shared_ptr<commMessage> message)
{
    communicationsCore::instance()->send(m_id, destName, std::move(message));
}

void Communicator::receive(std::uint64_t sourceID,
                           std::uint64_t destID,
                           std::shared_ptr<commMessage> message)
{
    if ((destID == m_id) || (destID == 0)) {
        if (autoPingEnabled) {
            if (message->getMessageType() == commMessage::pingMessageType) {
                communicationsCore::instance()->send(
                    m_id, sourceID, std::make_shared<commMessage>(commMessage::replyMessageType));
            } else if (message->getMessageType() == commMessage::replyMessageType) {
                lastReplyRX = communicationsCore::instance()->getTime();
                return;
            }
        }
        if (m_rxCallbackMessage) {
            m_rxCallbackMessage(sourceID, message);
        } else {
            messageQueue.emplace(sourceID, message);
        }
    }
}

void Communicator::receive(std::uint64_t sourceID,
                           const std::string& destName,
                           std::shared_ptr<commMessage> message)
{
    if (destName == getName()) {
        if (autoPingEnabled) {
            if (message->getMessageType() == commMessage::pingMessageType) {
                communicationsCore::instance()->send(
                    m_id, sourceID, std::make_shared<commMessage>(commMessage::replyMessageType));
            } else if (message->getMessageType() == commMessage::replyMessageType) {
                lastReplyRX = communicationsCore::instance()->getTime();
                return;
            }
        }
        if (m_rxCallbackMessage) {
            m_rxCallbackMessage(sourceID, message);
        } else {
            messageQueue.emplace(sourceID, message);
        }
    }
}

bool Communicator::messagesAvailable() const
{
    return !(messageQueue.empty());
}
std::shared_ptr<commMessage> Communicator::getMessage(std::uint64_t& source)
{
    auto msg = messageQueue.pop();
    if (!msg) {
        return nullptr;
    }
    source = msg->first;
    return msg->second;
}

// ping functions
void Communicator::ping(std::uint64_t destID)
{
    auto message = std::make_unique<commMessage>(commMessage::pingMessageType);
    auto ccore = communicationsCore::instance();
    lastPingSend = ccore->getTime();
    ccore->send(m_id, destID, std::move(message));
}

void Communicator::ping(const std::string& destName)
{
    auto message = std::make_unique<commMessage>(commMessage::pingMessageType);
    auto ccore = communicationsCore::instance();
    lastPingSend = ccore->getTime();
    ccore->send(m_id, destName, std::move(message));
}

coreTime Communicator::getLastPingTime() const
{
    return lastReplyRX - lastPingSend;
}
void Communicator::set(std::string_view param, std::string_view val)
{
    if ((param == "id") || (param == "name")) {
        setName(val);
    } else {
        helperObject::set(param, val);
    }
}
void Communicator::set(std::string_view param, double val)
{
    if ((param == "id") || (param == "name")) {
        setCommID(static_cast<uint64_t>(val));
    } else {
        helperObject::set(param, val);
    }
}

void Communicator::setFlag(std::string_view flag, bool val)
{
    if (flag == "autopingenabled") {
        autoPingEnabled = val;
    } else {
        helperObject::setFlag(flag, val);
    }
}

void Communicator::initialize()
{
    communicationsCore::instance()->registerCommunicator(this);
}
void Communicator::disconnect()
{
    communicationsCore::instance()->unregisterCommunicator(this);
}
std::unique_ptr<Communicator> makeCommunicator(const std::string& commType,
                                               const std::string& commName,
                                               const std::uint64_t id)
{
    auto ret = coreClassFactory<Communicator>::instance()->createObject(commType, commName);

    if (id != 0) {
        ret->setCommID(id);
    }

    return ret;
}
}  // namespace griddyn
