/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridBus.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/simulation/Diagnostics.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
// test case for CoreObject object

static const char clone_test_directory[] = GRIDDYN_TEST_DIRECTORY "/clone_tests/";

using namespace griddyn;
using gmlc::utilities::countDiffs;

class CloneTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(CloneTests, RawRemoteVoltageGroupCloneUsesItsOwnRegulatedBus)
{
    gds = std::make_unique<GridDynSimulation>();
    ASSERT_NO_THROW(loadFile(gds,
                             std::string(GRIDDYN_TEST_DIRECTORY) +
                                 "/input_tests/raw_remote_voltage_group.raw"));
    gds2 = std::unique_ptr<GridDynSimulation>(static_cast<GridDynSimulation*>(gds->clone()));
    gds = nullptr;

    auto* regulatedBus = dynamic_cast<AcBus*>(gds2->findByUserID("bus", 2));
    ASSERT_NE(regulatedBus, nullptr);
    ASSERT_EQ(gds2->pFlowInitialize(), 0);
    ASSERT_EQ(regulatedBus->voltageControlCount(), 3);
    ASSERT_EQ(gds2->powerflow(), 0);
    EXPECT_NEAR(static_cast<GridBus*>(regulatedBus)->getVoltage(), 1.04, 2e-4);
}

TEST_F(CloneTests, CloningTest1)
{
    const std::string fileName = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/two_bus_example.xml";
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;

    gds2 = std::unique_ptr<GridDynSimulation>(static_cast<GridDynSimulation*>(gds->clone()));
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds2->powerflow();
    ASSERT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    std::vector<double> originalVoltages;
    std::vector<double> clonedVoltages;
    gds->getVoltage(originalVoltages);
    gds2->getVoltage(clonedVoltages);
    auto diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_EQ(diffc, 0U);
}

TEST_F(CloneTests, CloningTest2)
{
    const std::string fileName = GRIDDYN_TEST_DIRECTORY "/dyn_tests1/test_griddyn39.xml";
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;

    gds2 = std::unique_ptr<GridDynSimulation>(static_cast<GridDynSimulation*>(gds->clone()));
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds2->powerflow();
    ASSERT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    std::vector<double> originalVoltages;
    std::vector<double> clonedVoltages;
    gds->getVoltage(originalVoltages);
    gds2->getVoltage(clonedVoltages);
    auto diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_EQ(diffc, 0U);
}

/* clone test 3 has an approximation in the solver that should get cloned over to powerflow 2*/
TEST_F(CloneTests, CloningTestSolverApprox)
{
    std::string fileName = std::string(clone_test_directory) + "clone_test3.xml";
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;

    gds2 = std::unique_ptr<GridDynSimulation>(static_cast<GridDynSimulation*>(gds->clone()));
    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    checkState2(GridDynSimulation::GridState::STARTUP);
    gds2->powerflow();
    requireState2(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    std::vector<double> originalVoltages;
    std::vector<double> clonedVoltages;
    gds->getVoltage(originalVoltages);
    gds2->getVoltage(clonedVoltages);
    auto diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_EQ(diffc, 0U);

    auto obj = gds2->find("bus3::load3");

    auto obj2 = gds->find("bus3::load3");
    EXPECT_NE(obj->getID(), obj2->getID());
    obj->set("q+", 0.4);

    gds->powerflow();
    gds2->powerflow();

    gds2->getVoltage(clonedVoltages);
    diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_GT(diffc, 0U);

    gds->getVoltage(originalVoltages);
    diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_GT(diffc, 0U);

    obj->set("q+", -0.3);
    // ensure there is no connection with the previous simulation object
    auto ret = gds2->powerflow();
    EXPECT_EQ(ret, FUNCTION_EXECUTION_SUCCESS);
}

TEST_F(CloneTests, CloningTestEvents)
{
    std::string fileName = std::string(clone_test_directory) + "test_griddyn39_events.xml";
    gds = readSimXMLFile(fileName);

    gds2 = std::unique_ptr<GridDynSimulation>(static_cast<GridDynSimulation*>(gds->clone()));

    EXPECT_EQ(gds->getInt("eventcount"), gds2->getInt("eventcount"));
    EXPECT_EQ(gds->getInt("relaycount"), gds2->getInt("relaycount"));

    gds->pFlowInitialize();
    gds2->pFlowInitialize();

    std::vector<double> originalVoltages;
    std::vector<double> clonedVoltages;
    auto cnt1 = gds->getVoltage(originalVoltages);
    auto cnt2 = gds2->getVoltage(clonedVoltages);
    EXPECT_EQ(cnt1, cnt2);

    auto diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_EQ(diffc, 0U);

    auto res = checkObjectEquivalence(gds.get(), gds2.get(), true);
    ASSERT_TRUE(res);

    /** get the event objects and make sure they are independent*/
    std::vector<CoreObject*> obj1;
    std::vector<CoreObject*> obj2;
    gds->getEventObjects(obj1);
    gds2->getEventObjects(obj2);
    ASSERT_EQ(obj1.size(), obj2.size());
    for (size_t ii = 0; ii < obj1.size(); ++ii) {
        EXPECT_NE(obj1[ii]->getID(), obj2[ii]->getID());
    }
    // check the event timing
    EXPECT_EQ(gds->getEventTime(), gds2->getEventTime());
    // run the simulations
    gds2->run();
    gds->run();
    checkStates(gds->currentProcessState(), gds2->currentProcessState());

    cnt1 = gds->getVoltage(originalVoltages);
    cnt2 = gds2->getVoltage(clonedVoltages);
    EXPECT_EQ(cnt1, cnt2);

    diffc = countDiffs(originalVoltages, clonedVoltages, 0.0000001);
    EXPECT_EQ(diffc, 0U);
}
