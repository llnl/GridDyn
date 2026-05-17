/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "helicsTest.h"

#include "../../../test/exeTestHelper.h"
#include "../../../test/gtestHelper.h"
#include "core/objectFactory.hpp"
#include "gmlc/utilities/string_viewOps.h"
#include "griddyn/Generator.h"
#include "griddyn/gridBus.h"
#include "helics/apps/BrokerApp.hpp"
#include "helics/apps/Player.hpp"
#include "helics/apps/Recorder.hpp"
#include "helics/helicsCoordinator.h"
#include "helics/helicsLibrary.h"
#include "helics/helicsLoad.h"
#include "helics/helicsRunner.h"
#include "helics/helicsSource.h"
#include "helics/helicsSupport.h"
#include <complex>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class HelicsTests: public gridDynSimulationTestFixture, public ::testing::Test {};

using griddyn::coreObject;
using griddyn::coreObjectFactory;
using griddyn::coreTime;
using griddyn::Generator;
using griddyn::gridDynSimulationTestFixture;
using griddyn::Source;
using griddyn::helicsLib::gdToHelicsTime;
using griddyn::helicsLib::HelicsCoordinator;
using griddyn::helicsLib::HelicsLoad;
using griddyn::helicsLib::HelicsRunner;
using griddyn::helicsLib::HelicsSource;
using griddyn::helicsLib::helicsToGdTime;
using griddyn::helicsLib::runBroker;
using griddyn::helicsLib::runPlayer;
using griddyn::helicsLib::runRecorder;

static const char helics_test_directory[] = GRIDDYN_TEST_DIRECTORY "/helics_tests/";

TEST_F(HelicsTests, TimeConversionTest)
{
    coreTime val = 4.5234235;
    auto helicsTime = gdToHelicsTime(val);
    coreTime ret = helicsToGdTime(helicsTime);

    EXPECT_NEAR(static_cast<double>(val), static_cast<double>(ret), 0.0000001);

    val = 9.0;
    helicsTime = gdToHelicsTime(val);
    ret = helicsToGdTime(helicsTime);

    EXPECT_NEAR(static_cast<double>(val), static_cast<double>(ret), 0.000000001);

    val = -2.0;
    helicsTime = gdToHelicsTime(val);
    ret = helicsToGdTime(helicsTime);

    EXPECT_NEAR(static_cast<double>(val), static_cast<double>(ret), 0.000000001);
}

TEST_F(HelicsTests, TestPubSubStr)
{
    helics::FederateInfo fi;
    fi.coreType = helics::core_type::INPROC;
    fi.coreInitString = "--autobroker";

    auto vFed = std::make_shared<helics::ValueFederate>("string_test", fi);
    auto& pubid = vFed->registerGlobalPublication<std::string>("pub1");

    auto& subid = vFed->registerSubscription("pub1");
    vFed->setProperty(helics::defs::properties::period, 1.0);
    vFed->enterExecutingMode();
    vFed->publish(pubid, "string1");
    auto gtime = vFed->requestTime(1.0);

    EXPECT_EQ(gtime, 1.0);
    std::string s = subid.getValue<std::string>();

    EXPECT_EQ(s, "string1");
    vFed->publish(pubid, "string2");
    subid.getValue(s);

    EXPECT_EQ(s, "string1");
    gtime = vFed->requestTime(2.0);
    EXPECT_EQ(gtime, 2.0);
    subid.getValue(s);

    EXPECT_EQ(s, "string2");
    vFed->finalize();
}

TEST_F(HelicsTests, TestPubSubDouble)
{
    helics::FederateInfo fi;
    fi.coreType = helics::core_type::INPROC;
    fi.coreInitString = "--autobroker";

    auto vFed = std::make_shared<helics::ValueFederate>("double_test", fi);
    auto pubid = vFed->registerGlobalPublication<double>("pub1");

    auto& subid = vFed->registerSubscription("pub1");
    vFed->setProperty(helics::defs::properties::period, 1.0);
    vFed->enterExecutingMode();
    vFed->publish(pubid, 27.0);
    auto gtime = vFed->requestTime(1.0);

    EXPECT_EQ(gtime, 1.0);
    double s = subid.getValue<double>();

    EXPECT_EQ(s, 27.0);
    vFed->publish(pubid, 23.234234);
    s = vFed->getDouble(subid);
    EXPECT_EQ(s, 27.0);

    gtime = vFed->requestTime(2.0);
    EXPECT_EQ(gtime, 2.0);
    subid.getValue(s);

    EXPECT_NEAR(s, 23.234234, 0.00001);
    vFed->finalize();
}

