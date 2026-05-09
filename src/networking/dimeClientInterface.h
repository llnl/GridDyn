/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "cppzmq/zmq_addon.hpp"
#include <exception>
#include <memory>
#include <string>

class InitFailure: public std::exception {
  public:
    InitFailure() = default;
};

class SendFailure: public std::exception {
  public:
    SendFailure() = default;
};

class DimeClientInterface {
  private:
    std::string mName;
    std::string mAddress;

  public:
    DimeClientInterface(const std::string& dimeName, const std::string& dimeAddress = "");

    ~DimeClientInterface();
    /** initialize the connection*/
    void init();
    /** close the connection*/
    void close();
    /** sync with the server*/
    void sync();
    /** send a variable to server*/
    void sendVar(const std::string& varName, double val, const std::string& recipient = "");
    void broadcast(const std::string& varName, double val);

    void getDevices();

  private:
    std::unique_ptr<zmq::socket_t> mSocket;
};
