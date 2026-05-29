/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "CommManager.h"

#include "Communicator.h"
#include "core/PropertyBuffer.h"
#include <charconv>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn::comms {
CommManager::CommManager() = default;
CommManager::CommManager(const CommManager& other)
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
        commPropBuffer = std::make_unique<griddyn::PropertyBuffer>(*other.commPropBuffer);
    }
}

CommManager::CommManager(CommManager&&) = default;
CommManager::~CommManager() = default;

CommManager& CommManager::operator=(const CommManager& other)
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
        commPropBuffer = std::make_unique<griddyn::PropertyBuffer>(*(other.commPropBuffer));
    } else {
        commPropBuffer = nullptr;
    }
    return *this;
}

CommManager& CommManager::operator=(CommManager&&) = default;

void CommManager::setName(std::string_view name)
{
    commName = std::string{name};
}
bool CommManager::set(std::string_view param, std::string_view val)
{
    if ((param == "commname") || (param == "name")) {
        setName(val);
    } else if (param == "commtype") {
        commType = std::string{val};
    } else if ((param == "commdest") || (param == "destination")) {
        if (val.front() == '#') {
            const auto destId = val.substr(1);
            const auto* begin = destId.data();
            const auto* end = begin + destId.size();
            auto result = std::from_chars(begin, end, commDestId);
            if ((result.ec != std::errc{}) || (result.ptr != end)) {
                throw std::invalid_argument("invalid communicator destination id");
            }
        } else {
            commDestName = std::string{val};
        }
    } else if (param.starts_with("comm::")) {
        if (commLink) {
            commLink->set(std::string{param.substr(6)}, std::string{val});
        } else {
            if (!commPropBuffer) {
                commPropBuffer = std::make_unique<griddyn::PropertyBuffer>();
            }
            commPropBuffer->set(param.substr(6), val);
        }
    } else {
        return false;
    }
    return true;
}
bool CommManager::set(std::string_view param, double val)
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
                commPropBuffer = std::make_unique<PropertyBuffer>();
            }
            commPropBuffer->set(param.substr(6), val);
        }
    } else {
        return false;
    }

    return true;
}

bool CommManager::setFlag(std::string_view flag, bool val)
{
    if (flag.starts_with("comm::")) {
        if (commLink) {
            commLink->setFlag(std::string{flag.substr(6)}, val);
        } else {
            if (!commPropBuffer) {
                commPropBuffer = std::make_unique<griddyn::PropertyBuffer>();
            }
            commPropBuffer->setFlag(flag.substr(6), val);
        }
    } else {
        return false;
    }
    return true;
}

std::shared_ptr<Communicator> CommManager::build()
{
    commLink = makeCommunicator(commType, commName, commId);
    if (commPropBuffer) {
        commPropBuffer->apply(commLink);
        commPropBuffer = nullptr;
    }
    return commLink;
}

void CommManager::send(const std::shared_ptr<CommMessage>& message) const
{
    if (commDestId != 0) {
        commLink->transmit(commDestId, message);
    } else if (!commDestName.empty()) {
        commLink->transmit(commDestName, message);
    } else {
        commLink->transmit(0, message);
    }
}

}  // namespace griddyn::comms