TEST_F(HelicsTests, HelicsCoordinatorTests1)
{
    HelicsCoordinator coord;
    auto ind1 = coord.addPublication("pub1", helics::data_type::helics_double);
    auto ind2 = coord.addSubscription("pub1");
    EXPECT_GE(ind1, 0);
    EXPECT_GE(ind2, 0);
    coord.set("coretype", "inproc");
    coord.set("init", "-f 1 --autobroker");
    coord.set("name", "coordtest");

    auto fed = coord.registerAsFederate();

    EXPECT_EQ(fed->getCurrentMode(), helics::Federate::modes::initializing);
    fed->enterExecutingMode();
    EXPECT_EQ(fed->getCurrentMode(), helics::Federate::modes::executing);

    coord.publish(ind1, 23.234);
    fed->requestTime(3.0);
    double val = coord.getValueAs<double>(ind2);

    EXPECT_NEAR(val, 23.234, 0.0001);

    std::string v = coord.getValueAs<std::string>(ind2);
    EXPECT_TRUE((v.compare(0, 7, "23.2340") == 0) || (v.compare(0, 7, "23.2339") == 0));
}

TEST_F(HelicsTests, LoadHelicsXml)
{
    helics::FederateInfo fi(helics::core_type::INPROC);
    fi.coreInitString = "-f 2 --autobroker";

    auto vFed = std::make_shared<helics::ValueFederate>("source_test", fi);
    auto pubid = vFed->registerGlobalPublication<double>("sourceValue");

    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) +
                             "helics_test1.xml --core_type=inproc");

    coreObject* obj =
        coreObjectFactory::instance()->createObject("source", "helics", "helicsSource");

    ASSERT_NE(dynamic_cast<HelicsSource*>(obj), nullptr);
    auto src = static_cast<HelicsSource*>(obj);
    ASSERT_NE(src, nullptr);
    src->set("valkey", "sourceValue");
    src->set("period", 2);
    src->set("value", 0.4);
    auto sim = hR->getSim();

    auto genObj = sim->find("bus2::gen#0");
    auto gen = dynamic_cast<Generator*>(genObj);
    ASSERT_NE(gen, nullptr);

    gen->add(src);

    auto res = std::async(std::launch::async, [&]() { hR->simInitialize(); });

    vFed->enterInitializingMode();

    vFed->publish(pubid, 0.3);
    vFed->enterExecutingMode();
    res.get();

    auto resT = std::async(std::launch::async, [&]() { return hR->Step(3.0); });
    auto tm = vFed->requestTime(3.0);
    EXPECT_EQ(tm, 3.0);

    double tret = resT.get();
    EXPECT_EQ(tret, 3.0);
    EXPECT_NEAR(src->getOutput(), 0.3, 0.00001);
    vFed->publish(pubid, 0.5);

    resT = std::async(std::launch::async, [&]() { return hR->Step(5.0); });
    tm = vFed->requestTime(5.0);
    EXPECT_EQ(tm, 5.0);

    tret = resT.get();
    EXPECT_EQ(tret, 5.0);
    EXPECT_NEAR(src->getOutput(), 0.5, 0.00001);
    hR->Finalize();
    vFed->finalize();
}

TEST_F(HelicsTests, HelicsXmlWithLoad)
{
    helics::FederateInfo fi(helics::core_type::INPROC);
    fi.coreName = "test2";
    fi.coreInitString = "-f 2 --autobroker";
    auto vFed = std::make_shared<helics::ValueFederate>("helics_load_test", fi);
    auto& subid = vFed->registerSubscription("voltage3key");
    auto& pubid = vFed->registerGlobalPublication<std::complex<double>>("load3val");

    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) +
                             "helics_test2.xml --core_type=inproc --core_name=test2");

    auto sim = hR->getSim();

    auto ldObj = sim->find("bus2::load#0");
    auto ld = dynamic_cast<HelicsLoad*>(ldObj);
    ASSERT_NE(ld, nullptr);

    auto res = std::async(std::launch::async, [&]() { hR->simInitialize(); });

    vFed->enterInitializingMode();

    vFed->enterExecutingMode();
    pubid.publish(std::complex<double>{130.0, 40.0});

    res.get();

    auto resT = std::async(std::launch::async, [&]() { return hR->Step(3.0); });
    auto tm = vFed->requestTime(3.0);
    while (tm < 3.0) {
        tm = vFed->requestTime(3.0);
    }
    EXPECT_EQ(tm, 3.0);

    double tret = resT.get();
    EXPECT_EQ(tret, 3.0);
    auto voltageVal = subid.getValue<std::complex<double>>();
    pubid.publish(std::complex<double>{150.0, 70.0});

    resT = std::async(std::launch::async, [&]() { return hR->Step(7.0); });
    tm = vFed->requestTime(7.0);
    while (tm < 7.0) {
        tm = vFed->requestTime(7.0);
    }
    EXPECT_EQ(tm, 7.0);

    tret = resT.get();
    EXPECT_EQ(tret, 7.0);
    auto voltageVal2 = subid.getValue<std::complex<double>>();

    EXPECT_GT(std::abs(voltageVal), std::abs(voltageVal2));
    hR->Finalize();
    vFed->finalize();
}

