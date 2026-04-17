/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/comms/Communicator.h"
#include <cstdint>
#include <fskit/event-message.h>
#include <fskit/logical-process.h>
#include <memory>
#include <string>
#include <vector>

#include <boost/serialization/serialization.hpp>

class FskitCommunicator: public griddyn::Communicator, public fskit::LogicalProcess {
  public:
    class FskitCommunicatorMessage: public fskit::EventMessage {
      public:
        FskitCommunicatorMessage() = default;

      private:
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const int version)
        {
        }
    };

    FskitCommunicator();
    FskitCommunicator(std::string name);
    FskitCommunicator(std::string m_name, std::uint64_t id);

    virtual ~FskitCommunicator() = default;

    virtual void transmit(const std::string& destName,
                          std::shared_ptr<griddyn::commMessage> message) override;

    virtual void transmit(std::uint64_t destID,
                          std::shared_ptr<griddyn::commMessage> message) override;

    void ProcessEventMessage(const fskit::EventMessage& eventMessage);
    virtual void initialize() override;  //!< XXX: Must be called by client
    virtual void disconnect() override;

  private:
    void doTransmit(std::shared_ptr<griddyn::commMessage> message);
};
