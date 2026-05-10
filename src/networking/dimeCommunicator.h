/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "zmqCommunicator.h"
#include <memory>
#include <string>

namespace griddyn::dimeLib {
/** class implementing a communicator to interact with the DIME Communication methods*/
class dimeCommunicator: public zmqInterface::zmqCommunicator {
  public:
    dimeCommunicator();
    explicit dimeCommunicator(const std::string& name);
    dimeCommunicator(const std::string& name, std::uint64_t id);
    explicit dimeCommunicator(std::uint64_t id);

    virtual std::unique_ptr<Communicator> clone() const override;

    void cloneTo(Communicator* comm) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void set(std::string_view param, double val) override;
    virtual void setFlag(std::string_view flag, bool val) override;

  protected:
    virtual void messageHandler(const zmq::multipart_t& msg) override;
    virtual void addHeader(zmq::multipart_t& msg,
                           const std::shared_ptr<commMessage>& message) override;
    virtual void addMessageBody(zmq::multipart_t& msg,
                                const std::shared_ptr<commMessage>& message) override;

  private:
};

}  // namespace griddyn::dimeLib