TEST_F(HelicsTests, TestRecorderPlayer)
{
    auto brk = runBroker("2");
    auto play = runPlayer(std::string(helics_test_directory) +
                          "source_player.txt --name=player --stop=25 2> playerout.txt");
    auto rec = runRecorder(
        std::string(helics_test_directory) +
        "recorder_capture_list.txt --name=rec --stop=25 --output=rec_capture.txt 2> recout.txt");

    EXPECT_EQ(play.get(), 0);
    EXPECT_EQ(rec.get(), 0);

    EXPECT_EQ(brk.get(), 0);
    EXPECT_TRUE(std::filesystem::exists("rec_capture.txt"));

    std::ifstream inFile("rec_capture.txt");
    std::string line;
    std::getline(inFile, line);
    std::getline(inFile, line);
    EXPECT_FALSE(line.empty());
    auto lineEle = gmlc::utilities::string_viewOps::split(
        line,
        gmlc::utilities::string_viewOps::whiteSpaceCharacters,
        gmlc::utilities::string_viewOps::delimiter_compression::on);
    ASSERT_GE(lineEle.size(), 3U);
    EXPECT_EQ(lineEle[0], "3");
    EXPECT_EQ(lineEle[1], "gen");
    EXPECT_EQ(lineEle[2], "40");

    std::getline(inFile, line);
    std::getline(inFile, line);
    EXPECT_FALSE(line.empty());

    lineEle = gmlc::utilities::string_viewOps::split(
        line,
        gmlc::utilities::string_viewOps::whiteSpaceCharacters,
        gmlc::utilities::string_viewOps::delimiter_compression::on);
    ASSERT_GE(lineEle.size(), 3U);
    EXPECT_EQ(lineEle[0], "11");
    EXPECT_EQ(lineEle[1], "gen");
    EXPECT_EQ(lineEle[2], "50");
    inFile.close();
    remove("rec_capture.txt");
}

TEST_F(HelicsTests, TestZmqCore)
{
    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) + "helics_test3.xml");

    auto sim = hR->getSim();

    auto genObj = sim->find("bus2::gen1");
    ASSERT_NE(genObj, nullptr);
    auto src = static_cast<Source*>(genObj->find("source"));
    ASSERT_NE(src, nullptr);
    auto brk = runBroker("2");
    auto play =
        runPlayer(std::string(helics_test_directory) + "source_player.txt --name=player --stop=24");

    hR->simInitialize();

    auto tret = hR->Step(5.0);

    EXPECT_EQ(tret, 5.0);

    EXPECT_NEAR(src->getOutput(), 0.40, 0.00001);
    tret = hR->Step(9.0);
    EXPECT_NEAR(src->getOutput(), 0.45, 0.000001);
    tret = hR->Step(13.0);
    EXPECT_NEAR(src->getOutput(), 0.50, 0.00000001);
    tret = hR->Step(17.0);
    EXPECT_NEAR(src->getOutput(), 0.55, 0.00000001);
    tret = hR->Step(24.0);
    EXPECT_NEAR(src->getOutput(), 0.65, 0.00000001);

    EXPECT_EQ(play.get(), 0);
    hR->Finalize();
    sim = nullptr;
    EXPECT_EQ(brk.get(), 0);
}

TEST_F(HelicsTests, TestCollector)
{
    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) + "simple_bus_test1.xml");

    auto sim = hR->getSim();

    auto brk = helics::apps::BrokerApp("2");
    auto cmd = std::vector<std::string>{"--tags=\"mag1,mag2,ang1,ang2\""};
    auto rec = helics::apps::Recorder(cmd);

    auto fut = std::async(std::launch::async, [&rec]() { rec.runTo(250); });
    hR->simInitialize();

    auto tret = hR->Run();

    EXPECT_EQ(tret, 240.0);

    fut.get();
    hR->Finalize();
    auto pts = rec.pointCount();
    EXPECT_EQ(pts, 41 * 4);
    rec.finalize();
    hR = nullptr;
    sim = nullptr;
}

