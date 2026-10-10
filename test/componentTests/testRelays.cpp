/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/GridBus.h"
#include "griddyn/comms/CommMessage.h"
#include "griddyn/comms/Communicator.h"
#include "griddyn/comms/CommunicationsCore.h"
#include "griddyn/comms/ControlMessage.h"
#include "griddyn/relays/ControlRelay.h"
#include "griddyn/relays/Pmu.h"
#include "griddyn/relays/Sensor.h"
#include "griddyn/relays/TimeOverCurrentRelay.h"
#include "griddyn/relays/ZonalRelay.h"
#include "griddyn/links/AdjustableTransformer.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

// #include <crtdbg.h>
//  test case for link objects

#define RELAY_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/relay_tests/"

using namespace griddyn;

class RelayTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST(TimeOverCurrentRelayTests, IECAndIEEECharacteristics)
{
    relays::TimeOverCurrentRelay relay("toc");
    relay.set("pickup", 1.0);
    relay.set("timedial", 1.0);

    EXPECT_NEAR(relay.operatingTime(2.0), 0.14 / (std::pow(2.0, 0.02) - 1.0), 1e-9);

    relay.set("curve", "iec_very_inverse");
    EXPECT_NEAR(relay.operatingTime(2.0), 13.5, 1e-12);

    relay.set("curve", "ieee_very_inverse");
    EXPECT_NEAR(relay.operatingTime(2.0), 19.61 / 3.0 + 0.491, 1e-9);

    relay.set("curve", "definite_time");
    relay.set("delay", 0.25);
    EXPECT_NEAR(relay.operatingTime(2.0), 0.25, 1e-12);
    EXPECT_EQ(relay.operatingTime(1.0), maxTime);
    EXPECT_EQ(relay.getString("curve"), "definite_time");
}

TEST(TimeOverCurrentRelayTests, InvalidCharacteristicIsRejected)
{
    relays::TimeOverCurrentRelay relay;
    EXPECT_THROW(relay.set("curve", "not_a_curve"), InvalidParameterValue);
}

TEST(TimeOverCurrentRelayTests, PiecewiseTimeCurrentCurveInterpolatesAndClones)
{
    relays::TimeOverCurrentRelay relay("table");
    relay.set("voltagebase", 20.0, units::kV);
    std::array<relays::TimeOverCurrentRelay::TimeCurrentPoint, 3> points{{
        {10.0, 9999.0},
        {12.0, 120.0},
        {20.0, 10.0},
    }};
    for (auto& point : points) {
        point.current *= 1000.0;
    }
    relay.setTimeCurrentCurve(points, units::A);

    EXPECT_EQ(relay.getString("curve"), "time_current_table");
    EXPECT_EQ(relay.timeCurrentCurveSize(), 3U);
    EXPECT_EQ(relay.operatingTime(relay.get("pickup", units::puA)), maxTime);
    const auto midpoint = relay.get("pickup", units::puA) * (14.0 / 10.0);
    EXPECT_NEAR(relay.operatingTime(midpoint), 92.5, 1e-12);

    auto* clone = dynamic_cast<relays::TimeOverCurrentRelay*>(relay.clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->timeCurrentCurveSize(), 3U);
    EXPECT_NEAR(clone->operatingTime(midpoint), 92.5, 1e-12);
    delete clone;
}

TEST(CommMessageSerializationTests, StringAndVectorStreamsRoundTripEquivalentBytes)
{
    CommMessage message;
    message.setMessageType(CommMessage::PING_MESSAGE_TYPE);
    message.setPayload(std::vector<char>{'A', '\0', 'B'});

    const std::string dataString = message.toDataString();
    const std::vector<char> dataVector = message.toVector();
    std::string dataStringOutput;
    std::vector<char> dataVectorOutput;
    message.toDataString(dataStringOutput);
    message.toVector(dataVectorOutput);

    EXPECT_EQ(dataString, dataStringOutput);
    EXPECT_EQ(dataVector, dataVectorOutput);
    EXPECT_EQ(std::vector<char>(dataString.begin(), dataString.end()), dataVector);
    std::vector<char> dataArray(dataVector.size());
    EXPECT_EQ(message.toByteArray(dataArray.data(), dataArray.size()),
              static_cast<int>(dataVector.size()));
    EXPECT_EQ(dataArray, dataVector);

    CommMessage fromString;
    fromString.fromDataString(dataString);
    EXPECT_EQ(fromString.toVector(), dataVector);

    CommMessage fromVector;
    fromVector.fromVector(dataVector);
    EXPECT_EQ(fromVector.toDataString(), dataString);
}

