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
#include <string_view>
#include <utility>

namespace griddyn {
static classFactory<Communicator> commFac(std::vector<std::string>{"comm", "simple", "basic"},
                                          "basic");

Communicator::Communicator(): mId(getID())
{
    setName("comm_" + std::to_string(mId));
}
Communicator::Communicator(const std::string& name): helperObject(name), mId(getID()) {}
Communicator::Communicator(const std::string& name, std::uint64_t id): helperObject(name), mId(id)
{
}
Communicator::Communicator(std::uint64_t id): mId(id)
{
    setName("comm_" + std::to_string(mId));
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
    communicationsCore::instance()->send(mId, destID, std::move(message));
}

void Communicator::transmit(std::string_view destName, std::shared_ptr<commMessage> message)
{
    communicationsCore::instance()->send(mId, destName, std::move(message));
}

void Communicator::receive(std::uint64_t sourceID,
                           std::uint64_t destID,
                           std::shared_ptr<commMessage> message)
{
    if ((destID == mId) || (destID == 0)) {
        if (autoPingEnabled) {
            if (message->getMessageType() == commMessage::pingMessageType) {
                communicationsCore::instance()->send(
                    mId, sourceID, std::make_shared<commMessage>(commMessage::replyMessageType));
            } else if (message->getMessageType() == commMessage::replyMessageType) {
                mLastReplyRx = communicationsCore::instance()->getTime();
                return;
            }
        }
        if (mRxCallbackMessage) {
            mRxCallbackMessage(sourceID, message);
        } else {
            mMessageQueue.emplace(sourceID, message);
        }
    }
}

void Communicator::receive(std::uint64_t sourceID,
                           std::string_view destName,
                           std::shared_ptr<commMessage> message)
{
    if (destName == getName()) {
        if (autoPingEnabled) {
            if (message->getMessageType() == commMessage::pingMessageType) {
                communicationsCore::instance()->send(
                    mId, sourceID, std::make_shared<commMessage>(commMessage::replyMessageType));
            } else if (message->getMessageType() == commMessage::replyMessageType) {
                mLastReplyRx = communicationsCore::instance()->getTime();
                return;
            }
        }
        if (mRxCallbackMessage) {
            mRxCallbackMessage(sourceID, message);
        } else {
            mMessageQueue.emplace(sourceID, message);
        }
    }
}

bool Communicator::messagesAvailable() const
{
    return !(mMessageQueue.empty());
}
std::shared_ptr<commMessage> Communicator::getMessage(std::uint64_t& source)
{
    auto msg = mMessageQueue.pop();
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
    mLastPingSend = ccore->getTime();
    ccore->send(mId, destID, std::move(message));
}

void Communicator::ping(std::string_view destName)
{
    auto message = std::make_unique<commMessage>(commMessage::pingMessageType);
    auto ccore = communicationsCore::instance();
    mLastPingSend = ccore->getTime();
    ccore->send(mId, destName, std::move(message));
}

coreTime Communicator::getLastPingTime() const
{
    return mLastReplyRx - mLastPingSend;
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