TEST_F(HelicsTests, TestCollectorVector)
{
    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) +
                             "simple_bus_test_collector_vec.xml");

    auto sim = hR->getSim();

    auto brk = helics::apps::BrokerApp("2");
    auto cmd = std::vector<std::string>{"--tags=vout"};
    auto rec = helics::apps::Recorder(cmd);

    auto fut = std::async(std::launch::async, [&rec]() { rec.runTo(250); });
    hR->simInitialize();

    auto tret = hR->Run();

    EXPECT_EQ(tret, 240.0);

    fut.get();
    hR->Finalize();
    auto pts = rec.pointCount();
    EXPECT_EQ(pts, 41);

    auto pt1 = rec.getValue(0);
    EXPECT_EQ(pt1.first, "vout");
    EXPECT_EQ(pt1.second.compare(0, 3, "v4["), 0);
    rec.finalize();
    hR = nullptr;
    sim = nullptr;
}

TEST_F(HelicsTests, TestEvent)
{
    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) + "simple_bus_test1_event.xml");

    auto sim = hR->getSim();

    auto brk = helics::apps::BrokerApp("-f 3");
    auto cmd = std::vector<std::string>{"--tags=\"mag1,mag2,ang1,ang2\""};
    auto rec = helics::apps::Recorder(cmd);

    auto play = helics::apps::Player(std::string{}, helics::FederateInfo());
    play.addPublication("breaker", helics::data_type::helics_double);
    play.addPoint(120.0, "breaker", 1.0);

    auto fut_rec = std::async(std::launch::async, [&rec]() { rec.runTo(250); });
    auto fut_play = std::async(std::launch::async, [&play]() { play.run(); });
    hR->simInitialize();

    auto tret = hR->Run();

    EXPECT_EQ(tret, 240.0);

    fut_rec.get();
    fut_play.get();
    hR->Finalize();
    auto pts = rec.pointCount();
    EXPECT_EQ(pts, 41 * 4);
    auto endpt = rec.getValue(163);
    EXPECT_EQ(gmlc::utilities::numeric_conversion(endpt.second, 45.7), 0.0);
    rec.finalize();
    hR = nullptr;
    sim = nullptr;
}

TEST_F(HelicsTests, TestVectorEvent)
{
    auto hR = std::make_unique<HelicsRunner>();
    hR->InitializeFromString(std::string(helics_test_directory) + "simple_bus_test2_event.xml");

    auto sim = hR->getSim();

    auto brk = helics::apps::BrokerApp("-f 3");
    auto cmd = std::vector<std::string>{"--tags=\"mag1,mag2,ang1,ang2\""};
    auto rec = helics::apps::Recorder(cmd);

    auto play = helics::apps::Player(std::string{}, helics::FederateInfo{});
    play.addPublication("breakers", helics::data_type::helics_vector);
    play.addPoint(120.0, "breakers", "v[0.0,1.0,0.0,0.0]");

    auto fut_rec = std::async(std::launch::async, [&rec]() { rec.runTo(250); });
    auto fut_play = std::async(std::launch::async, [&play]() { play.run(); });
    hR->simInitialize();

    auto tret = hR->Run();

    EXPECT_EQ(tret, 240.0);

    fut_rec.get();
    fut_play.get();
    hR->Finalize();
    auto pts = rec.pointCount();
    EXPECT_EQ(pts, 41 * 4);
    auto endpt = rec.getValue(163);
    EXPECT_EQ(gmlc::utilities::numeric_conversion(endpt.second, 45.7), 0.0);
    rec.finalize();
    hR = nullptr;
    sim = nullptr;
}

TEST_F(HelicsTests, TestMainExe)
{
    exeTestRunner mainExeRunner(GRIDDYNINSTALL_LOCATION, GRIDDYNMAIN_LOCATION, "gridDynMain");
    if (mainExeRunner.isActive()) {
        auto brk = runBroker("2");
        auto play = runPlayer(std::string(helics_test_directory) +
                              "source_player.txt --name=player --stop=24");
        auto out = mainExeRunner.runCaptureOutput(std::string(helics_test_directory) +
                                                  "helics_test3.xml --helics");
        auto res = out.find("HELICS");
        EXPECT_NE(res, std::string::npos);
        EXPECT_EQ(play.get(), 0);
        EXPECT_EQ(brk.get(), 0);
    } else {
        std::cout << "Unable to locate main executable:: skipping test\n";
    }
}
