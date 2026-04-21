/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "schedulerMessage.h"

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
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
    schedulerPayloadFactory("scheduler");
}  // namespace

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeClearTargets,
                      "CLEAR TARGETS",
                      schedulerMessagePayload::CLEAR_TARGETS);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeShutdown, "SHUTDOWN", schedulerMessagePayload::SHUTDOWN);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeStartup, "STARTUP", schedulerMessagePayload::STARTUP);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeAddTargets, "ADD TARGETS", schedulerMessagePayload::ADD_TARGETS);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeUpdateTargets,
                      "UPDATE TARGETS",
                      schedulerMessagePayload::UPDATE_TARGETS);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeUpdateReserves,
                      "UPDATE RESERVES",
                      schedulerMessagePayload::UPDATE_RESERVES);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeUpdateRegulationReserve,
                      "UPDATE REGULATION RESERVE",
                      schedulerMessagePayload::UPDATE_REGULATION_RESERVE);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeUseReserve, "USE RESERVE", schedulerMessagePayload::USE_RESERVE);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeUpdateRegulationTarget,
                      "UPDATE REGULATION RESERVE",
                      schedulerMessagePayload::UPDATE_REGULATION_TARGET);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeRegisterDispatcher,
                      "REGISTER DISPATCHER",
                      schedulerMessagePayload::REGISTER_DISPATCHER);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeRegisterAgcDispatcher,
                      "REGISTER AGC DISPATCHER",
                      schedulerMessagePayload::REGISTER_AGC_DISPATCHER);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeRegisterReserveDispatcher,
                      "REGISTER RESERVE DISPATCHER",
                      schedulerMessagePayload::REGISTER_RESERVE_DISPATCHER);
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
REGISTER_MESSAGE_TYPE(messageTypeRegisterController,
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

    auto loadtargets = [this](std::vector<double> newTargets) {
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
            loadtargets(targets);
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
