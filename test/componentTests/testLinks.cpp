/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridBus.h"
#include "griddyn/events/Event.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/ThreeWindingTransformer.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/simulation/Diagnostics.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// #include <crtdbg.h>
//  test case for link objects

#define LINK_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/link_tests/"

using namespace griddyn;

class LinkTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(LinkTests, LinkTest1Simple)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    std::vector<double> voltages;
    gds->getVoltage(voltages);

    EXPECT_TRUE(
        std::all_of(voltages.begin(), voltages.end(), [](double value) { return (value > 0.95); }));
}

TEST_F(LinkTests, ThreeWindingTransformerSolves)
{
    gds = std::make_unique<GridDynSimulation>();
    auto* source = new AcBus("source");
    auto* loadA = new AcBus("load_a");
    auto* loadB = new AcBus("load_b");
    source->set("type", "slk");
    source->set("p", 0.8);
    loadA->set("load p", 0.4);
    loadA->set("load q", 0.1);
    loadB->set("load p", 0.3);
    loadB->set("load q", 0.08);
    gds->add(source);
    gds->add(loadA);
    gds->add(loadB);

    auto* transformer = new links::ThreeWindingTransformer("three_winding");
    transformer->updateBus(source, 1);
    transformer->updateBus(loadA, 2);
    transformer->updateBus(loadB, 3);
    transformer->setWindingImpedance(1, 0.005, 0.08);
    transformer->setWindingImpedance(2, 0.005, 0.08);
    transformer->setWindingImpedance(3, 0.005, 0.08);
    gds->add(transformer);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    EXPECT_TRUE(loadA->isConnected());
    EXPECT_TRUE(loadB->isConnected());
}

TEST_F(LinkTests, LinkTestSwitches)
{
    auto* busOne = new GridBus();
    auto* busTwo = new GridBus();
    auto line = std::make_unique<AcLine>(0.005, 0.2);
    line->updateBus(busOne, 1);
    line->updateBus(busTwo, 2);
    busTwo->set("angle", -0.2);
    line->updateLocalCache();
    auto realPowerOne = line->getRealPower(1);
    auto reactivePowerOne = line->getReactivePower(1);
    auto realPowerTwo = line->getRealPower(2);
    auto reactivePowerTwo = line->getReactivePower(2);
    EXPECT_GT(realPowerOne, realPowerTwo);
    EXPECT_GT(std::abs(realPowerOne), std::abs(realPowerTwo));
    EXPECT_NE(reactivePowerOne, reactivePowerTwo);
    line->set("fault", 0.5);
    line->updateLocalCache();
    realPowerOne = line->getRealPower(1);
    reactivePowerOne = line->getReactivePower(1);
    realPowerTwo = line->getRealPower(2);
    reactivePowerTwo = line->getReactivePower(2);
    EXPECT_GT(realPowerOne, 0);
    EXPECT_GT(realPowerTwo, 0);
    EXPECT_GT(reactivePowerOne, 9.99);
    EXPECT_GT(reactivePowerTwo, 9.99);
    line->set("switch1", 1);
    line->updateLocalCache();
    realPowerOne = line->getRealPower(1);
    reactivePowerOne = line->getReactivePower(1);
    realPowerTwo = line->getRealPower(2);
    reactivePowerTwo = line->getReactivePower(2);
    EXPECT_EQ(realPowerOne, 0);
    EXPECT_GT(realPowerTwo, 0);
    EXPECT_EQ(reactivePowerOne, 0);
    EXPECT_GT(reactivePowerTwo, 9.99);
    line->set("switch2", 1);
    line->updateLocalCache();
    realPowerOne = line->getRealPower(1);
    reactivePowerOne = line->getReactivePower(1);
    realPowerTwo = line->getRealPower(2);
    reactivePowerTwo = line->getReactivePower(2);
    EXPECT_EQ(realPowerOne, 0);
    EXPECT_EQ(realPowerTwo, 0);
    EXPECT_EQ(reactivePowerOne, 0);
    EXPECT_EQ(reactivePowerTwo, 0);
    line->set("fault", -1);
    line->updateLocalCache();
    realPowerOne = line->getRealPower(1);
    reactivePowerOne = line->getReactivePower(1);
    realPowerTwo = line->getRealPower(2);
    reactivePowerTwo = line->getReactivePower(2);
    EXPECT_EQ(realPowerOne, 0);
    EXPECT_EQ(realPowerTwo, 0);
    EXPECT_EQ(reactivePowerOne, 0);
    EXPECT_EQ(reactivePowerTwo, 0);

    delete busOne;
    delete busTwo;
}

TEST_F(LinkTests, LinkTest1Dynamic)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::WARNING;
    auto eventOne = std::make_shared<Event>();

    // this tests events as much as links here
    auto obj = gds->find("load5");
    eventOne->setTarget(obj, "p");
    eventOne->setValue(1.35);

    auto eventTwo = eventOne->clone();  // fullcopy clone
    eventTwo->setValue(1.25);

    eventOne->setTime(1.0);
    eventTwo->setTime(3.4);

    gds->add(eventOne);
    gds->add(std::shared_ptr<Event>(std::move(eventTwo)));
    gds->run(0.5);
    int mmatch = runJacobianCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    gds->run(20.0);
    mmatch = runJacobianCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    std::vector<double> voltages;
    gds->getVoltage(voltages);
    EXPECT_TRUE(
        std::all_of(voltages.begin(), voltages.end(), [](double value) { return (value > 0.95); }));

    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

