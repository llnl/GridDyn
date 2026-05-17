/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/comms/Communicator.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace griddyn::helicsLib {

class HelicsCoordinator;

/** class defining a Griddyn coordinator to communicate through HELICS*/
class HelicsCommunicator:
    public griddyn::Communicator

{
  public:
    HelicsCommunicator() = default;
    explicit HelicsCommunicator(const std::string& name);
    HelicsCommunicator(const std::string& name, std::uint64_t identifier);

    virtual ~HelicsCommunicator() = default;

    virtual void transmit(const std::string& destName,
                          std::shared_ptr<griddyn::commMessage> message) override;

    virtual void transmit(std::uint64_t destID,
                          std::shared_ptr<griddyn::commMessage> message) override;

    virtual void initialize() override;  //!< XXX: Must be called by client
    virtual void disconnect() override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;

  private:
    std::string target;
    std::string coordName;
    HelicsCoordinator* coord = nullptr;
    int32_t index = 0;
};

}  // namespace griddyn::helicsLib
