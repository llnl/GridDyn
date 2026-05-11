/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

// class for creating a udp socket for data transmission
class pmu_udp_socket {
  public:
    int index;
    // PMU not allowed to have multiple packets in flight at the same time
    std::mutex sendLock;
    boost::asio::ip::udp::socket socket_;
    pmu_udp_socket(boost::asio::io_context& ios, boost::asio::ip::udp::endpoint ep):
        socket_(ios, ep)
    {
        index = 0;
    }
    void data_sent(const boost::system::error_code& error, std::size_t size) { sendLock.unlock(); }
};

// class for establishing a TCP connection

class pmu_tcp_acc {
  public:
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::io_context& io_context_;
    pmu_tcp_acc(boost::asio::io_context& ios, boost::asio::ip::tcp::endpoint ep):
        io_context_(ios), acceptor_(ios, ep)
    {
    }
};

class pmu_tcp_session {
  public:
    int index;
    enum session_state_t { accepting = 0, waiting = 1, sending = 2, halted = 3 };
    session_state_t connectionState;
    boost::asio::ip::tcp::socket socket_;
    // PMU not allowed to have multiple packets in flight at the same time
    std::mutex sendLock;

    std::vector<unsigned char> recvBuffer;
    pmu_tcp_session(boost::asio::io_context& ios): socket_(ios)
    {
        index = 0;
        recvBuffer.resize(65536);
        connectionState = accepting;
    }
    void data_sent(const boost::system::error_code& error, std::size_t size) { sendLock.unlock(); }
};

// class for creating a virtual PMU server
class gridDynServer {
  public:
    enum ip_protocol_t { tcp = 1, udp = 2 };

    int mPort;
    ip_protocol_t mIpProtocol;

    int mTimeBase;

  protected:
    int mVid;
    int mCurrentConnections;  // the current number of connected sockets
    int mMaxConnections;  // the maximum number of transmit sockets
                          // timing information

    double mIntervalError;

    // PMU unit itself

    // thread for the send data loop
    std::thread mSendThread;

    // network connection information
    pmu_udp_socket* mUdpSocket;
    pmu_tcp_acc* mTcpAcceptor;

    // boost::asio::io_context ioserve;
    boost::asio::ip::udp::endpoint mRemoteEndpointUdp;
    boost::asio::ip::udp::endpoint mRemoteEndpointUdpSend;
    boost::asio::ip::udp::endpoint mLocalEndpointUdp;

    boost::asio::ip::tcp::endpoint mLocalEndpointTcp;
    std::mutex mSessionLock;
    std::vector<pmu_tcp_session*> mActiveTcpSessions;

    // command frame buffer
    std::vector<unsigned char> mRecvBuffer;

    // frame buffers for send data
    std::vector<unsigned char> mDataFrame;
    std::vector<unsigned char> mHeader;
    std::vector<unsigned char> mCfg1;
    std::vector<unsigned char> mCfg2;

  public:
    gridDynServer();
    ~gridDynServer();

    // threaded loop for transmitting data
    virtual void send_data();

    // function for responding to UDP requests
    virtual void pmu_udp(const boost::system::error_code& error, std::size_t size);
    // function for responding to tcp requests
    virtual void
        pmu_tcp(pmu_tcp_session* session, const boost::system::error_code& error, std::size_t size);
    // function for accepting TCP connection requests
    virtual void tcp_accept(pmu_tcp_session* session, const boost::system::error_code& error);
    // hook for executing alternate command and control functions
    virtual void command_loop() {}

    // function for starting the PMU server
    virtual void start_server(boost::asio::io_context& ios);
    // halt the server
    virtual void stop_server();
    // initialize the server
    virtual void initialize();
    // returns the PMU id
    virtual int id() { return 0; }
    // helper function to set various parameters
    virtual void set(std::string_view param, int val);
    virtual void set(std::string_view param, std::string_view val) { return; }
};
