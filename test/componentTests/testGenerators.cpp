/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include "griddyn/Generator.h"
#include "griddyn/gridBus.h"
#include <gtest/gtest.h>

#define GEN_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/gen_tests/"

using namespace griddyn;

class GeneratorTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(GeneratorTests, GenTestRemote)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_remote.xml");
    detailedStageCheck(fileName, gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

TEST_F(GeneratorTests, GenTestRemoteB)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_remote_b.xml");
    detailedStageCheck(fileName, gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
}

TEST_F(GeneratorTests, GenTestRemote2)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_dualremote.xml");
    detailedStageCheck(fileName, gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

TEST_F(GeneratorTests, GenTestRemote2B)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_dualremote_b.xml");
    detailedStageCheck(fileName, gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
}

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(GeneratorTests, GenTestIsoc)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_isoc2.xml");

    gds = readSimXMLFile(fileName);

    gds->set("recorddirectory", GEN_TEST_DIRECTORY);

    gds->run();

    std::string recname = std::string(GEN_TEST_DIRECTORY "datafile.dat");
    TimeSeriesMulti<> ts3(recname);
    ASSERT_GT(ts3.size(), 30u);
    EXPECT_LT(ts3.data(0, 30), 0.995);
    EXPECT_GT(ts3[0].back(), 1.0);

    EXPECT_GT(ts3.data(1, 0) - ts3[1].back(), 0.199);
    remove(recname.c_str());
}
#endif