TEST(CommunicatorTests, BasicNamedTransportRoutesAndCleansUp)
{
    auto sender = makeCommunicator("", "relay_unit_sender", 0);
    auto receiver = makeCommunicator("", "relay_unit_receiver", 0);
    ASSERT_NE(sender, nullptr);
    ASSERT_NE(receiver, nullptr);

    sender->initialize();
    receiver->initialize();

    auto message = std::make_shared<CommMessage>(CommMessage::LOCAL_FAULT_EVENT);
    sender->transmit("relay_unit_receiver", message);
    ASSERT_TRUE(receiver->messagesAvailable());

    std::uint64_t source = 0;
    auto received = receiver->getMessage(source);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->getMessageType(), CommMessage::LOCAL_FAULT_EVENT);
    EXPECT_EQ(source, sender->getCommID());

    sender.reset();
    receiver.reset();
    EXPECT_EQ(CommunicationsCore::instance()->lookup("relay_unit_sender"), 0U);
    EXPECT_EQ(CommunicationsCore::instance()->lookup("relay_unit_receiver"), 0U);
}

TEST(RelaySensorTests, InvalidOutputConfigurationFailsWithoutIndexingPastVectors)
{
    Sensor sensor("invalidSensor");
    sensor.set("outputname0", std::string_view{"unbound"});
    sensor.dynInitializeA(0.0, 0);
    IOdata fields;
    EXPECT_THROW(sensor.dynInitializeB({}, {}, fields), InvalidParameterValue);
}

TEST_F(RelayTests, RelayTest1)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test1.xml");

    gds = readSimXMLFile(fileName);

    gds->dynInitialize(timeZero);

    auto* zonalRelay = dynamic_cast<relays::ZonalRelay*>(gds->getRelay(0));
    EXPECT_NE(zonalRelay, nullptr);
}

TEST_F(RelayTests, RelayTest2)
{
    // Verify the reader creates both sides of a zonal relay and its configured conditions.
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test2.xml");

    gds = readSimXMLFile(fileName);

    ASSERT_EQ(gds->dynInitialize(timeZero), 0);

    relays::ZonalRelay* Yp = dynamic_cast<relays::ZonalRelay*>(gds->getRelay(0));
    ASSERT_NE(Yp, nullptr);
    ASSERT_NE(Yp->getCondition(0), nullptr);
    ASSERT_NE(Yp->getCondition(1), nullptr);
    EXPECT_EQ(Yp->getConditionStatus(0), Relay::ConditionStatus::ACTIVE);
    EXPECT_EQ(Yp->getConditionStatus(1), Relay::ConditionStatus::ACTIVE);
    Yp = dynamic_cast<relays::ZonalRelay*>(gds->getRelay(1));
    ASSERT_NE(Yp, nullptr);
    ASSERT_NE(Yp->getCondition(0), nullptr);
    ASSERT_NE(Yp->getCondition(1), nullptr);

    auto obj = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(obj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_EQ(v.size(), 3U);
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
}

TEST_F(RelayTests, RelayTestMulti)
{
    // Verify that library relay definitions expand across multiple links.
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test_multi.xml");

    gds = readSimXMLFile(fileName);

    ASSERT_EQ(gds->dynInitialize(timeZero), 0);
    int cnt = gds->getInt("relaycount");

    EXPECT_EQ(cnt, 12);

    auto obj = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(obj->isConnected());
    EXPECT_FALSE(obj->switchTest(1));
    EXPECT_FALSE(obj->switchTest(2));
    std::vector<double> v;
    gds->getVoltage(v);

    EXPECT_EQ(v.size(), 4U);
    for (int index = 0; index < cnt; ++index) {
        auto* relay = dynamic_cast<relays::ZonalRelay*>(gds->getRelay(index));
        ASSERT_NE(relay, nullptr);
        ASSERT_NE(relay->getCondition(0), nullptr);
        ASSERT_NE(relay->getCondition(1), nullptr);
    }
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
}

TEST_F(RelayTests, RelayTestBasicCommunicationFixture)
{
    const std::string fileName = std::string(RELAY_TEST_DIRECTORY "relay_test3.xml");

    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->dynInitialize(timeZero), 0);

    auto* terminalOne = dynamic_cast<relays::ZonalRelay*>(gds->getRelay(0));
    auto* terminalTwo = dynamic_cast<relays::ZonalRelay*>(gds->getRelay(1));
    ASSERT_NE(terminalOne, nullptr);
    ASSERT_NE(terminalTwo, nullptr);
    EXPECT_NE(CommunicationsCore::instance()->lookup("bus2bus3"), 0U);
    EXPECT_NE(CommunicationsCore::instance()->lookup("bus3bus2"), 0U);
    EXPECT_NE(terminalOne->getCondition(0), nullptr);
    EXPECT_NE(terminalTwo->getCondition(0), nullptr);
}

