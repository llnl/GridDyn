/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

using namespace griddyn;
/** these test cases test out the contingency capabilities in GridDyn
 */

static constexpr std::string_view contingencyTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                           "/contingency_tests/"};

static std::string makeContingencyTestPath(std::string_view fileName)
{
    return std::string{contingencyTestDirectory} + std::string{fileName};
}

class ContingencyTests: public GridDynSimulationTestFixture, public ::testing::Test {};

using namespace std::filesystem;
TEST_F(ContingencyTests, DISABLED_ContingencyTest1)
{
    std::string fileName = makeContingencyTestPath("contingency_test1.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();
    EXPECT_TRUE(exists("contout.csv"));
    remove("contout.csv");
}

TEST_F(ContingencyTests, DISABLED_ContingencyTest2)
{
    std::string fileName = makeContingencyTestPath("contingency_test2.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();

    EXPECT_TRUE(exists("contout_load.csv"));
    EXPECT_TRUE(exists("contout_bus.csv"));
    EXPECT_TRUE(exists("contout_gen.csv"));
    EXPECT_TRUE(exists("contout_line.csv"));
    remove("contout_load.csv");
    remove("contout_bus.csv");
    remove("contout_gen.csv");
    remove("contout_line.csv");
}

// Testing N-2 contingencies  TODO:: move to testExtra
/*
TEST_F(ContingencyTests, ContingencyTest3)
{
    std::string fileName = makeContingencyTestPath("contingency_test3.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();

    EXPECT_TRUE(exists("contout_N2.csv"));
    remove("contout_N2.csv");
}
*/
