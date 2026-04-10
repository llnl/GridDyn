/*
 * LLNS Copyright Start
 * Copyright (c) 2017, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_NETWORKING_DIMECOMMUNICATOR_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_NETWORKING_DIMECOMMUNICATOR_H_

#include "zmqCommunicator.h"
#include <memory>
#include <string>

namespace griddyn {
namespace dimeLib {
    /** class implementing a communicator to interact with the DIME Communication methods*/
    class dimeCommunicator: public zmqInterface::zmqCommunicator {
      public:
        dimeCommunicator();
        explicit dimeCommunicator(const std::string& name);
        dimeCommunicator(const std::string& name, std::uint64_t id);
        explicit dimeCommunicator(std::uint64_t id);

        virtual std::unique_ptr<Communicator> clone() const override;

        void cloneTo(Communicator* comm) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param, double val) override;
        virtual void setFlag(const std::string& flag, bool val) override;

      protected:
        virtual void messageHandler(const zmq::multipart_t& msg) override;
        virtual void addHeader(zmq::multipart_t& msg,
                               std::shared_ptr<commMessage>& message) override;
        virtual void addMessageBody(zmq::multipart_t& msg,
                                    std::shared_ptr<commMessage>& message) override;

      private:
    };

}  // namespace dimeLib
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_NETWORKING_DIMECOMMUNICATOR_H_
