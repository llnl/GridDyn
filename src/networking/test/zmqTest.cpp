/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../../../test/gtestHelper.h"
#include "networking/dimeCollector.h"
#include "networking/zmqInterface.h"
#include "networking/zmqLibrary/zmqContextManager.h"
#include "networking/zmqLibrary/zmqHelper.h"
#include "networking/zmqLibrary/zmqReactor.h"
#include "networking/zmqLibrary/zmqSocketDescriptor.h"
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>

[[maybe_unused]] static constexpr char zmq_test_directory[] =
    GRIDDYN_TEST_DIRECTORY "/zmq_tests/";

class ZmqTests: public gridDynSimulationTestFixture, public ::testing::Test {};

using zmq::message_t;
using zmq::socket_type;
using zmqlib::socket_ops;
using zmqlib::socketTypeFromString;
using zmqlib::zmqContextManager;
using zmqlib::zmqReactor;
using zmqlib::zmqSocketDescriptor;

TEST_F(ZmqTests, LoadZmqContextManager)
{
    auto testContextManager = zmqContextManager::getContextPointer("context1");
    EXPECT_EQ(testContextManager->getName(), "context1");
    auto defaultContext = zmqContextManager::getContextPointer();
    EXPECT_TRUE(defaultContext->getName().empty());

    auto& alternativeContext = zmqContextManager::getContext("context1");
    EXPECT_EQ(&alternativeContext, &(testContextManager->getBaseContext()));
}

TEST_F(ZmqTests, TestSocketDescriptor)
{
    zmqSocketDescriptor zDescriptor("test_socket");
    zDescriptor.addOperation(socket_ops::bind, "inproc://1");

    zDescriptor.type = socket_type::pub;

    zmqSocketDescriptor zDescriptor2("test_socketr");
    zDescriptor2.addOperation(socket_ops::connect, "inproc://1");
    zDescriptor2.addOperation(socket_ops::subscribe, "test1");
    zDescriptor2.type = socketTypeFromString("sub");

    auto& defContext = zmqContextManager::getContext();
    auto sock1 = zDescriptor.makeSocket(defContext);

    auto sock2 = zDescriptor2.makeSocket(defContext);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::string mess1 = "test1:hello";
    sock1.send(mess1);

    message_t rxMessage;
    sock2.recv(rxMessage);

    EXPECT_EQ(rxMessage.size(), mess1.size());

    std::string mess2(static_cast<const char*>(rxMessage.data()), rxMessage.size());

    EXPECT_EQ(mess2, mess1);
}

TEST_F(ZmqTests, TestReactorA)
{
    int count = 0;

    auto reactor = zmqReactor::getReactorInstance("reactor1");

    zmqSocketDescriptor zDescriptor("test_socket");
    zDescriptor.addOperation(socket_ops::bind, "inproc://1");

    zDescriptor.type = socket_type::pub;
    auto& defContext = zmqContextManager::getContext();
    auto sock1 = zDescriptor.makeSocket(defContext);

    zmqSocketDescriptor zDescriptor2("test_socketr");
    zDescriptor2.addOperation(socket_ops::connect, "inproc://1");
    zDescriptor2.addOperation(socket_ops::subscribe, "test1");
    zDescriptor2.type = socketTypeFromString("sub");
    zDescriptor2.callback = [&count](const zmq::multipart_t&) { ++count; };
    reactor->addSocketBlocking(zDescriptor2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::string mess1 = "test1:hello";
    sock1.send(mess1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count, 1);

    std::string mess2 = "test2:hello";
    sock1.send(mess2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count, 1);

    zmqSocketDescriptor zDescriptorMod("test_socketr");
    zDescriptorMod.addOperation(socket_ops::subscribe, "test2");

    reactor->modifySocketBlocking(zDescriptorMod);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sock1.send(mess2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count, 2);
    reactor->closeSocket("test_socketr");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sock1.send(mess2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count, 2);
}

TEST_F(ZmqTests, TestReactorB)
{
    int count1 = 0;
    int count2 = 0;
    auto reactor = zmqReactor::getReactorInstance("reactor1");

    zmqSocketDescriptor zDescriptor("test_socket");
    zDescriptor.addOperation(socket_ops::bind, "inproc://2");

    zDescriptor.type = socket_type::pub;
    auto& defContext = zmqContextManager::getContext();
    auto sock1 = zDescriptor.makeSocket(defContext);

    zmqSocketDescriptor zDescriptor2("test_socketr1");
    zDescriptor2.addOperation(socket_ops::connect, "inproc://2");
    zDescriptor2.addOperation(socket_ops::subscribe, "test1");
    zDescriptor2.type = socketTypeFromString("sub");
    zDescriptor2.callback = [&count1](const zmq::multipart_t&) { ++count1; };
    reactor->addSocket(zDescriptor2);
    zmqSocketDescriptor zDescriptor3("test_socketr2");
    zDescriptor3.addOperation(socket_ops::connect, "inproc://2");
    zDescriptor3.addOperation(socket_ops::subscribe, "test2");
    zDescriptor3.type = socketTypeFromString("sub");
    zDescriptor3.callback = [&count2](const zmq::multipart_t&) { ++count2; };
    reactor->addSocketBlocking(zDescriptor3);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::string mess1 = "test1:hello";
    sock1.send(mess1);
    std::string mess2 = "test2:hello";
    sock1.send(mess2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);

    zmqSocketDescriptor zDescriptorMod("test_socketr1");
    zDescriptorMod.addOperation(socket_ops::subscribe, "test3");

    reactor->modifySocket(zDescriptorMod);
    zDescriptorMod.name = "test_socketr2";
    reactor->modifySocketBlocking(zDescriptorMod);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::string mess3 = "test3:hello";
    sock1.send(mess3);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count1, 2);
    EXPECT_EQ(count2, 2);

    reactor->closeSocket("test_socketr1");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sock1.send(mess3);
    sock1.send(mess2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(count1, 2);
    EXPECT_EQ(count2, 4);
}
