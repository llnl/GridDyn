/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
// test case for CoreObject object

using namespace griddyn;
#define ROOTS_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/rootFinding_tests/"

class RootTests: public gridDynSimulationTestFixture, public ::testing::Test {};

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(RootTests, RootTest1)
{
    std::string fileName = std::string(ROOTS_TEST_DIRECTORY "test_roots1.xml");

    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);

    //    gds->consolePrintLevel=0;

    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->run(30);
    int alerts = gds->getInt("alertcount");

    EXPECT_EQ(alerts, 2);
    EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(RootTests, TestRampLoadChange2)
{
    std::string fileName = std::string(ROOTS_TEST_DIRECTORY "test_rampLoadChange2.xml");
    simpleRunTestXML(fileName);
}
#endif
/* TODO :: getting this working again will require finishing the updates to governors
which requires the updates to control system logic
*/
/*
TEST_F(RootTests, TestGovernorRoots)
{
  std::string fileName = std::string(ROOTS_TEST_DIRECTORY "test_gov_limit3.xml");
  gds = readSimXMLFile(fileName);
  requireState(gridDynSimulation::gridState_t::STARTUP);
  gds->consolePrintLevel = print_level::no_print;
  gds->set("recorddirectory", ROOTS_TEST_DIRECTORY);
  gds->run();
  requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);

  std::string recname = std::string(ROOTS_TEST_DIRECTORY "rootDisplay.dat");
  TimeSeriesMulti<> ts3;
  int ret = ts3.loadBinaryFile(recname);
  EXPECT_EQ(ret, 0);



  auto d= diff(ts3.data[8]);
  auto mx2 = absMax(ts3.data[9]);
  EXPECT_LT(mx2, 2.52);

  auto r1=d[1000];
  auto r2=d[3000];
  auto r3=d[5500];
  EXPECT_NEAR(r1*100.0*100.0,-2,0.01);
  EXPECT_NEAR(r2*10000.0,-1,0.005);
  EXPECT_NEAR(r3,r1,std::abs(r1)*0.015);
}
*/

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(RootTests, TestBusDisable)
{
    std::string fileName = std::string(ROOTS_TEST_DIRECTORY "test_bus_disable.xml");
    simpleRunTestXML(fileName);
}
#endif

