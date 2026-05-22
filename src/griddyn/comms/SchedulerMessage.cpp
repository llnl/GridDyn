/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SchedulerMessage.h"

#include "gmlc/utilities/stringConversion.h"
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn::comms {
namespace {
    dPayloadFactory<schedulerMessagePayload,
                    BASE_SCHEDULER_MESSAGE_NUMBER,
                    BASE_SCHEDULER_MESSAGE_NUMBER + 16>
        gSchedulerPayloadFactory("scheduler");
}  // namespace

REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_CLEAR_TARGETS,
                      "CLEAR TARGETS",
                      schedulerMessagePayload::CLEAR_TARGETS);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_SHUTDOWN, "SHUTDOWN", schedulerMessagePayload::SHUTDOWN);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_STARTUP, "STARTUP", schedulerMessagePayload::STARTUP);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_ADD_TARGETS,
                      "ADD TARGETS",
                      schedulerMessagePayload::ADD_TARGETS);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_UPDATE_TARGETS,
                      "UPDATE TARGETS",
                      schedulerMessagePayload::UPDATE_TARGETS);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_UPDATE_RESERVES,
                      "UPDATE RESERVES",
                      schedulerMessagePayload::UPDATE_RESERVES);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_UPDATE_REGULATION_RESERVE,
                      "UPDATE REGULATION RESERVE",
                      schedulerMessagePayload::UPDATE_REGULATION_RESERVE);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_USE_RESERVE,
                      "USE RESERVE",
                      schedulerMessagePayload::USE_RESERVE);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_UPDATE_REGULATION_TARGET,
                      "UPDATE REGULATION RESERVE",
                      schedulerMessagePayload::UPDATE_REGULATION_TARGET);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REGISTER_DISPATCHER,
                      "REGISTER DISPATCHER",
                      schedulerMessagePayload::REGISTER_DISPATCHER);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REGISTER_AGC_DISPATCHER,
                      "REGISTER AGC DISPATCHER",
                      schedulerMessagePayload::REGISTER_AGC_DISPATCHER);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REGISTER_RESERVE_DISPATCHER,
                      "REGISTER RESERVE DISPATCHER",
                      schedulerMessagePayload::REGISTER_RESERVE_DISPATCHER);
REGISTER_MESSAGE_TYPE(MESSAGE_TYPE_REGISTER_CONTROLLER,
                      "REGISTER CONTROLLER",
                      schedulerMessagePayload::REGISTER_CONTROLLER);

schedulerMessagePayload::schedulerMessagePayload(std::vector<double> time,
                                                 std::vector<double> target):
    m_time(std::move(time)), m_target(std::move(target))
{
}
void schedulerMessagePayload::loadMessage(std::vector<double> time, std::vector<double> target)
{
    m_time = std::move(time);
    m_target = std::move(target);
}

std::string schedulerMessagePayload::to_string(uint32_t type, uint32_t /*code*/) const
{
    std::string typeString;
    auto tsize = m_time.size();

    switch (type) {
        case SHUTDOWN:
        case STARTUP:
            return typeString + "@" + std::to_string(m_time[0]);
        case ADD_TARGETS:
        case UPDATE_TARGETS:
        case UPDATE_RESERVES:
        case UPDATE_REGULATION_RESERVE:
        case USE_RESERVE:
        case UPDATE_REGULATION_TARGET:
            return typeString + makeTargetString(tsize);
        default:
            break;
    }
    return typeString;
}

void schedulerMessagePayload::from_string(uint32_t type,
                                          uint32_t /*code*/,
                                          std::string_view fromString,
                                          size_t offset)
{
    std::vector<double> targets;
    if (fromString.size() - offset > 1) {
        targets =
            gmlc::utilities::str2vector(std::string{fromString.substr(offset)}, kNullVal, "@ ");
    }

    auto loadTargets = [this](std::vector<double> newTargets) {
        auto dvs = newTargets.size() / 2;
        m_time.resize(dvs);
        m_target.resize(dvs);
        for (size_t kk = 0; kk < newTargets.size() - 1; kk += 2) {
            m_target[kk / 2] = newTargets[kk];
            m_time[(kk / 2) + 1] = newTargets[kk + 1];
        }
    };
    switch (type) {
        case SHUTDOWN:
        case STARTUP:
            m_time.resize(1);
            m_time[0] = targets[0];
            break;
        case ADD_TARGETS:
        case UPDATE_TARGETS:
        case UPDATE_RESERVES:
        case USE_RESERVE:
        case UPDATE_REGULATION_TARGET:
            loadTargets(targets);
            break;
        default:
            break;
    }
}

std::string schedulerMessagePayload::makeTargetString(size_t cnt) const
{
    std::string targetString;
    for (size_t kk = 0; kk < cnt; ++kk) {
        targetString += ((kk == 0) ? "" : " ") + std::to_string(m_target[kk]) + '@' +
            std::to_string(m_time[kk]);
    }
    return targetString;
}

}  // namespace griddyn::comms
