/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/primary/infiniteBus.h"
#include "griddyn/simulation/diagnostics.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
// test case for CoreObject object

using namespace griddyn;
using namespace gmlc::utilities;

#define DYN1_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/dyn_tests1/"

class DynamicSystemTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(DynamicSystemTests, DynTestGenModel)
{
    std::string fileName = std::string(DYN1_TEST_DIRECTORY "test_dynSimple1.xml");

    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 1);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 0);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    std::vector<double> st = gds->getState(cDaeSolverMode);

    EXPECT_EQ(st.size(), 8u);
    EXPECT_NEAR(st[1], 1.0, 1e-5);  // check the voltage
    EXPECT_NEAR(st[0], 0.0, 1e-5);  // check the angle

    EXPECT_NEAR(st[5], 1.0, 1e-5);  // check the rotational speed
    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds->getState(cDaeSolverMode);

    // check for stability
    ASSERT_EQ(st.size(), st2.size());
    auto diffs = countDiffs(st, st2, 0.0001);
    EXPECT_EQ(diffs, 0u);
}

TEST_F(DynamicSystemTests, DynTestExciter)
{
    std::string fileName = std::string(DYN1_TEST_DIRECTORY "test_2m4bDyn_ss_ext_only.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    auto st = gds->getState(cDaeSolverMode);

    EXPECT_EQ(st.size(), 22u);
    if (st.size() != 22) {
        printStateNames(gds.get(), cDaeSolverMode);
    }

    runResidualCheck(gds, cDaeSolverMode);

    runJacobianCheck(gds, cDaeSolverMode);

    gds->run(0.25);
    runJacobianCheck(gds, cDaeSolverMode);

    gds->run();

    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    auto st2 = gds->getState(cDaeSolverMode);

    // check for stability
    auto diff = countDiffsIgnoreCommon(st, st2, 0.0001);
    EXPECT_EQ(diff, 0);
}

TEST_F(DynamicSystemTests, DynTestSimpleCase)
{
    std::string fileName = std::string(DYN1_TEST_DIRECTORY "test_2m4bDyn_ss.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    int count = gds->getInt("totalbuscount");

    EXPECT_EQ(count, 4);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 5);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    std::vector<double> st = gds->getState(cDaeSolverMode);

    EXPECT_EQ(st.size(), 30u);

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds->getState(cDaeSolverMode);

    auto diff = countDiffsIgnoreCommon(st, st2, 0.0001);
    EXPECT_EQ(diff, 0);
}

TEST_F(DynamicSystemTests, DynTestInfiniteBus)
{
    std::string fileName = std::string(DYN1_TEST_DIRECTORY "test_inf_bus.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);
    infiniteBus* bus = dynamic_cast<infiniteBus*>(gds->getBus(0));
    ASSERT_NE(bus, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);

    gds->powerflow();
    std::vector<double> st = gds->getState();

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds->getState(cDaeSolverMode);

    EXPECT_NEAR(st2[0], st[0], 1e-5);
    EXPECT_NEAR(st2[1], st[1], 1e-5);
}

TEST_F(DynamicSystemTests, DynTestMbase)
{
    std::string fileName = std::string(DYN1_TEST_DIRECTORY "test_dynSimple1_mod.xml");
    detailedStageCheck(fileName, gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

/*
TEST_F(DynamicSystemTests, DynTestGriddyn39)
{
    std::string fileName = std::string(DYN1_TEST_DIRECTORY "test_griddyn39.xml");
    detailedStageCheck(fileName, gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
*/

