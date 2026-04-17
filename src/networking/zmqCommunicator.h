#pragma once

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
        zmqCommunicator(const std::string& name, std::uint64_t id);
        /** construct with id*/
        explicit zmqCommunicator(std::uint64_t id);
        /** destructor*/
        virtual ~zmqCommunicator();

        virtual std::unique_ptr<Communicator> clone() const override;

        virtual void cloneTo(Communicator* comm) const override;
        virtual void transmit(const std::string& destName,
                              std::shared_ptr<commMessage> message) override;

        virtual void transmit(std::uint64_t destID, std::shared_ptr<commMessage> message) override;

        virtual void initialize() override;

        virtual void disconnect() override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param, double val) override;
        virtual void setFlag(const std::string& flag, bool val) override;

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
        virtual void addHeader(zmq::multipart_t& msg, std::shared_ptr<commMessage>& message);
        /** add the body from a regular commMessage*/
        virtual void addMessageBody(zmq::multipart_t& msg, std::shared_ptr<commMessage>& message);
    };

}  // namespace zmqInterface
}  // namespace griddyn
