/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "commManager.h"

#include "Communicator.h"
#include "core/propertyBuffer.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn::comms {
commManager::commManager() = default;
commManager::commManager(const commManager& cm)
{
    commName = cm.commName;
    commId = cm.commId;
    commType = cm.commType;
    commDestName = cm.commDestName;
    commDestId = cm.commDestId;
    if (cm.commLink) {
        commLink = cm.commLink->clone();
    }
    if (cm.commPropBuffer) {
        commPropBuffer = std::make_unique<griddyn::propertyBuffer>(*cm.commPropBuffer);
    }
}

commManager::commManager(commManager&&) = default;
commManager::~commManager() = default;

commManager& commManager::operator=(const commManager& cm)
{
    commName = cm.commName;
    commId = cm.commId;
    commType = cm.commType;
    commDestName = cm.commDestName;
    commDestId = cm.commDestId;
    if (cm.commLink) {
        commLink = cm.commLink->clone();
    }

    if (cm.commPropBuffer) {
        commPropBuffer = std::make_unique<griddyn::propertyBuffer>(*(cm.commPropBuffer));
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
    } else if (param.compare(0, 6, "comm::") == 0) {
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
    } else if (param.compare(0, 6, "comm::") == 0) {
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
    if (flag.compare(0, 6, "comm::") == 0) {
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

void commManager::send(std::shared_ptr<commMessage> m) const
{
    if (commDestId != 0) {
        commLink->transmit(commDestId, std::move(m));
    } else if (!commDestName.empty()) {
        commLink->transmit(commDestName, std::move(m));
    } else {
        commLink->transmit(0, std::move(m));
    }
}

}  // namespace griddyn::comms
