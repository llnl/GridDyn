/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test cases for the main executable

#include "../exeTestHelper.h"
#include <cstdlib>
#include <iostream>

#include <gtest/gtest.h>

static std::string pFlow_test_directory = std::string(GRIDDYN_TEST_DIRECTORY "/pFlow_tests/");

TEST(MainExeTests, MainExeTest1)
{
    exeTestRunner mainExeRunner(GRIDDYNMAIN_LOCATION, GRIDDYNINSTALL_LOCATION, "griddynMain");
    if (mainExeRunner.isActive()) {
        auto out = mainExeRunner.runCaptureOutput("--version");
        EXPECT_EQ(out.compare(0, 15, "GridDyn version"), 0);
        if (out.compare(0, 15, "GridDyn version") != 0) {
            printf("mismatch out string\n");
            printf("out=%s||\n", out.c_str());
        }
    } else {
        std::cout << "Unable to locate main executable:: skipping test\n";
    }
}

// test is in development
TEST(MainExeTests, CdfReadwriteTest)
{
    exeTestRunner mainExeRunner(GRIDDYNMAIN_LOCATION, GRIDDYNINSTALL_LOCATION, "griddynMain");
    if (mainExeRunner.isActive()) {
        std::string fileName = pFlow_test_directory + "test_powerflow3m9b2.xml";
        auto out = mainExeRunner.runCaptureOutput(
            fileName + " --powerflow-only --powerflow-output testout.cdf");
        std::cout << out;
    }
}