// test line fault in powerflow and power flow after line fault in recovery.
TEST_F(LinkTests, LinkTestFaultPowerflow)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::WARNING;
    gds->powerflow();

    std::vector<double> originalVoltages;
    gds->getVoltage(originalVoltages);

    // this tests events as much as links here
    auto obj = gds->find("bus8_to_bus9");

    obj->set("fault", 0.5);
    gds->powerflow();
    int mmatch = runJacobianCheck(gds, cPflowSolverMode);
    EXPECT_EQ(mmatch, 0);
    std::vector<double> faultVoltages;
    gds->getVoltage(faultVoltages);

    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    obj->set("fault", -1.0);
    gds->powerflow();

    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    std::vector<double> recoveredVoltages;
    gds->getVoltage(recoveredVoltages);
    EXPECT_TRUE(std::all_of(originalVoltages.begin(), originalVoltages.end(), [](double value) {
        return (value > 0.95);
    }));
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    auto mismatchCount = gmlc::utilities::countDiffs(recoveredVoltages, originalVoltages, 0.0001);

    EXPECT_EQ(mismatchCount, 0U);
}

// test line fault in powerflow and power flow after line fault in recovery.
TEST_F(LinkTests, LinkTestFaultPowerflow2)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = std::string(LINK_TEST_DIRECTORY "link_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::WARNING;
    gds->powerflow();

    std::vector<double> originalVoltages;
    gds->getVoltage(originalVoltages);

    // this tests events as much as links here
    auto obj = gds->find("bus2_to_bus3");

    obj->set("fault", 0.5);
    gds->powerflow();

    std::vector<double> faultVoltages;
    gds->getVoltage(faultVoltages);
    EXPECT_TRUE(std::all_of(faultVoltages.begin(), faultVoltages.end(), [](double value) {
        return (value > -1e-8);
    }));

    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    obj->set("fault", -1.0);
    gds->powerflow();

    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    std::vector<double> recoveredVoltages;
    gds->getVoltage(recoveredVoltages);
    EXPECT_TRUE(std::all_of(originalVoltages.begin(), originalVoltages.end(), [](double value) {
        return (value > 0.95);
    }));
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    auto mismatchCount = gmlc::utilities::countDiffs(recoveredVoltages, originalVoltages, 0.0001);
    EXPECT_EQ(mismatchCount, 0U);
}

TEST_F(LinkTests, LinkTestFixPower)
{
    // test a bunch of different link parameters to make sure all the solve properly
    auto* line = new AcLine();
    line->set("r", 0.008);
    line->set("x", 0.14);
    double voltageOne = 1.0;
    double angleOne = 0;
    double voltageTwo = 1.02;
    double angleTwo = -0.12;
    auto* busOne = new GridBus(voltageOne, angleOne);

    auto* busTwo = new GridBus(voltageTwo, angleTwo);
    line->updateBus(busOne, 1);
    line->updateBus(busTwo, 2);

    line->updateLocalCache();
    double realPowerOne = line->getRealPower(1);
    double reactivePowerOne = line->getReactivePower(1);
    double realPowerTwo = line->getRealPower(2);
    double reactivePowerTwo = line->getReactivePower(2);

    busTwo->setVoltageAngle(voltageTwo, -0.18);
    line->fixPower(realPowerOne, reactivePowerOne, 1, 1);
    EXPECT_NEAR(std::abs(angleTwo - busTwo->getAngle()), 0.0, 1e-4);

    busTwo->setVoltageAngle(1.05, angleTwo);
    line->fixPower(realPowerOne, reactivePowerOne, 1, 1);
    EXPECT_NEAR(std::abs(voltageTwo - busTwo->getVoltage()), 0.0, 1e-4);

    busTwo->setVoltageAngle(voltageTwo, -0.18);
    line->fixPower(realPowerTwo, reactivePowerTwo, 2, 1);
    EXPECT_NEAR(std::abs(angleTwo - busTwo->getAngle()), 0.0, 1e-4);

    busTwo->setVoltageAngle(1.05, angleTwo);
    line->fixPower(realPowerTwo, reactivePowerTwo, 2, 1);
    EXPECT_NEAR(std::abs(voltageTwo - busTwo->getVoltage()), 0.0, 1e-4);

    busOne->setVoltageAngle(1.05, angleOne);
    line->fixPower(realPowerOne, reactivePowerOne, 1, 2);
    EXPECT_NEAR(std::abs(voltageOne - busOne->getVoltage()), 0.0, 1e-4);

    busOne->setVoltageAngle(voltageOne, 0.02);
    line->fixPower(realPowerOne, reactivePowerOne, 1, 2);
    EXPECT_NEAR(std::abs(angleOne - busOne->getAngle()), 0.0, 1e-4);

    busOne->setVoltageAngle(1.05, angleOne);
    line->fixPower(realPowerTwo, reactivePowerTwo, 2, 2);
    EXPECT_NEAR(std::abs(voltageOne - busOne->getVoltage()), 0.0, 1e-4);

    busOne->setVoltageAngle(voltageOne, 0.02);
    line->fixPower(realPowerTwo, reactivePowerTwo, 2, 2);
    EXPECT_NEAR(std::abs(angleOne - busOne->getAngle()), 0.0, 1e-4);

    busOne->setVoltageAngle(1.05, -0.07);
    line->fixPower(realPowerOne, reactivePowerOne, 1, 2);
    EXPECT_NEAR(std::abs(voltageOne - busOne->getVoltage()), 0.0, 1e-4);
    EXPECT_NEAR(std::abs(angleOne - busOne->getAngle()), 0.0, 1e-4);

    delete line;
    delete busOne;
    delete busTwo;
}
