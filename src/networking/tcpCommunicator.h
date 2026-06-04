/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/comms/Communicator.h"
#include <bitset>
#include <memory>
#include <string>

namespace griddyn {
/** namespace containing zmq specific interface objects*/
namespace tcpLib {
    /** class implementing a general communicator to work across zmq channels*/
    class TcpCommunicator: public Communicator {
      public:
        /** default constructor*/
        TcpCommunicator() = default;
        /** construct with object name*/
        explicit TcpCommunicator(const std::string& name);
        /** construct with object name and id*/
        TcpCommunicator(const std::string& name, std::uint64_t identifier);
        /** construct with id*/
        explicit TcpCommunicator(std::uint64_t identifier);
        /** destructor*/
        virtual ~TcpCommunicator();

        virtual std::unique_ptr<Communicator> clone() const override;

        virtual void cloneTo(Communicator* comm) const override;
        virtual void transmit(std::string_view destName,
                              const std::shared_ptr<CommMessage>& message) override;

        virtual void transmit(std::uint64_t destID,
                              const std::shared_ptr<CommMessage>& message) override;

        virtual void initialize() override;

        virtual void disconnect() override;

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void set(std::string_view param, double val) override;
        virtual void setFlag(std::string_view flag, bool val) override;

      protected:
        /** enumeration flags for the communicator object*/
        enum ZmqCommFlags {
            NO_TRANSMIT_DEST = 0,  //!< flag indicating whether the communicator should include the
                                 //!< destination as the first frame
            NO_TRANSMIT_SOURCE = 1,  //!< flag indicating whether the communicator should include
                                   //!< the source in the transmission
            USE_TX_PROXY = 2,  //!< use an internal proxy NOTE:if connection and proxyAddress are
                             //!< false this will
            //!< convert to true and use the default proxy
            USE_RX_PROXY = 3,  //!< use an internal proxy NOTE:if connection and proxyAddress are
                             //!< false this will
            //!< convert to true and use the default proxy
            TX_CONNECTION_SPECIFIED = 4,  //!< indicator that the transmit connection was specified
            RX_CONNECTION_SPECIFIED = 5,  //!< indicator that the receive connection was specified

            TRANSMIT_ONLY = 6,  //!< flag indicating whether the communicator is transmit only
        };
        std::bitset<32> flags;  //!< storage for the flags
        // std::unique_ptr<zmq::socket_t> txSocket;  //!< the transmission socket
      private:
        // zmqlib::ZmqSocketDescriptor txDescriptor;  //!< socket description for transmit socket
        // zmqlib::ZmqSocketDescriptor rxDescriptor;  //!< socket description for the receive socket

        std::string proxyName;  //!< the address of the local proxy to use
        std::string contextName;  //!< the context to use

        // private functions
      protected:
        /** handle a zmq message*/
        // virtual void messageHandler(const zmq::multipart_t& msg);
        /** add a header to a message*/
        // virtual void addHeader(zmq::multipart_t &msg, std::shared_ptr<CommMessage> &message);
        /** add the body from a regular CommMessage*/
        // virtual void addMessageBody(zmq::multipart_t &msg, std::shared_ptr<CommMessage>
        // &message);
    };

}  // namespace tcpLib
}  // namespace griddyn
