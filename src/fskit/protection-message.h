/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * LLNS Copyright Start
 * Copyright (c) 2016, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Lawrence Livermore National Security, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Brian M. Kelley <kelley35@llnl.gov>
 */

#ifndef PROTECTION_MESSAGE_H
#define PROTECTION_MESSAGE_H

#include "griddyn/griddyn-config.h"
#include <fskit/event-message.h>
#include <string>

#include <boost/serialization/serialization.hpp>

class ProtectionMessage: public fskit::EventMessage {
  public:
    enum MESSAGE_TYPE {
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
    ProtectionMessage(MESSAGE_TYPE t);
    virtual ~ProtectionMessage();

    MESSAGE_TYPE GetMessageType(void);

  private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const int version)
    {
        ar & m_messageType;
    }

    MESSAGE_TYPE m_messageType;
};

#endif  // PROTECTION_MESSAGE_H
