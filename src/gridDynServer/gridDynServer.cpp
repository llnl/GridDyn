/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynServer.h"
#include "gmlc/utilities/stringOps.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <print>
#include <string>
#include <vector>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

gridDynServer::gridDynServer():
    port(4612), ip_protocol(udp), timeBase(1000000), vid(0), current_connections(0),
    max_connections(0), intervalError(0.0), udpsock(nullptr), tcpacc(nullptr)
{
}

gridDynServer::~gridDynServer()
{
    delete udpsock;
    for (size_t sessionIndex = 0; sessionIndex < active_tcp_sessions.size(); ++sessionIndex) {
        if (active_tcp_sessions[sessionIndex] != nullptr) {
            delete active_tcp_sessions[sessionIndex];
        }
    }
    delete tcpacc;
}
void gridDynServer::stop_server()
{
    if (ip_protocol == udp) {
        udpsock->socket_.close();
    } else if (ip_protocol == tcp) {
        tcpacc->acceptor_.close();
        for (size_t sessionIndex = 0; sessionIndex < active_tcp_sessions.size(); ++sessionIndex) {
            if (active_tcp_sessions[sessionIndex] != nullptr) {
                active_tcp_sessions[sessionIndex]->socket_.close();
            }
        }
        // sleep for a little while to make sure everything is shutdown
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void gridDynServer::set(std::string_view param, int val)
{
    auto normalizedParam = gmlc::utilities::convertToLowerCase(param);
    if (normalizedParam == "port") {
        port = val;
    } else if (normalizedParam == "ip_protocol") {
        if ((val == 0) || (val == 1)) {
            ip_protocol = static_cast<ip_protocol_t>(val);
        } else {
            std::cout << "invalid ip protocol\n";
        }
    } else if (normalizedParam == "timebase") {
        timeBase = val;
    } else if (normalizedParam == "max_connections") {
        max_connections = val;
    }
}

void gridDynServer::start_server(boost::asio::io_context& ios)
{
    initialize();

    if (ip_protocol == udp) {
        std::println("\n\t\t|-------------------------------------------------------|");
        std::println("\t\t|\t\tWelcome to PMU SERVER - UDP\t\t|");
        std::println("\t\t|-------------------------------------------------------|");

        // create a local endpoint and initialize a socket
        local_endpoint_udp = udp::endpoint(udp::v4(), port);
        udpsock = new pmu_udp_socket(ios, local_endpoint_udp);

        // start to receive on this port
        udpsock->socket_.async_receive_from(boost::asio::buffer(recv_buffer_),
                                            remote_endpoint_udp,
                                            [this](const boost::system::error_code& error,
                                                   std::size_t size) { pmu_udp(error, size); });
        /* end of UDP protocol */
    } else {
        std::println("\n\t\t|-------------------------------------------------------|");
        std::println("\t\t|\t\tWelcome to PMU SERVER - TCP\t\t|");
        std::println("\t\t|-------------------------------------------------------|");

        // create a local endpoint and initialize a socket
        local_endpoint_tcp = tcp::endpoint(tcp::v4(), port);
        tcpacc = new pmu_tcp_acc(ios, local_endpoint_tcp);

        auto* sess = new pmu_tcp_session(tcpacc->io_context_);
        // start to accept connections on this socket
        session_lock.lock();
        sess->index = active_tcp_sessions.size();
        active_tcp_sessions.push_back(sess);
        session_lock.unlock();

        tcpacc->acceptor_.async_accept(sess->socket_,
                                       [this, sess](const boost::system::error_code& error) {
                                           tcp_accept(sess, error);
                                       });

#ifdef ENABLE_TCP
        std::println("\n\t\t|-------------------------------------------------------|");
        std::println("\t\t|\t\tWelcome to PMU SERVER - TCP\t\t|");
        std::println("\t\t|-------------------------------------------------------|");

        if ((TCP_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) /* Create TCP socket */
        {
            perror("socket");
            exit(1);
        } else {
            std::println("\n-> TCP Socket : Successfully created");
        }

        if (setsockopt(TCP_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ==
            -1) /* TCP socket */
        {
            perror("setsockopt");
            exit(1);
        }

        TCP_my_addr.sin_family = AF_INET; /* host byte order */
        TCP_my_addr.sin_port = htons(PORT); /* short, network byte order */
        TCP_my_addr.sin_addr.s_addr =
            INADDR_ANY; /* ("127.0.0.1")/ INADDR_ANY-automatically fill with my IP */
        memset(&(TCP_my_addr.sin_zero), '\0', 8); /* zero the rest of the struct */

        if (bind(TCP_sockfd, (struct sockaddr*)&TCP_my_addr, sizeof(struct sockaddr)) ==
            -1) /* bind TCP socket to port */
        {
            perror("bind");
            exit(1);
        } else {
            std::println("\n-> TCP Socket Bind : Successful");
        }

        if (listen(TCP_sockfd, BACKLOG) == -1) /* Listen on port on TCP socket */
        {
            perror("listen");
            exit(1);
        }

        /* Signal handling for TCP Connection */
        sa.sa_handler = sigchld_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(SIGCHLD, &sa, NULL) == -1) {
            perror("sigaction");
            exit(1);
        }

        std::println("\n-> TCP SERVER Listening on port: {}", PORT);

        /* Create Thread for TCP on given port default mode */
        if ((err = pthread_create(&TCP_thread, NULL, pmu_tcp, NULL))) {
            perror(strerror(err));
            exit(1);
        }
        pthread_join(TCP_thread, NULL);
        close(TCP_sockfd);
#endif
    }
} /* end of start_server() */

/* ----------------------------------------------------------------------------    */
/* FUNCTION  void* udp_send_data():                        */
/* Function to generate and send the data frame periodically to destination    */
/* address or to PDC or client.                            */
/* ----------------------------------------------------------------------------    */

void gridDynServer::send_data()
{
    auto cTime = std::chrono::system_clock::now();
    auto sTime = std::chrono::milliseconds(0);
    constexpr auto maxSleepTime = std::chrono::seconds(1);
    std::vector<int> skipped;
    bool skipped_some = false;
    bool data_sent = false;
    double accumError = 0.0;
    while (true) {
        cTime = std::chrono::system_clock::now();

        while (sTime > std::chrono::milliseconds(0)) {
            if (maxSleepTime < sTime) {
                std::this_thread::sleep_for(maxSleepTime);
            } else {
                std::this_thread::sleep_for(sTime);
            }

            cTime = std::chrono::system_clock::now();
        }

        if (ip_protocol == udp) {
            udpsock->send_lock.lock();
            udpsock->socket_.async_send_to(boost::asio::buffer(dataFrame),
                                           remote_endpoint_udp_send,
                                           [socket =
                                                udpsock](const boost::system::error_code& error,
                                                         std::size_t size) {
                                               socket->data_sent(error, size);
                                           });
        } else if (ip_protocol == tcp) {
            skipped_some = false;
            data_sent = false;
            session_lock.lock();
            for (size_t sessionIndex = 0; sessionIndex < active_tcp_sessions.size();
                 ++sessionIndex) {
                if (active_tcp_sessions[sessionIndex] == nullptr) {
                    continue;
                }
                if (active_tcp_sessions[sessionIndex]->cstate != pmu_tcp_session::sending) {
                    continue;
                }
                if (active_tcp_sessions[sessionIndex]->send_lock.try_lock()) {
                    active_tcp_sessions[sessionIndex]->socket_.async_send(
                        boost::asio::buffer(dataFrame),
                        [session = active_tcp_sessions[sessionIndex]](
                            const boost::system::error_code& error, std::size_t size) {
                            session->data_sent(error, size);
                        });
                } else {
                    skipped_some = true;
                    skipped.push_back(static_cast<int>(sessionIndex));
                }
                data_sent = true;
            }
            if (skipped_some) {
                for (size_t skipIndex = 0; skipIndex < skipped.size(); ++skipIndex) {
                    if (active_tcp_sessions[skipped[skipIndex]]->send_lock.try_lock()) {
                        active_tcp_sessions[skipped[skipIndex]]->socket_.async_send(
                            boost::asio::buffer(dataFrame),
                            [session = active_tcp_sessions[skipped[skipIndex]]](
                                const boost::system::error_code& error, std::size_t size) {
                                session->data_sent(error, size);
                            });
                    }
                }
                skipped.resize(0);
            }
            session_lock.unlock();
            if (!data_sent)  // nobody to send data to
            {
                break;
            }
        }

        accumError += intervalError;
        if (accumError > 1.0) {
            accumError -= 1.0;
        }
    }
} /* end of function udp_send_data() */

void gridDynServer::pmu_udp(const boost::system::error_code& error, std::size_t /*size*/)
{
    if (error) {
        if (error.value() == 995) {
            return;
        }
        std::cout << "READ ERROR: ";
        std::cout << boost::system::system_error(error).what();
        std::cout << '\n';
        return;
    }

    udpsock->socket_.async_receive_from(boost::asio::buffer(recv_buffer_),
                                        remote_endpoint_udp,
                                        [this](const boost::system::error_code& error,
                                               std::size_t receivedSize) {
                                            pmu_udp(error, receivedSize);
                                        });

} /* end of pmu_udp(); */
void gridDynServer::tcp_accept(pmu_tcp_session* active_session,
                               const boost::system::error_code& error)
{
    /* std::cout << "RServer::startAccept()" << '\n';
        Connection::Pointer newConn =
            Connection::create(acceptor.get_executor().context());*/
    if (!error) {
        // set up to read commands
        active_session->cstate = pmu_tcp_session::waiting;
        active_session->socket_.async_read_some(
            boost::asio::buffer(active_session->recv_buffer_),
            [this, active_session](const boost::system::error_code& readError, std::size_t size) {
                pmu_tcp(active_session, readError, size);
            });
        // reset to accept connections on a new socket
        auto* sess = new pmu_tcp_session(tcpacc->io_context_);
        session_lock.lock();
        sess->index = active_tcp_sessions.size();
        active_tcp_sessions.push_back(sess);
        session_lock.unlock();
        tcpacc->acceptor_.async_accept(sess->socket_,
                                       [this, sess](const boost::system::error_code& acceptError) {
                                           tcp_accept(sess, acceptError);
                                       });

    } else {
        session_lock.lock();
        active_tcp_sessions[active_session->index] = nullptr;
        session_lock.unlock();
        delete active_session;
    }
}

void gridDynServer::pmu_tcp(pmu_tcp_session* active_session,
                            const boost::system::error_code& error,
                            std::size_t /*size*/)
{
    unsigned int commandByte = active_session->recv_buffer_[1];
    const int id_pdc = 0;
    if (error) {
        active_session->cstate = pmu_tcp_session::halted;
        session_lock.lock();
        active_tcp_sessions[active_session->index] = nullptr;
        session_lock.unlock();
        active_session->send_lock.lock();
        delete active_session;
        return;
    }

    commandByte <<= 1U;  // get rid of the undefined bit
    commandByte >>= 5U;  // get rid of the version number

    if (commandByte == 0x04U) /* Check if it is a command frame from PDC */
    {
        if (id_pdc == vid) /* Check if it is coming from authentic PDC/iPDC */
        {
            commandByte = static_cast<unsigned int>(active_session->recv_buffer_[15]) & 0x0FU;
            switch (commandByte) {
                case 0x01: /* turn off data transmission*/
                    active_session->cstate = pmu_tcp_session::waiting;
                    break;
                case 0x02: /*turn on data transmission*/
                    active_session->cstate = pmu_tcp_session::sending;

                    send_thread = std::thread(&gridDynServer::send_data, this);

                    break;
                case 0x03: /*send header frame*/

                    active_session->send_lock.lock();
                    active_session->socket_.async_send(
                        boost::asio::buffer(header),
                        [active_session](const boost::system::error_code& sendError,
                                         std::size_t sendSize) {
                            active_session->data_sent(sendError, sendSize);
                        });
                    break;
                case 0x04: /*send config-1 frame*/

                    active_session->send_lock.lock();
                    active_session->socket_.async_send(
                        boost::asio::buffer(cfg1),
                        [active_session](const boost::system::error_code& sendError,
                                         std::size_t sendSize) {
                            active_session->data_sent(sendError, sendSize);
                        });
                    break;
                case 0x05: /*send config-2 frame*/

                    active_session->send_lock.lock();
                    active_session->socket_.async_send(
                        boost::asio::buffer(cfg2),
                        [active_session](const boost::system::error_code& sendError,
                                         std::size_t sendSize) {
                            active_session->data_sent(sendError, sendSize);
                        });
                    break;
                case 0x08: /*extended frame configuration*/
                    break;
                default:
                    break;
            }
        }

        else /* If Received Command Frame Id code not matched */
        {
            std::println(
                "\n-> Received Command Frame not from authentic PDC, ID code not matched in command frame from PDC...!!!");
        }
    } /* end of processing with received Command frame */

    else /* If it is other than command frame */
    {
        std::println("\n-> Received Frame is not a command frame...!!!");
    }
    active_session->socket_.async_read_some(
        boost::asio::buffer(active_session->recv_buffer_),
        [this, active_session](const boost::system::error_code& readError, std::size_t readSize) {
            pmu_tcp(active_session, readError, readSize);
        });

} /* end of pmu_udp(); */

void gridDynServer::initialize()
{
    // generate the appropriate sizes and place that field appropriately

    // don't do anything with the data frame yet since that gets generated on the fly

    recv_buffer_.resize(65536, 0);
}
