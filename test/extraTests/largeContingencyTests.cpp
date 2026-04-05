/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <chrono>

#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

static const std::string contingency_test_directory(GRIDDYN_TEST_DIRECTORY "/contingency_tests/");

class LargeContingencyTests : public gridDynSimulationTestFixture, public ::testing::Test {};

using namespace boost::filesystem;
using namespace griddyn;

TEST_F(LargeContingencyTests, ContingencyN2)
{
    std::string fileName = contingency_test_directory + "contingency_test3.xml";
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    auto start_t = std::chrono::high_resolution_clock::now();
    gds->run();
    auto stop_t = std::chrono::high_resolution_clock::now();
    EXPECT_TRUE(exists("contout_N2.csv"));
    remove("contout_N2.csv");

    std::chrono::duration<double> load_time = (stop_t - start_t);
    printf("contingencies run in %f seconds\n", load_time.count());
}

TEST_F(LargeContingencyTests, ContingencyBcase)
{
    std::string fileName = contingency_test_directory + "contingency_testbig.xml";
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    auto start_t = std::chrono::high_resolution_clock::now();
    int ret = gds->run();
    auto stop_t = std::chrono::high_resolution_clock::now();
    EXPECT_EQ(ret, FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(exists("contout_N2.csv"));

    std::chrono::duration<double> load_time = (stop_t - start_t);
    printf("contingencies run in %f seconds\n", load_time.count());
}