TEST_F(RelayTests, TimeOverCurrentRelayFixture)
{
    const std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_time_overcurrent_relay.xml");

    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->dynInitialize(timeZero), 0);

    auto* relay = dynamic_cast<relays::TimeOverCurrentRelay*>(gds->getRelay(0));
    ASSERT_NE(relay, nullptr);
    EXPECT_EQ(relay->getString("curve"), "definite_time");
    EXPECT_NEAR(relay->get("pickup"), 1.0, 1e-12);
    EXPECT_NEAR(relay->get("delay"), 0.02, 1e-12);

    auto* line = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    ASSERT_NE(line, nullptr);
    EXPECT_FALSE(line->switchTest(1));
    EXPECT_LT(line->getCurrent(1), relay->get("pickup"));

    gds->run();

    EXPECT_TRUE(line->switchTest(1));
    EXPECT_EQ(relay->get("tripped"), 1.0);
}

TEST_F(RelayTests, TimeOverCurrentRelaySecondaryInstantaneousFixture)
{
    const std::string fileName =
        std::string(RELAY_TEST_DIRECTORY "test_time_overcurrent_secondary.xml");

    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->dynInitialize(timeZero), 0);

    auto* relay = dynamic_cast<relays::TimeOverCurrentRelay*>(gds->getRelay(0));
    ASSERT_NE(relay, nullptr);
    auto* load = gds->find("bus2::load2");
    ASSERT_NE(load, nullptr);
    EXPECT_TRUE(load->isEnabled());
    EXPECT_FALSE(relay->get("tripped") > 0.5);
    ASSERT_NE(relay->getCondition(1), nullptr);
    EXPECT_FALSE(relay->checkCondition(0));
    EXPECT_FALSE(relay->checkCondition(1));

    // The secondary-object path measures apparent current as S/V rather than
    // asking a load for a link-terminal current.  Change the load directly so
    // both the pickup and high-set conditions can be checked without creating
    // a discontinuity in the dynamic solver just for this reader test.
    load->set("p", 1.5);
    EXPECT_TRUE(relay->checkCondition(0));
    EXPECT_TRUE(relay->checkCondition(1));
    EXPECT_EQ(relay->triggerAction(0), ChangeCode::PARAMETER_CHANGE);

    EXPECT_FALSE(load->isEnabled());
    EXPECT_TRUE(relay->get("tripped") > 0.5);
}

TEST_F(RelayTests, DyrLoadsConstrainedTIOCR1TransformerRelay)
{
    const std::string fileName =
        std::string(RELAY_TEST_DIRECTORY "test_tiocr1_dyr.xml");
    gds = readSimXMLFile(fileName);

    auto* highSide = gds->getBus(0);
    auto* lowSide = gds->getBus(1);
    auto* transformer = dynamic_cast<links::AdjustableTransformer*>(gds->find("high_to_low"));
    ASSERT_NE(highSide, nullptr);
    ASSERT_NE(lowSide, nullptr);
    ASSERT_NE(transformer, nullptr);

    const auto dyrFile = std::filesystem::temp_directory_path() /
        ("griddyn_tiocr1_" + std::to_string(gds->getID()) + ".dyr");
    {
        std::ofstream output(dyrFile);
        ASSERT_TRUE(output.good());
        output << highSide->getUserID() << " 'TIOCR1' " << lowSide->getUserID()
               << " 1 1 1 " << lowSide->getUserID()
               << " BL " << highSide->getUserID() << " " << lowSide->getUserID()
               << " 1 " << highSide->getUserID() << " " << lowSide->getUserID()
               << " 1 " << highSide->getUserID() << " " << lowSide->getUserID()
               << " 1 10.05 9999 10.60 120 11.88 60 14.08 30 20.66 10 20.66 10"
               << " 0.05 1 /\n";
    }
    try {
        loadDyr(gds.get(), dyrFile.string(), BasicReaderInfo{});
    }
    catch (...) {
        std::filesystem::remove(dyrFile);
        throw;
    }
    std::filesystem::remove(dyrFile);

    ASSERT_EQ(gds->getInt("relaycount"), 1);
    auto* relay = dynamic_cast<relays::TimeOverCurrentRelay*>(gds->getRelay(0));
    ASSERT_NE(relay, nullptr);
    EXPECT_EQ(relay->getString("curve"), "time_current_table");
    EXPECT_EQ(relay->get("terminal"), 2.0);
    EXPECT_EQ(relay->timeCurrentCurveSize(), 6U);
    EXPECT_NEAR(relay->get("pickup", units::A), 10050.0, 1e-9);
    const auto secondPoint = relay->get("pickup", units::puA) * (10.60 / 10.05);
    EXPECT_NEAR(relay->operatingTime(secondPoint), 120.0, 1e-9);
}

