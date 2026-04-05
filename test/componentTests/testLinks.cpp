/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/events/Event.h"
#include "griddyn/gridBus.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/links/acLine.h"
#include "griddyn/simulation/diagnostics.h"
#include <gtest/gtest.h>

// #include <crtdbg.h>
//  test case for link objects

#define LINK_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/link_tests/"

using namespace griddyn;

class LinkTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(LinkTests, LinkTest1Simple)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    std::vector<double> v;
    gds->getVoltage(v);

    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.95); }));
}

TEST_F(LinkTests, LinkTestSwitches)
{
    auto B1 = new gridBus();
    auto B2 = new gridBus();
    auto L1 = std::make_unique<acLine>(0.005, 0.2);
    L1->updateBus(B1, 1);
    L1->updateBus(B2, 2);
    B2->set("angle", -0.2);
    L1->updateLocalCache();
    auto P1 = L1->getRealPower(1);
    auto Q1 = L1->getReactivePower(1);
    auto P2 = L1->getRealPower(2);
    auto Q2 = L1->getReactivePower(2);
    EXPECT_GT(P1, P2);
    EXPECT_GT(std::abs(P1), std::abs(P2));
    L1->set("fault", 0.5);
    L1->updateLocalCache();
    P1 = L1->getRealPower(1);
    Q1 = L1->getReactivePower(1);
    P2 = L1->getRealPower(2);
    Q2 = L1->getReactivePower(2);
    EXPECT_GT(P1, 0);
    EXPECT_GT(P2, 0);
    EXPECT_GT(Q1, 9.99);
    EXPECT_GT(Q2, 9.99);
    L1->set("switch1", 1);
    L1->updateLocalCache();
    P1 = L1->getRealPower(1);
    Q1 = L1->getReactivePower(1);
    P2 = L1->getRealPower(2);
    Q2 = L1->getReactivePower(2);
    EXPECT_EQ(P1, 0);
    EXPECT_GT(P2, 0);
    EXPECT_EQ(Q1, 0);
    EXPECT_GT(Q2, 9.99);
    L1->set("switch2", 1);
    L1->updateLocalCache();
    P1 = L1->getRealPower(1);
    Q1 = L1->getReactivePower(1);
    P2 = L1->getRealPower(2);
    Q2 = L1->getReactivePower(2);
    EXPECT_EQ(P1, 0);
    EXPECT_EQ(P2, 0);
    EXPECT_EQ(Q1, 0);
    EXPECT_EQ(Q2, 0);
    L1->set("fault", -1);
    L1->updateLocalCache();
    P1 = L1->getRealPower(1);
    Q1 = L1->getReactivePower(1);
    P2 = L1->getRealPower(2);
    Q2 = L1->getReactivePower(2);
    EXPECT_EQ(P1, 0);
    EXPECT_EQ(P2, 0);
    EXPECT_EQ(Q1, 0);
    EXPECT_EQ(Q2, 0);

    delete B1;
    delete B2;
}

TEST_F(LinkTests, LinkTest1Dynamic)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    auto g1 = std::make_shared<Event>();

    // this tests events as much as links here
    auto obj = gds->find("load5");
    g1->setTarget(obj, "p");
    g1->setValue(1.35);

    auto g2 = g1->clone();  // fullcopy clone
    g2->setValue(1.25);

    g1->setTime(1.0);
    g2->setTime(3.4);

    gds->add(g1);
    gds->add(std::shared_ptr<Event>(std::move(g2)));
    gds->run(0.5);
    int mmatch = runJacobianCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    gds->run(20.0);
    mmatch = runJacobianCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.95); }));

    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

// test line fault in powerflow and power flow after line fault in recovery.
TEST_F(LinkTests, LinkTestFaultPowerflow)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    gds->powerflow();

    std::vector<double> v;
    gds->getVoltage(v);

    // this tests events as much as links here
    auto obj = gds->find("bus8_to_bus9");

    obj->set("fault", 0.5);
    gds->powerflow();
    int mmatch = runJacobianCheck(gds, cPflowSolverMode);
    EXPECT_EQ(mmatch, 0);
    std::vector<double> v2;
    gds->getVoltage(v2);

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    obj->set("fault", -1.0);
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> v3;
    gds->getVoltage(v3);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.95); }));
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    auto mm = gmlc::utilities::countDiffs(v3, v, 0.0001);

    EXPECT_EQ(mm, 0u);
}

