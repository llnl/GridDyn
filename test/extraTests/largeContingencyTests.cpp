/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <print>
#include <string>
#include <string_view>

static constexpr std::string_view contingencyTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                           "/contingency_tests/"};

static std::string makeContingencyTestPath(std::string_view fileName)
{
    return std::string{contingencyTestDirectory} + std::string{fileName};
}

class LargeContingencyTests: public GridDynSimulationTestFixture, public ::testing::Test {};

using namespace std::filesystem;
using namespace griddyn;

TEST_F(LargeContingencyTests, ContingencyN2)
{
    std::string fileName = makeContingencyTestPath("contingency_test3.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    auto start_t = std::chrono::high_resolution_clock::now();
    gds->run();
    auto stop_t = std::chrono::high_resolution_clock::now();
    EXPECT_TRUE(exists("contout_N2.csv"));
    static_cast<void>(remove("contout_N2.csv"));

    std::chrono::duration<double> load_time = (stop_t - start_t);
    std::println("contingencies run in {:f} seconds", load_time.count());
}

TEST_F(LargeContingencyTests, ContingencyBcase)
{
    std::string fileName = makeContingencyTestPath("contingency_testbig.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    auto start_t = std::chrono::high_resolution_clock::now();
    int ret = gds->run();
    auto stop_t = std::chrono::high_resolution_clock::now();
    EXPECT_EQ(ret, FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(exists("contout_N2.csv"));

    std::chrono::duration<double> load_time = (stop_t - start_t);
    std::println("contingencies run in {:f} seconds", load_time.count());
}
