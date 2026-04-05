/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

using namespace griddyn;
/** these test cases test out the contingency capabilities in GridDyn
 */

static const std::string contingency_test_directory(GRIDDYN_TEST_DIRECTORY "/contingency_tests/");

class ContingencyTests: public gridDynSimulationTestFixture, public ::testing::Test {};

using namespace boost::filesystem;
TEST_F(ContingencyTests, DISABLED_ContingencyTest1)
{
    std::string fileName = contingency_test_directory + "contingency_test1.xml";
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();
    EXPECT_TRUE(exists("contout.csv"));
    remove("contout.csv");
}

TEST_F(ContingencyTests, DISABLED_ContingencyTest2)
{
    std::string fileName = contingency_test_directory + "contingency_test2.xml";
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
    std::string fileName = contingency_test_directory + "contingency_test3.xml";
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();

    EXPECT_TRUE(exists("contout_N2.csv"));
    remove("contout_N2.csv");
}
*/
