/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "cppzmq/zmq_addon.hpp"
#include "json/json.h"
#include <exception>
#include <memory>
#include <string>

class initFailure: public std::exception {
  public:
    initFailure() {}
}

class sendFailure: public std::exception {
  public:
    sendFailure() {}
}

class dimeClientInterface {
  private:
    std::string name;
    std::string address;

  public:
    dimeClientInterface(const std::string& dimeName, const std::string& dimeAddress = "");

    ~dimeClientInterface();
    /** initialize the connection*/
    void init();
    /** close the connection*/
    void close();
    /** sync with the server*/
    void sync();
    /** send a variable to server*/
    void send_var(const std::string& varName, double val, const std::string& recipient = "");
    void broadcast(const std::string& varName, double val);

    void get_devices();

  private:
    std::unique_ptr<zmq::socket_t> socket;
};
