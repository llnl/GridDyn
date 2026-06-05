/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/loads/ZipLoad.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// test case for CoreObject object

#include "../gtestHelper.h"

static constexpr std::string_view xmlTestDirectory{GRIDDYN_TEST_DIRECTORY "/xml_tests/"};

static std::string makeXmlTestPath(std::string_view fileName)
{
    return std::string{xmlTestDirectory} + std::string{fileName};
}
// create a test fixture that makes sure everything gets deleted properly

class XmlTests: public GridDynSimulationTestFixture, public ::testing::Test {};

using namespace griddyn;
using namespace gmlc::utilities;

TEST_F(XmlTests, XmlTest1)
{
    // test the loading of a single bus
    std::string fileName = makeXmlTestPath("test_xmltest1.xml");
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 1);
    EXPECT_EQ(readerConfig::warnCount, 0);
    EXPECT_EQ(gds->getName().compare("test1"), 0);

    GridBus* bus = gds->getBus(0);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), GridBus::BusType::SLK);
    EXPECT_EQ(bus->getAngle(), 0);
    EXPECT_EQ(bus->getVoltage(), 1.04);

    Generator* gen = bus->getGen(0);

    if (gen == nullptr) {
        return;
    }
    EXPECT_EQ(gen->getRealPower(), -0.7160);
    EXPECT_EQ(gen->getName().compare("gen1"), 0);
}

TEST_F(XmlTests, XmlTest2)
{
    // test the loading of 3 buses one of each type

    std::string fileName = makeXmlTestPath("test_xmltest2.xml");
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 4);
    EXPECT_EQ(readerConfig::warnCount, 0);

    GridBus* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), GridBus::BusType::PV);

    bus = gds->getBus(3);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), GridBus::BusType::PQ);

    GridLoad* load = bus->getLoad();
    ASSERT_NE(load, nullptr);
    if (load == nullptr) {
        return;
    }
    EXPECT_EQ(load->getRealPower(), 1.25);
    EXPECT_EQ(load->getReactivePower(), 0.5);
    EXPECT_EQ(load->getName().compare("load5"), 0);
}

TEST_F(XmlTests, XmlTest3)
{
    // testt the loading of links
    std::string fileName = makeXmlTestPath("test_xmltest3.xml");
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 9);
    EXPECT_EQ(readerConfig::warnCount, 1);
    count = gds->getInt("linkcount");
    EXPECT_EQ(count, 2);
    // test out link 1
    Link* lnk = gds->getLink(0);
    if (lnk == nullptr) {
        return;
    }

    GridBus* bus = lnk->getBus(1);
    if (bus == nullptr) {
        return;
    }
    EXPECT_EQ(bus->getName().compare("bus1"), 0);

    bus = lnk->getBus(2);
    if (bus == nullptr) {
        return;
    }
    EXPECT_EQ(bus->getName().compare("bus4"), 0);
    EXPECT_EQ(lnk->getName().compare("bus1_to_bus4"), 0);
    // test out link2
    lnk = gds->getLink(1);
    if (lnk == nullptr) {
        return;
    }

    bus = lnk->getBus(1);
    if (bus == nullptr) {
        return;
    }
    EXPECT_EQ(bus->getName().compare("bus4"), 0);

    bus = lnk->getBus(2);
    if (bus == nullptr) {
        return;
    }
    EXPECT_EQ(bus->getName().compare("bus5"), 0);
}

TEST_F(XmlTests, XmlTest4)
{
    // test the loading of a dynamic generator
    int count;

    std::string fileName = makeXmlTestPath("test_xmltest4.xml");

    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 1);
    EXPECT_EQ(readerConfig::warnCount, 0);

    GridBus* bus = gds->getBus(0);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), GridBus::BusType::SLK);

    Generator* gen = bus->getGen(0);
    ASSERT_NE(gen, nullptr);

    GenModel* genModel = nullptr;
    Governor* gov = nullptr;
    Exciter* ext = nullptr;

    CoreObject* obj = nullptr;

    obj = gen->find("exciter");
    ASSERT_NE(obj, nullptr);
    ext = dynamic_cast<Exciter*>(obj);
    ASSERT_NE(ext, nullptr);
    obj = gen->find("governor");
    ASSERT_NE(obj, nullptr);
    gov = dynamic_cast<Governor*>(obj);
    ASSERT_NE(gov, nullptr);
    obj = gen->find("genmodel");
    ASSERT_NE(obj, nullptr);
    genModel = dynamic_cast<GenModel*>(obj);
    ASSERT_NE(genModel, nullptr);
}

