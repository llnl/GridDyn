/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "commMessage.h"
#include <string>

#define BASE_RELAY_MESSAGE_NUMBER 400
namespace griddyn::comms {
using relayMessage = commMessage;
enum relay_message_type_t : std::uint32_t {
    NO_EVENT = BASE_RELAY_MESSAGE_NUMBER,
    LOCAL_FAULT_EVENT = BASE_RELAY_MESSAGE_NUMBER + 3,
    REMOTE_FAULT_EVENT = BASE_RELAY_MESSAGE_NUMBER + 4,
    BREAKER_TRIP_EVENT = BASE_RELAY_MESSAGE_NUMBER + 5,
    BREAKER_CLOSE_EVENT = BASE_RELAY_MESSAGE_NUMBER + 6,
    LOCAL_FAULT_CLEARED = BASE_RELAY_MESSAGE_NUMBER + 7,
    REMOTE_FAULT_CLEARED = BASE_RELAY_MESSAGE_NUMBER + 8,
    BREAKER_TRIP_COMMAND = BASE_RELAY_MESSAGE_NUMBER + 9,
    BREAKER_CLOSE_COMMAND = BASE_RELAY_MESSAGE_NUMBER + 10,
    BREAKER_OOS_COMMAND = BASE_RELAY_MESSAGE_NUMBER + 11,
    ALARM_TRIGGER_EVENT = BASE_RELAY_MESSAGE_NUMBER + 12,
    ALARM_CLEARED_EVENT = BASE_RELAY_MESSAGE_NUMBER + 13,
};

}  // namespace griddyn::comms
