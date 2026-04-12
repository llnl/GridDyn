/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#pragma once
#include "commMessage.h"
#include <string>

#define BASE_RELAY_MESSAGE_NUMBER 400
namespace griddyn {
namespace comms {
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

}  // namespace comms
}  // namespace griddyn
