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

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_FSKIT_FSKITCOMMUNICATOR_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_FSKIT_FSKITCOMMUNICATOR_H_

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

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_FSKIT_FSKITCOMMUNICATOR_H_
