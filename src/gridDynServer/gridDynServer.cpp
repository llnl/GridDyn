/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynServer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

gridDynServer::gridDynServer()
{
    port = 4612;  // typical port number
    ip_protocol = udp;  // use udp protocol as default
    timeBase = 1000000;
    intervalError = 0.0;
    tcpacc = NULL;
    udpsock = NULL;
}

gridDynServer::~gridDynServer()
{
    if (udpsock != NULL) {
        delete udpsock;
    }
    unsigned int kk;
    for (kk = 0; kk < active_tcp_sessions.size(); kk++) {
        if (active_tcp_sessions[kk] != NULL) {
            delete active_tcp_sessions[kk];
        }
    }
    if (tcpacc != NULL) {
        delete tcpacc;
    }
}
void gridDynServer::stop_server()
{
    if (ip_protocol == udp) {
        udpsock->socket_.close();
    } else if (ip_protocol == tcp) {
        tcpacc->acceptor_.close();
        unsigned int kk;
        for (kk = 0; kk < active_tcp_sessions.size(); kk++) {
            if (active_tcp_sessions[kk] != NULL) {
                active_tcp_sessions[kk]->socket_.close();
            }
        }
        // sleep for a little while to make sure everything is shutdown
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void gridDynServer::set(std::string param, int val)
{
    std::transform(param.begin(), param.end(), param.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (param.compare("port") == 0) {
        port = val;
    } else if (param.compare("ip_protocol") == 0) {
        if ((val == 0) | (val == 1)) {
            ip_protocol = (ip_protocol_t)val;
        } else {
            std::cout << "invalid ip protocol\n";
        }
    } else if (param.compare("timeBase") == 0) {
        timeBase = val;
    } else if (param.compare("max_connections") == 0) {
        max_connections = val;
    }
}

void gridDynServer::start_server(boost::asio::io_service& ios)
{
    initialize();

    if (ip_protocol == udp) {
        printf("\n\t\t|-------------------------------------------------------|\n");
        printf("\t\t|\t\tWelcome to PMU SERVER - UDP\t\t|\n");
        printf("\t\t|-------------------------------------------------------|\n");

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
        printf("\n\t\t|-------------------------------------------------------|\n");
        printf("\t\t|\t\tWelcome to PMU SERVER - TCP\t\t|\n");
        printf("\t\t|-------------------------------------------------------|\n");

        // create a local endpoint and initialize a socket
        local_endpoint_tcp = tcp::endpoint(tcp::v4(), port);
        tcpacc = new pmu_tcp_acc(ios, local_endpoint_tcp);

        pmu_tcp_session* sess = new pmu_tcp_session(tcpacc->io_service_);
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
        printf("\n\t\t|-------------------------------------------------------|\n");
        printf("\t\t|\t\tWelcome to PMU SERVER - TCP\t\t|\n");
        printf("\t\t|-------------------------------------------------------|\n");

        if ((TCP_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) /* Create TCP socket */
        {
            perror("socket");
            exit(1);
        } else {
            printf("\n-> TCP Socket : Successfully created\n");
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
            printf("\n-> TCP Socket Bind : Successful\n");
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

        printf("\n-> TCP SERVER Listening on port: %d\n", PORT);

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
    unsigned int kk;
    auto cTime = std::chrono::system_clock::now();
    auto sTime = std::chrono::milliseconds(0);
    constexpr auto maxSleepTime = std::chrono::seconds(1);
    std::vector<int> skipped;
    bool skipped_some = false;
    bool data_sent = false;
    double accumError = 0.0;
    while (1) {
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
            for (kk = 0; kk < active_tcp_sessions.size(); kk++) {
                if (active_tcp_sessions[kk] == NULL) {
                    continue;
                }
                if (active_tcp_sessions[kk]->cstate != pmu_tcp_session::sending) {
                    continue;
                }
                if (active_tcp_sessions[kk]->send_lock.try_lock()) {
                    active_tcp_sessions[kk]->socket_.async_send(
                        boost::asio::buffer(dataFrame),
                        [session = active_tcp_sessions[kk]](const boost::system::error_code& error,
                                                            std::size_t size) {
                            session->data_sent(error, size);
                        });
                } else {
                    skipped_some = true;
                    skipped.push_back(kk);
                }
                data_sent = true;
            }
            if (skipped_some) {
                for (kk = 0; kk < skipped.size(); kk++) {
                    if (active_tcp_sessions[skipped[kk]]->send_lock.try_lock()) {
                        active_tcp_sessions[skipped[kk]]->socket_.async_send(
                            boost::asio::buffer(dataFrame),
                            [session = active_tcp_sessions[skipped[kk]]](
                                const boost::system::error_code& error, std::size_t size) {
                                session->data_sent(error, size);
                            });
                    }
                }
                skipped.resize(0);
            }
            session_lock.unlock();
            if (data_sent == false)  // nobody to send data to
            {
                break;
            }
        }

        accumError += intervalError;
        if (accumError > 1.0) {
            accumError -= 1.0;
        }
    }
    return;

} /* end of function udp_send_data() */

void gridDynServer::pmu_udp(const boost::system::error_code& error, std::size_t size)
{
    unsigned char c;
    int id_pdc;

    if (error) {
        if (error.value() == 995) {
            return;
        } else {
            std::cout << "READ ERROR: ";
            std::cout << boost::system::system_error(error).what();
            std::cout << '\n';

            return;
        }
    }

    c = recv_buffer_[1];
    c <<= 1;  // get rid of the undefined bit
    c >>= 5;  // get rid of the version number

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
            Connection::create(acceptor.io_service());*/
    if (!error) {
        // set up to read commands
        active_session->cstate = pmu_tcp_session::waiting;
        active_session->socket_.async_read_some(
            boost::asio::buffer(active_session->recv_buffer_),
            [this, active_session](const boost::system::error_code& readError, std::size_t size) {
                pmu_tcp(active_session, readError, size);
            });
        // reset to accept connections on a new socket
        pmu_tcp_session* sess = new pmu_tcp_session(tcpacc->io_service_);
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
        active_tcp_sessions[active_session->index] = NULL;
        session_lock.unlock();
        delete active_session;
    }
}

void gridDynServer::pmu_tcp(pmu_tcp_session* active_session,
                            const boost::system::error_code& error,
                            std::size_t size)
{
    unsigned char c;
    int id_pdc = 0;
    if (error) {
        active_session->cstate = pmu_tcp_session::halted;
        session_lock.lock();
        active_tcp_sessions[active_session->index] = NULL;
        session_lock.unlock();
        active_session->send_lock.lock();
        delete active_session;
        return;
    }

    c = active_session->recv_buffer_[1];
    c <<= 1;  // get rid of the undefined bit
    c >>= 5;  // get rid of the version number

    if (c == 0x04) /* Check if it is a command frame from PDC */
    {
        if (id_pdc == vid) /* Check if it is coming from authentic PDC/iPDC */
        {
            c = active_session->recv_buffer_[15];
            c = c & 0x0F;
            switch (c) {
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
            }
        }

        else /* If Received Command Frame Id code not matched */
        {
            printf(
                "\n-> Received Command Frame not from authentic PDC, ID code not matched in command frame from PDC...!!!\n");
        }
    } /* end of processing with received Command frame */

    else /* If it is other than command frame */
    {
        printf("\n-> Received Frame is not a command frame...!!!\n");
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
