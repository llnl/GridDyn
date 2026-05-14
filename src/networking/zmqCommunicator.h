/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/comms/Communicator.h"
#include "zmqLibrary/zmqSocketDescriptor.h"
#include <bitset>
#include <memory>
#include <string>

namespace griddyn {
/** namespace containing zmq specific interface objects*/
namespace zmqInterface {
    /** class implementing a general communicator to work across zmq channels*/
    class zmqCommunicator: public Communicator {
      public:
        /** default constructor*/
        zmqCommunicator() = default;
        /** construct with object name*/
        explicit zmqCommunicator(const std::string& name);
        /** construct with object name and id*/
        zmqCommunicator(const std::string& name, std::uint64_t identifier);
        /** construct with id*/
        explicit zmqCommunicator(std::uint64_t identifier);
        /** destructor*/
        virtual ~zmqCommunicator();

        virtual std::unique_ptr<Communicator> clone() const override;

        virtual void cloneTo(Communicator* comm) const override;
        virtual void transmit(std::string_view destName,
                              const std::shared_ptr<commMessage>& message) override;

        virtual void transmit(std::uint64_t destID,
                              const std::shared_ptr<commMessage>& message) override;

        virtual void initialize() override;

        virtual void disconnect() override;

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void set(std::string_view param, double val) override;
        virtual void setFlag(std::string_view flag, bool val) override;

      protected:
        /** enumeration flags for the communicator object*/
        enum zmqCommFlags {
            no_transmit_dest = 0,  //!< flag indicating whether the communicator should include the
                                   //!< destination as the first frame
            no_transmit_source = 1,  //!< flag indicating whether the communicator should include
                                     //!< the source in the transmission
            use_tx_proxy = 2,  //!< use an internal proxy NOTE:if connection and proxyAddress are
                               //!< false this will convert to true and use the default proxy
            use_rx_proxy = 3,  //!< use an internal proxy NOTE:if connection and proxyAddress are
                               //!< false this will convert to true and use the default proxy
            tx_conn_specified = 4,  //!< indicator that the transmit connection was specified
            rx_conn_specified = 5,  //!< indicator that the receive connection was specified

            transmit_only = 6,  //!< flag indicating whether the communicator is transmit only
        };
        std::bitset<32> flags;  //!< storage for the flags
        std::unique_ptr<zmq::socket_t> txSocket;  //!< the transmission socket
      private:
        zmqlib::zmqSocketDescriptor txDescriptor;  //!< socket description for transmit socket
        zmqlib::zmqSocketDescriptor rxDescriptor;  //!< socket description for the receive socket

        std::string proxyName;  //!< the address of the local proxy to use
        std::string contextName;  //!< the context to use

        // private functions
      protected:
        /** handle a zmq message*/
        virtual void messageHandler(const zmq::multipart_t& msg);
        /** add a header to a message*/
        virtual void addHeader(zmq::multipart_t& msg, const std::shared_ptr<commMessage>& message);
        /** add the body from a regular commMessage*/
        virtual void addMessageBody(zmq::multipart_t& msg,
                                    const std::shared_ptr<commMessage>& message);
    };

}  // namespace zmqInterface
}  // namespace griddyn
