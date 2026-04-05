/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/simulation/diagnostics.h"
#include <cstdio>
#include <gtest/gtest.h>
// test case for coreObject object

static const std::string clone_test_directory = std::string(GRIDDYN_TEST_DIRECTORY "/clone_tests/");

using namespace griddyn;
using gmlc::utilities::countDiffs;

class CloneTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(CloneTests, CloningTest1)
{
    std::string fileName = clone_test_directory + "clone_test1.xml";
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::no_print;

    gds2 = std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(gds->clone()));
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds2->powerflow();
    ASSERT_EQ(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    std::vector<double> V1;
    std::vector<double> V2;
    gds->getVoltage(V1);
    gds2->getVoltage(V2);
    auto diffc = countDiffs(V1, V2, 0.0000001);
    EXPECT_EQ(diffc, 0u);
}

TEST_F(CloneTests, CloningTest2)
{
    std::string fileName = clone_test_directory + "clone_test2.xml";
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::no_print;

    gds2 = std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(gds->clone()));
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds2->powerflow();
    ASSERT_EQ(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    std::vector<double> V1;
    std::vector<double> V2;
    gds->getVoltage(V1);
    gds2->getVoltage(V2);
    auto diffc = countDiffs(V1, V2, 0.0000001);
    EXPECT_EQ(diffc, 0u);
}

/* clone test 3 has an approximation in the solver that should get cloned over to powerflow 2*/
TEST_F(CloneTests, CloningTestSolverApprox)
{
    std::string fileName = clone_test_directory + "clone_test3.xml";
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::no_print;

    gds2 = std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(gds->clone()));
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    checkState2(gridDynSimulation::gridState_t::STARTUP);
    gds2->powerflow();
    requireState2(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    std::vector<double> V1;
    std::vector<double> V2;
    gds->getVoltage(V1);
    gds2->getVoltage(V2);
    auto diffc = countDiffs(V1, V2, 0.0000001);
    EXPECT_EQ(diffc, 0u);

    auto obj = gds2->find("bus3::load3");

    auto obj2 = gds->find("bus3::load3");
    EXPECT_NE(obj->getID(), obj2->getID());
    obj->set("q+", 0.4);

    gds->powerflow();
    gds2->powerflow();

    gds2->getVoltage(V1);
    diffc = countDiffs(V1, V2, 0.0000001);
    EXPECT_GT(diffc, 0u);

    gds->getVoltage(V2);
    diffc = countDiffs(V1, V2, 0.0000001);
    EXPECT_GT(diffc, 0u);

    obj->set("q+", -0.3);
    // ensure there is no connection with the previous simulation object
    auto ret = gds2->powerflow();
    EXPECT_EQ(ret, FUNCTION_EXECUTION_SUCCESS);
}

TEST_F(CloneTests, CloningTestEvents)
{
    std::string fileName = clone_test_directory + "test_griddyn39_events.xml";
    gds = readSimXMLFile(fileName);

    gds2 = std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(gds->clone()));

    EXPECT_EQ(gds->getInt("eventcount"), gds2->getInt("eventcount"));
    EXPECT_EQ(gds->getInt("relaycount"), gds2->getInt("relaycount"));

    gds->pFlowInitialize();
    gds2->pFlowInitialize();

    std::vector<double> v1;
    std::vector<double> v2;
    auto cnt1 = gds->getVoltage(v1);
    auto cnt2 = gds2->getVoltage(v2);
    EXPECT_EQ(cnt1, cnt2);

    auto diffc = countDiffs(v1, v2, 0.0000001);
    EXPECT_EQ(diffc, 0u);

    auto res = checkObjectEquivalence(gds.get(), gds2.get(), true);
    ASSERT_TRUE(res);

    /** get the event objects and make sure they are independent*/
    std::vector<coreObject*> obj1;
    std::vector<coreObject*> obj2;
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

    cnt1 = gds->getVoltage(v1);
    cnt2 = gds2->getVoltage(v2);
    EXPECT_EQ(cnt1, cnt2);

    diffc = countDiffs(v1, v2, 0.0000001);
    EXPECT_EQ(diffc, 0u);
}
