/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "commManager.h"

#include "Communicator.h"
#include "core/propertyBuffer.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn::comms {
commManager::commManager() = default;
commManager::commManager(const commManager& other)
{
    commName = other.commName;
    commId = other.commId;
    commType = other.commType;
    commDestName = other.commDestName;
    commDestId = other.commDestId;
    if (other.commLink) {
        commLink = other.commLink->clone();
    }
    if (other.commPropBuffer) {
        commPropBuffer = std::make_unique<griddyn::propertyBuffer>(*other.commPropBuffer);
    }
}

commManager::commManager(commManager&&) = default;
commManager::~commManager() = default;

commManager& commManager::operator=(const commManager& other)
{
    if (this == &other) {
        return *this;
    }

    commName = other.commName;
    commId = other.commId;
    commType = other.commType;
    commDestName = other.commDestName;
    commDestId = other.commDestId;
    if (other.commLink) {
        commLink = other.commLink->clone();
    } else {
        commLink = nullptr;
    }

    if (other.commPropBuffer) {
        commPropBuffer = std::make_unique<griddyn::propertyBuffer>(*(other.commPropBuffer));
    } else {
        commPropBuffer = nullptr;
    }
    return *this;
}

commManager& commManager::operator=(commManager&&) = default;

void commManager::setName(std::string_view name)
{
    commName = std::string{name};
}
bool commManager::set(std::string_view param, std::string_view val)
{
    if ((param == "commname") || (param == "name")) {
        setName(val);
    } else if (param == "commtype") {
        commType = std::string{val};
    } else if ((param == "commdest") || (param == "destination")) {
        if (val.front() == '#') {
            commDestId = std::stoull(std::string{val.substr(1)});
        } else {
            commDestName = std::string{val};
        }
    } else if (param.starts_with("comm::")) {
        if (commLink) {
            commLink->set(std::string{param.substr(6)}, std::string{val});
        } else {
            if (!commPropBuffer) {
                commPropBuffer = std::make_unique<griddyn::propertyBuffer>();
            }
            commPropBuffer->set(param.substr(6), val);
        }
    } else {
        return false;
    }
    return true;
}
bool commManager::set(std::string_view param, double val)
{
    if ((param == "commid") || (param == "id")) {
        commId = static_cast<std::uint64_t>(val);
    } else if ((param == "commdestid") || (param == "destid")) {
        commDestId = static_cast<uint64_t>(val);
    } else if (param.starts_with("comm::")) {
        if (commLink) {
            commLink->set(std::string{param.substr(6)}, val);
        } else {
            if (!commPropBuffer) {
                commPropBuffer = std::make_unique<propertyBuffer>();
            }
            commPropBuffer->set(param.substr(6), val);
        }
    } else {
        return false;
    }

    return true;
}

bool commManager::setFlag(std::string_view flag, bool val)
{
    if (flag.starts_with("comm::")) {
        if (commLink) {
            commLink->setFlag(std::string{flag.substr(6)}, val);
        } else {
            if (!commPropBuffer) {
                commPropBuffer = std::make_unique<griddyn::propertyBuffer>();
            }
            commPropBuffer->setFlag(flag.substr(6), val);
        }
    } else {
        return false;
    }
    return true;
}

std::shared_ptr<Communicator> commManager::build()
{
    commLink = makeCommunicator(commType, commName, commId);
    if (commPropBuffer) {
        commPropBuffer->apply(commLink);
        commPropBuffer = nullptr;
    }
    return commLink;
}

void commManager::send(std::shared_ptr<commMessage> message) const
{
    if (commDestId != 0) {
        commLink->transmit(commDestId, std::move(message));
    } else if (!commDestName.empty()) {
        commLink->transmit(commDestName, std::move(message));
    } else {
        commLink->transmit(0, std::move(message));
    }
}

}  // namespace griddyn::comms
