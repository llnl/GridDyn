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
    std::mutex send_lock;
    boost::asio::ip::udp::socket socket_;
    pmu_udp_socket(boost::asio::io_context& ios, boost::asio::ip::udp::endpoint ep):
        socket_(ios, ep)
    {
        index = 0;
    }
    void data_sent(const boost::system::error_code& error, std::size_t size) { send_lock.unlock(); }
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
    session_state_t cstate;
    boost::asio::ip::tcp::socket socket_;
    // PMU not allowed to have multiple packets in flight at the same time
    std::mutex send_lock;

    std::vector<unsigned char> recv_buffer_;
    pmu_tcp_session(boost::asio::io_context& ios): socket_(ios)
    {
        index = 0;
        recv_buffer_.resize(65536);
        cstate = accepting;
    }
    void data_sent(const boost::system::error_code& error, std::size_t size) { send_lock.unlock(); }
};

// class for creating a virtual PMU server
class gridDynServer {
  public:
    enum ip_protocol_t { tcp = 1, udp = 2 };

    int port;
    ip_protocol_t ip_protocol;

    int timeBase;

  protected:
    int vid;
    int current_connections;  // the current number of connected sockets
    int max_connections;  // the maximum number of transmit sockets
                          // timing information

    double intervalError;

    // PMU unit itself

    // thread for the send data loop
    std::thread send_thread;

    // network connection information
    pmu_udp_socket* udpsock;
    pmu_tcp_acc* tcpacc;

    // boost::asio::io_context ioserve;
    boost::asio::ip::udp::endpoint remote_endpoint_udp;
    boost::asio::ip::udp::endpoint remote_endpoint_udp_send;
    boost::asio::ip::udp::endpoint local_endpoint_udp;

    boost::asio::ip::tcp::endpoint local_endpoint_tcp;
    std::mutex session_lock;
    std::vector<pmu_tcp_session*> active_tcp_sessions;

    // command frame buffer
    std::vector<unsigned char> recv_buffer_;

    // frame buffers for send data
    std::vector<unsigned char> dataFrame;
    std::vector<unsigned char> header;
    std::vector<unsigned char> cfg1;
    std::vector<unsigned char> cfg2;

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
