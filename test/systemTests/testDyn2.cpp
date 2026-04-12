/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
// test case for coreObject object

#define DYN2_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/dyn_tests2/"

using namespace griddyn;

class DynamicSystemTests2: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(DynamicSystemTests2, DynTestSimpleEvent)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_2m4bDyn.xml");

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    std::vector<double> st = gds->getState();

    EXPECT_EQ(st.size(), 30u);

    gds->run();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds->getState();

    auto diff = gmlc::utilities::countDiffsIgnoreCommon(st, st2, 0.02);
    // check for stability
    EXPECT_EQ(diff, 0u);
}

TEST_F(DynamicSystemTests2, DynTestSimpleChunked)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_2m4bDyn.xml");
    simpleRunTestXML(fileName);
    std::vector<double> st = gds->getState();

    fileName = std::string(DYN2_TEST_DIRECTORY "test_2m4bDyn.xml");
    gds2 = readSimXMLFile(fileName);
    gds2->consolePrintLevel = print_level::warning;
    gds2->run(1.5);
    gds2->run(3.7);
    gds2->run(7.65896);
    gds2->run();
    ASSERT_EQ(gds2->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds2->getState();

    auto diff = gmlc::utilities::countDiffsIgnoreCommon(st, st2, 0.0001);

    EXPECT_EQ(diff, 0);
}

TEST_F(DynamicSystemTests2, DynTestRandomLoadChange)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_randLoadChange.xml");
    simpleRunTestXML(fileName);
}

TEST_F(DynamicSystemTests2, DynTestRampLoadChange)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_rampLoadChange.xml");
    simpleRunTestXML(fileName);
}

TEST_F(DynamicSystemTests2, DynTestPulseLoadChange1)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_pulseLoadChange1.xml");
    simpleRunTestXML(fileName);
}

TEST_F(DynamicSystemTests2, DynTestPulseLoadChange2)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_pulseLoadChange2.xml");
    simpleRunTestXML(fileName);
}

#ifdef LOAD_CVODE
TEST_F(DynamicSystemTests2, DynTestSinLoadChangePartCvode)
{  // using cvode
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_sineLoad_partitioned1.xml");
    simpleRunTestXML(fileName);
}

#endif
TEST_F(DynamicSystemTests2, DynTestSinLoadChangePartBasicOde)
{  // using basicode
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_sineLoad_partitioned2.xml");
    simpleRunTestXML(fileName);
}

#ifdef LOAD_ARKODE
TEST_F(DynamicSystemTests2, DynTestSinLoadChangePartArkode)
{  // using arkode
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_sineLoad_partitioned3.xml");
    simpleRunTestXML(fileName);
}

#endif

// now check if all the different solvers all produce the same results
TEST_F(DynamicSystemTests2, DynTestCompareOde)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_sineLoadChange.xml");
}

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(DynamicSystemTests2, DynTestPulseLoadChangePart)
{
    std::string fileName = std::string(DYN2_TEST_DIRECTORY "test_pulseLoadChange1_partitioned.xml");
    simpleRunTestXML(fileName);
}
#endif