TEST_F(XmlTests, XmlTest5)
{
    // test the loading of a library
    int count;

    std::string fileName = makeXmlTestPath("test_xmltest5.xml");

    gds = std::make_unique<GridDynSimulation>();

    ReaderInfo readerInfo;

    loadFile(gds.get(), fileName, &readerInfo);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 1);
    EXPECT_EQ(readerConfig::warnCount, 0);

    GenModel* genModel = nullptr;
    Governor* gov = nullptr;
    Exciter* ext = nullptr;

    CoreObject* obj = nullptr;

    obj = readerInfo.findLibraryObject("ex1");
    ASSERT_NE(obj, nullptr);
    ext = dynamic_cast<Exciter*>(obj);
    ASSERT_NE(ext, nullptr);

    obj = readerInfo.findLibraryObject("gov1");
    ASSERT_NE(obj, nullptr);
    gov = dynamic_cast<Governor*>(obj);
    ASSERT_NE(gov, nullptr);

    obj = readerInfo.findLibraryObject("gm1");
    ASSERT_NE(obj, nullptr);
    genModel = dynamic_cast<GenModel*>(obj);
    ASSERT_NE(genModel, nullptr);
}

TEST_F(XmlTests, XmlTest6)
{
    // test the loading of a reference object
    int count;

    std::string fileName = makeXmlTestPath("test_xmltest6.xml");

    gds = std::make_unique<GridDynSimulation>();

    ReaderInfo readerInfo;

    loadFile(gds.get(), fileName, &readerInfo);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 1);
    EXPECT_EQ(readerConfig::warnCount, 0);

    GridBus* bus = gds->getBus(0);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), GridBus::BusType::SLK);

    Generator* gen = bus->getGen(0);
    ASSERT_NE(gen, nullptr);
    GenModel* genModel = nullptr;

    CoreObject* obj = nullptr;

    obj = gen->find("genmodel");
    ASSERT_NE(obj, nullptr);
    genModel = dynamic_cast<GenModel*>(obj);
    ASSERT_NE(genModel, nullptr);
}

TEST_F(XmlTests, XmlTest7)
{
    // test the loading of generator submodels
    int count;

    std::string fileName = makeXmlTestPath("test_xmltest7.xml");
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 1);
    EXPECT_EQ(readerConfig::warnCount, 0);

    GridBus* bus = gds->getBus(0);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->getType(), GridBus::BusType::SLK);

    Generator* gen = bus->getGen(0);
    ASSERT_NE(gen, nullptr);

    GenModel* genModel = nullptr;
    Governor* gov = nullptr;
    Exciter* ext = nullptr;

    CoreObject* obj = nullptr;

    obj = gen->find("exciter");
    ASSERT_NE(obj, nullptr);
    ext = dynamic_cast<Exciter*>(obj);
    ASSERT_NE(ext, nullptr);
    obj = gen->find("governor");
    ASSERT_NE(obj, nullptr);
    gov = dynamic_cast<Governor*>(obj);
    ASSERT_NE(gov, nullptr);
    obj = gen->find("genmodel");
    ASSERT_NE(obj, nullptr);
    genModel = dynamic_cast<GenModel*>(obj);
    ASSERT_NE(genModel, nullptr);
}

