/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "commMessage.h"
#include <cereal/types/vector.hpp>
#include <string>
#include <vector>

#define BASE_SCHEDULER_MESSAGE_NUMBER 800
namespace griddyn::comms {
class schedulerMessagePayload: public CommPayload {
  public:
    enum scheduler_message_type_t : std::uint32_t {
        CLEAR_TARGETS = BASE_SCHEDULER_MESSAGE_NUMBER + 3,
        SHUTDOWN = BASE_SCHEDULER_MESSAGE_NUMBER + 4,
        STARTUP = BASE_SCHEDULER_MESSAGE_NUMBER + 5,
        ADD_TARGETS = BASE_SCHEDULER_MESSAGE_NUMBER + 6,
        UPDATE_TARGETS = BASE_SCHEDULER_MESSAGE_NUMBER + 7,
        UPDATE_RESERVES = BASE_SCHEDULER_MESSAGE_NUMBER + 8,
        UPDATE_REGULATION_RESERVE = BASE_SCHEDULER_MESSAGE_NUMBER + 9,
        USE_RESERVE = BASE_SCHEDULER_MESSAGE_NUMBER + 10,
        UPDATE_REGULATION_TARGET = BASE_SCHEDULER_MESSAGE_NUMBER + 11,
        REGISTER_DISPATCHER = BASE_SCHEDULER_MESSAGE_NUMBER + 12,
        REGISTER_AGC_DISPATCHER = BASE_SCHEDULER_MESSAGE_NUMBER + 13,
        REGISTER_RESERVE_DISPATCHER = BASE_SCHEDULER_MESSAGE_NUMBER + 14,
        REGISTER_CONTROLLER = BASE_SCHEDULER_MESSAGE_NUMBER + 15,
    };

    schedulerMessagePayload() = default;

    schedulerMessagePayload(std::vector<double> time, std::vector<double> target);

    void loadMessage(std::vector<double> time, std::vector<double> target);

    std::vector<double> m_time;
    std::vector<double> m_target;

    virtual std::string to_string(uint32_t type, uint32_t code) const override;
    virtual void from_string(uint32_t type,
                             uint32_t code,
                             const std::string& fromString,
                             size_t offset) override;

  private:
    friend class cereal::access;
    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<CommPayload>(this), m_time, m_target);
    }

    std::string makeTargetString(size_t cnt) const;
};

}  // namespace griddyn::comms

#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
CEREAL_REGISTER_TYPE(griddyn::comms::schedulerMessagePayload)
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif
