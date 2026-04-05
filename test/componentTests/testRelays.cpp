/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/comms/Communicator.h"
#include "griddyn/comms/controlMessage.h"
#include "griddyn/gridBus.h"
#include "griddyn/relays/controlRelay.h"
#include "griddyn/relays/pmu.h"
#include "griddyn/relays/zonalRelay.h"
#include <gtest/gtest.h>

// #include <crtdbg.h>
//  test case for link objects

#define RELAY_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/relay_tests/"

using namespace griddyn;

class RelayTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(RelayTests, RelayTest1)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test1.xml");

    gds = readSimXMLFile(fileName);

    gds->dynInitialize(timeZero);

    auto Yp = dynamic_cast<relays::zonalRelay*>(gds->getRelay(0));
    EXPECT_NE(Yp, nullptr);
}

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(RelayTests, RelayTest2)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test2.xml");

    gds = readSimXMLFile(fileName);

    gds->dynInitialize(timeZero);

    zonalRelay* Yp = dynamic_cast<zonalRelay*>(gds->getRelay(0));
    EXPECT_NE(Yp, nullptr);
    Yp = dynamic_cast<zonalRelay*>(gds->getRelay(1));
    EXPECT_NE(Yp, nullptr);

    auto obj = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    // BOOST_REQUIRE(obj != nullptr);
    gds->run();
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(obj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);

    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(RelayTests, RelayTestMulti)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test_multi.xml");

    gds = readSimXMLFile(fileName);

    gds->dynInitialize(timeZero);
    int cnt = gds->getInt("relaycount");

    EXPECT_EQ(cnt, 12);

    auto obj = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    // BOOST_REQUIRE(obj != nullptr);
    gds->run();
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(obj->isConnected());
    EXPECT_TRUE(obj->switchTest(1));
    EXPECT_TRUE(obj->switchTest(2));
    std::vector<double> v;
    gds->getVoltage(v);

    auto ps = gds->currentProcessState();
    ASSERT_TRUE((ps == gridDynSimulation::gridState_t::DYNAMIC_COMPLETE) ||
                (ps == gridDynSimulation::gridState_t::DYNAMIC_PARTIAL));
}
#endif

TEST_F(RelayTests, TestBusRelay)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_bus_relay.xml");
    simpleRunTestXML(fileName);
}

TEST_F(RelayTests, TestDifferentialRelay)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_differential_relay.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::summary;
    gds->run();
    auto obj = gds->find("bus1_to_bus3");
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(static_cast<gridComponent*>(obj)->isConnected());
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(RelayTests, TestControlRelay)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_control_relay.xml");
    gds = readSimXMLFile(fileName);
    // gds->consolePrintLevel = print_level::no_print;
    auto obj = gds->find("bus4::load4");
    auto cr = dynamic_cast<relays::controlRelay*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    ASSERT_NE(cr, nullptr);
    gds->dynInitialize();

    auto comm = makeCommunicator("", "control", 0);
    comm->initialize();

    auto cm = std::make_shared<commMessage>(comms::controlMessagePayload::SET);
    auto data = cm->getPayload<comms::controlMessagePayload>();
    ASSERT_NE(data, nullptr);
    data->m_field = "P";
    data->m_value = 1.3;

    comm->transmit("cld4", cm);

    EXPECT_TRUE(comm->messagesAvailable());
    std::uint64_t src;
    auto rep = comm->getMessage(src);
    ASSERT_TRUE(rep);
    EXPECT_EQ(rep->getMessageType(), comms::controlMessagePayload::SET_SUCCESS);
    auto ldr = obj->get("p");
    EXPECT_NEAR(ldr, 1.3, std::abs(1.3) * 1e-6 + 1e-12);
    // send a get request
    cm->setMessageType(comms::controlMessagePayload::GET);
    cm->getPayload<comms::controlMessagePayload>()->m_field = "q";

    comm->transmit("cld4", cm);
    rep = comm->getMessage(src);
    ASSERT_TRUE(rep);
    EXPECT_EQ(rep->getMessageType(), comms::controlMessagePayload::GET_RESULT);
    EXPECT_NEAR(rep->getPayload<comms::controlMessagePayload>()->m_value,
                0.126,
                std::abs(0.126) * 1e-5 + 1e-12);
}

TEST_F(RelayTests, TestRelayComms)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_relay_comms.xml");
    gds = readSimXMLFile(fileName);
    // gds->consolePrintLevel = print_level::no_print;
    gds->dynInitialize();
    auto obj = gds->find("sensor1");
    ASSERT_NE(obj, nullptr);
    double val = obj->get("current1");
    EXPECT_NE(val, kNullVal);
    val = obj->get("current2");
    EXPECT_NE(val, kNullVal);
    val = obj->get("voltage");
    EXPECT_NE(val, kNullVal);
    val = obj->get("angle");
    EXPECT_NE(val, kNullVal);
}

TEST_F(RelayTests, PmuTest1)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "pmu_test1.xml");

    gds = readSimXMLFile(fileName);

    gds->dynInitialize(timeZero);

    auto pmu = dynamic_cast<relays::pmu*>(gds->getRelay(0));
    ASSERT_NE(pmu, nullptr);

    auto bus3 = gds->getBus(2);
    ASSERT_NE(bus3, nullptr);
    EXPECT_TRUE(isSameObject(bus3, pmu->find("target")));
    EXPECT_NEAR(bus3->getVoltage(), pmu->getOutput(0), std::abs(pmu->getOutput(0)) * 1e-6 + 1e-12);

    EXPECT_NEAR(pmu->get("voltage"), pmu->getOutput(0), std::abs(pmu->getOutput(0)) * 1e-6 + 1e-12);

    double val = pmu->get("voltage");
    double ang = pmu->get("angle");
    double freq = pmu->get("freq");
    gds->run(20);
    double val2 = pmu->get("voltage");
    double ang2 = pmu->get("angle");
    double freq2 = pmu->get("freq");
    double rocof2 = pmu->get("rocof");
    gds->run(40);
    double val3 = pmu->get("voltage");
    double ang3 = pmu->get("angle");
    double freq3 = pmu->get("freq");
    double rocof3 = pmu->get("rocof");

    EXPECT_NE(val, val2);
    EXPECT_NE(val2, val3);
    EXPECT_GT(freq2, freq);
    EXPECT_GT(freq3, freq2);
    EXPECT_NE(ang, ang3);
    EXPECT_NE(ang, ang2);

    EXPECT_GT(rocof2, 0.0);
    EXPECT_GT(rocof3, 0.0);
}