TEST_F(RelayTests, DyrRejectsUnsupportedTIOCR1Variant)
{
    const std::string fileName =
        std::string(RELAY_TEST_DIRECTORY "test_tiocr1_dyr.xml");
    gds = readSimXMLFile(fileName);

    auto* highSide = gds->getBus(0);
    auto* lowSide = gds->getBus(1);
    ASSERT_NE(highSide, nullptr);
    ASSERT_NE(lowSide, nullptr);

    const auto dyrFile = std::filesystem::temp_directory_path() /
        ("griddyn_tiocr1_unsupported_" + std::to_string(gds->getID()) + ".dyr");
    {
        std::ofstream output(dyrFile);
        ASSERT_TRUE(output.good());
        output << highSide->getUserID() << " 'TIOCR1' " << lowSide->getUserID()
               << " 1 1 1 " << lowSide->getUserID()
               << " BL " << highSide->getUserID() << " " << lowSide->getUserID()
               << " 1 " << highSide->getUserID() << " " << lowSide->getUserID()
               << " 1 " << highSide->getUserID() << " " << lowSide->getUserID()
               << " 1 10.05 9999 10.60 120 11.88 60 14.08 30 20.66 10 20.66 10"
               << " 0.04 1 /\n";
    }

    EXPECT_THROW(loadDyr(gds.get(), dyrFile.string(), BasicReaderInfo{}), InvalidParameterValue);
    std::filesystem::remove(dyrFile);
}

TEST_F(RelayTests, TestBusRelay)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_bus_relay.xml");
    simpleRunTestXML(fileName);
}

TEST_F(RelayTests, TestDifferentialRelay)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_differential_relay.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::SUMMARY;
    gds->run();
    auto* obj = gds->find("bus1_to_bus3");
    ASSERT_NE(obj, nullptr);
    EXPECT_FALSE(static_cast<GridComponent*>(obj)->isConnected());
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(RelayTests, TestControlRelay)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_control_relay.xml");
    gds = readSimXMLFile(fileName);
    // gds->consolePrintLevel = PrintLevel::NO_PRINT;
    auto* obj = gds->find("bus4::load4");
    auto* controlRelay = dynamic_cast<relays::ControlRelay*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    ASSERT_NE(controlRelay, nullptr);
    gds->dynInitialize();

    auto comm = makeCommunicator("", "control", 0);
    comm->initialize();

    auto controlMessage = std::make_shared<CommMessage>(comms::ControlMessagePayload::SET);
    auto* data = controlMessage->getPayload<comms::ControlMessagePayload>();
    ASSERT_NE(data, nullptr);
    data->m_field = "P";
    data->m_value = 1.3;

    comm->transmit("cld4", controlMessage);

    EXPECT_TRUE(comm->messagesAvailable());
    std::uint64_t src;
    auto rep = comm->getMessage(src);
    ASSERT_TRUE(rep);
    EXPECT_EQ(rep->getMessageType(), comms::ControlMessagePayload::SET_SUCCESS);
    auto ldr = obj->get("p");
    EXPECT_NEAR(ldr, 1.3, (std::abs(1.3) * 1e-6) + 1e-12);
    // send a get request
    controlMessage->setMessageType(comms::ControlMessagePayload::GET);
    auto* getData = controlMessage->getPayload<comms::ControlMessagePayload>();
    ASSERT_NE(getData, nullptr);
    getData->m_field = "q";

    comm->transmit("cld4", controlMessage);
    rep = comm->getMessage(src);
    ASSERT_TRUE(rep);
    EXPECT_EQ(rep->getMessageType(), comms::ControlMessagePayload::GET_RESULT);
    EXPECT_NEAR(rep->getPayload<comms::ControlMessagePayload>()->m_value,
                0.126,
                (std::abs(0.126) * 1e-5) + 1e-12);
}

TEST_F(RelayTests, TestRelayComms)
{
    std::string fileName = std::string(RELAY_TEST_DIRECTORY "test_relay_comms.xml");
    gds = readSimXMLFile(fileName);
    // gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->dynInitialize();
    auto* obj = gds->find("sensor1");
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

    auto* pmu = dynamic_cast<relays::Pmu*>(gds->getRelay(0));
    ASSERT_NE(pmu, nullptr);

    auto* bus3 = gds->getBus(2);
    ASSERT_NE(bus3, nullptr);
    EXPECT_TRUE(isSameObject(bus3, pmu->find("target")));
    EXPECT_NEAR(bus3->getVoltage(),
                pmu->getOutput(0),
                (std::abs(pmu->getOutput(0)) * 1e-6) + 1e-12);

    EXPECT_NEAR(pmu->get("voltage"),
                pmu->getOutput(0),
                (std::abs(pmu->getOutput(0)) * 1e-6) + 1e-12);

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