/**test case for making sure library call work exactly like full specification calls*/
TEST_F(XmlTests, XmlTestDynLib)
{
    std::string fileName = makeXmlTestPath("test_2m4bDyn.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> stateValues = gds->getState();

    fileName = makeXmlTestPath("test_2m4bDyn_lib.xml");
    gds2 = readSimXMLFile(fileName);
    gds2->consolePrintLevel = PrintLevel::NO_PRINT;
    gds2->run();
    ASSERT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds2->getState();

    auto sdiffs = countDiffs(stateValues, st2, 5e-5);

    EXPECT_EQ(sdiffs, 0U);
}

/**test case for generators and loads in the main element*/
TEST_F(XmlTests, XmlTestMainGen)
{
    std::string fileName = makeXmlTestPath("test_2m4bDyn.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> stateValues = gds->getState();

    fileName = makeXmlTestPath("test_2m4bDyn_mgen.xml");
    gds2 = readSimXMLFile(fileName);
    gds2->consolePrintLevel = PrintLevel::NO_PRINT;
    gds2->run();
    ASSERT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds2->getState();

    auto sdiffs = countDiffs(stateValues, st2, 5e-5);

    EXPECT_EQ(sdiffs, 0U);
}

/**test case for property override and object reloading*/
TEST_F(XmlTests, XmlTestReload)
{
    std::string fileName = makeXmlTestPath("test_2m4bDyn.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> stateValues = gds->getState();

    fileName = makeXmlTestPath("test_2m4bDyn_rload.xml");
    gds2 = readSimXMLFile(fileName);
    gds2->consolePrintLevel = PrintLevel::NO_PRINT;
    gds2->run();
    ASSERT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds2->getState();

    auto sdiffs = countDiffs(stateValues, st2, 5e-5);

    EXPECT_EQ(sdiffs, 0U);
}

/**test case for separate source file*/
TEST_F(XmlTests, XmlTestSource1)
{
    std::string fileName = makeXmlTestPath("test_2m4bDyn.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> stateValues = gds->getState();

    fileName = makeXmlTestPath("test_2m4bDyn_sep.xml");
    gds2 = readSimXMLFile(fileName);
    gds2->consolePrintLevel = PrintLevel::NO_PRINT;
    gds2->run();
    ASSERT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds2->getState();

    auto sdiffs = countDiffs(stateValues, st2, 5e-5);

    EXPECT_EQ(sdiffs, 0U);
}

TEST_F(XmlTests, XmlTest8)
{
    // test the loading of a Recorder

    std::string fileName = makeXmlTestPath("test_xmltest8.xml");

    gds = std::make_unique<GridDynSimulation>();

    ReaderInfo readerInfo;

    loadFile(gds.get(), fileName, &readerInfo);
    requireState(GridDynSimulation::GridState::STARTUP);
    EXPECT_EQ(readerConfig::warnCount, 0);
    EXPECT_EQ(readerInfo.collectors.size(), 1U);
}

TEST_F(XmlTests, XmlTest9)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_2m4bDyn.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> stateValues = gds->getState();

    fileName = makeXmlTestPath("test_xmltest9.xml");

    gds2 = readSimXMLFile(fileName);
    gds2->consolePrintLevel = PrintLevel::NO_PRINT;
    gds2->run();
    requireState2(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds2->getState();

    // check for equality
    ASSERT_EQ(stateValues.size(), st2.size());
    // ASSERT_LT(compare(stateValues, st2), 0.001);

    double diff = compareVec(stateValues, st2);
    EXPECT_LT(diff, 0.01);
}

TEST_F(XmlTests, TestBadXml)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_bad_xml.xml");
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds, nullptr);
    std::cout << "NOTE: this was supposed to have a failed file load to check error recovery\n";
}

TEST_F(XmlTests, TestFunctionConstants)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_function_constant.xml");
    gds = readSimXMLFile(fileName);

    GridBus* bus = gds->getBus(0);
    EXPECT_NEAR(bus->getVoltage(), 1.0, 1e-5);

    bus = gds->getBus(1);
    EXPECT_NEAR(bus->getAngle(), acos(0.1734), 1e-5);
    bus = gds->getBus(2);
    EXPECT_NEAR(bus->getVoltage(), 0.9576, 1e-5);
    bus = gds->getBus(3);
    EXPECT_NEAR(bus->getVoltage(), 1.1, 1e-5);
}

// Test case for testing the various means of setting parameters
TEST_F(XmlTests, TestParamSpecs)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_param_setting.xml");
    gds = readSimXMLFile(fileName);

    auto bus = gds->getBus(0);
    ASSERT_NE(bus, nullptr);
    auto ld1 = bus->getLoad(0);
    ASSERT_NE(ld1, nullptr);
    auto ld2 = bus->getLoad(1);
    ASSERT_NE(ld2, nullptr);

    EXPECT_NEAR(ld1->get("p"), 0.4, 1e-4);
    EXPECT_NEAR(ld1->get("q"), 0.3, 1e-4);
    EXPECT_NEAR(ld1->get("ip"), 0.55, 1e-4);
    EXPECT_NEAR(ld1->get("iq"), 0.32, 1e-4);
    EXPECT_NEAR(ld1->get("yp"), 0.5, 1e-4);
    EXPECT_NEAR(ld1->get("yq"), 0.11, 1e-4);

    EXPECT_NEAR(ld2->get("p"), 0.31, 1e-4);
    EXPECT_NEAR(ld2->get("q", units::MVAR), 14.8, 1e-4);
    EXPECT_NEAR(ld2->get("yp"), 1.27, 1e-4);
    EXPECT_NEAR(ld2->get("yq"), 0.74, 1e-4);
    // TODO(phlpt): This capability is not enabled yet.
    // EXPECT_NEAR(ld2->get("ip"), 0.145, 0.0001);
    // EXPECT_NEAR(ld2->get("iq"), 0.064, 0.0001);
}

// Test case for testing the various means of setting parameters
TEST_F(XmlTests, TestCustomElement1)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_custom_element1.xml");
    gds = readSimXMLFile(fileName);

    int busCount = gds->getInt("buscount");
    EXPECT_EQ(busCount, 10);
    std::vector<double> voltages;
    gds->getVoltage(voltages);
    double mxv = *std::max_element(voltages.begin(), voltages.end());
    double mnv = *std::min_element(voltages.begin(), voltages.end());
    EXPECT_LT(mnv, mxv);
}

// Test case for testing the various means of setting parameters
TEST_F(XmlTests, TestCustomElement2)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_custom_element2.xml");
    gds = readSimXMLFile(fileName);

    int busCount = gds->getInt("buscount");
    EXPECT_EQ(busCount, 7);
    std::vector<double> voltages;
    gds->getVoltage(voltages);
    double mxv = *std::max_element(voltages.begin(), voltages.end());
    double mnv = *std::min_element(voltages.begin(), voltages.end());
    EXPECT_LT(mnv, mxv);
}

// Test case for query conditions in an if element
TEST_F(XmlTests, TestQueryIf)
{
    // test the define functionality
    std::string fileName = makeXmlTestPath("test_query_if.xml");
    gds = readSimXMLFile(fileName);

    auto bus = gds->getBus(0);
    // This will show up as 2 or 0 if the conditions are not working properly
    EXPECT_EQ(bus->get("gencount"), 1);
}
