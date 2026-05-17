/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/griddyn-config.h"
#include <fskit/event-message.h>
#include <string>

#include <boost/serialization/serialization.hpp>

class ProtectionMessage: public fskit::EventMessage {
  public:
    enum MessageType {
        NO_EVENT,
        LOCAL_FAULT_EVENT,
        REMOTE_FAULT_EVENT,
        BREAKER_TRIP_EVENT,
        BREAKER_CLOSE_EVENT,
        BREAKER_TRIP_COMMAND,
        BREAKER_CLOSE_COMMAND,
        BREAKER_OOS_COMMAND
    };

    ProtectionMessage();
    ProtectionMessage(MessageType messageType);
    virtual ~ProtectionMessage();

    MessageType GetMessageType(void);

  private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const int version)
    {
        ar & mMessageType;
    }

    MessageType mMessageType;
};