// test line fault in powerflow and power flow after line fault in recovery.
TEST_F(LinkTests, LinkTestFaultPowerflow2)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    gds->powerflow();

    std::vector<double> v;
    gds->getVoltage(v);

    // this tests events as much as links here
    auto obj = gds->find("bus2_to_bus3");

    obj->set("fault", 0.5);
    gds->powerflow();

    std::vector<double> v2;
    gds->getVoltage(v2);
    EXPECT_TRUE(std::all_of(v2.begin(), v2.end(), [](double a) { return (a > -1e-8); }));

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    obj->set("fault", -1.0);
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> v3;
    gds->getVoltage(v3);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.95); }));
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    auto mm = gmlc::utilities::countDiffs(v3, v, 0.0001);
    EXPECT_EQ(mm, 0u);
}

TEST_F(LinkTests, LinkTestFixPower)
{
    // test a bunch of different link parameters to make sure all the solve properly
    Link* a = new acLine();
    a->set("r", 0.008);
    a->set("x", 0.14);
    double v1 = 1.0;
    double a1 = 0;
    double v2 = 1.02;
    double a2 = -0.12;
    gridBus* b1 = new gridBus(v1, a1);

    gridBus* b2 = new gridBus(v2, a2);
    a->updateBus(b1, 1);
    a->updateBus(b2, 2);

    a->updateLocalCache();
    double rP1 = a->getRealPower(1);
    double qP1 = a->getReactivePower(1);
    double rP2 = a->getRealPower(2);
    double qP2 = a->getReactivePower(2);

    b2->setVoltageAngle(v2, -0.18);
    a->fixPower(rP1, qP1, 1, 1);
    EXPECT_NEAR(std::abs(a2 - b2->getAngle()), 0.0, 1e-4);

    b2->setVoltageAngle(1.05, a2);
    a->fixPower(rP1, qP1, 1, 1);
    EXPECT_NEAR(std::abs(v2 - b2->getVoltage()), 0.0, 1e-4);

    b2->setVoltageAngle(v2, -0.18);
    a->fixPower(rP2, qP2, 2, 1);
    EXPECT_NEAR(std::abs(a2 - b2->getAngle()), 0.0, 1e-4);

    b2->setVoltageAngle(1.05, a2);
    a->fixPower(rP2, qP2, 2, 1);
    EXPECT_NEAR(std::abs(v2 - b2->getVoltage()), 0.0, 1e-4);

    b1->setVoltageAngle(1.05, a1);
    a->fixPower(rP1, qP1, 1, 2);
    EXPECT_NEAR(std::abs(v1 - b1->getVoltage()), 0.0, 1e-4);

    b1->setVoltageAngle(v1, 0.02);
    a->fixPower(rP1, qP1, 1, 2);
    EXPECT_NEAR(std::abs(a1 - b1->getAngle()), 0.0, 1e-4);

    b1->setVoltageAngle(1.05, a1);
    a->fixPower(rP2, qP2, 2, 2);
    EXPECT_NEAR(std::abs(v1 - b1->getVoltage()), 0.0, 1e-4);

    b1->setVoltageAngle(v1, 0.02);
    a->fixPower(rP2, qP2, 2, 2);
    EXPECT_NEAR(std::abs(a1 - b1->getAngle()), 0.0, 1e-4);

    b1->setVoltageAngle(1.05, -0.07);
    a->fixPower(rP1, qP1, 1, 2);
    EXPECT_NEAR(std::abs(v1 - b1->getVoltage()), 0.0, 1e-4);
    EXPECT_NEAR(std::abs(a1 - b1->getAngle()), 0.0, 1e-4);

    delete a;
    delete b1;
    delete b2;
}
