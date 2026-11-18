/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test cases for the main executable

#include "../exeTestHelper.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

TEST(MainExeTests, PrintsVersion)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const auto output = mainExeRunner.runCaptureOutput("--version");
    EXPECT_TRUE(output.starts_with("GridDyn version")) << output;
    EXPECT_EQ(output.find(" returned "), std::string::npos) << output;
}

TEST(MainExeTests, WritesPowerFlowCaseAsCdf)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const std::string outputFile = "main_exe_powerflow.cdf";
    std::filesystem::remove(outputFile);
    const std::string inputFile = std::string{pFlow_test_directory} + "test_powerflow3m9b2.xml";
    ASSERT_EQ(mainExeRunner.run("\"" + inputFile + "\" --powerflow-only --powerflow-output " +
                                outputFile),
              0);
    std::ifstream result(outputFile);
    ASSERT_TRUE(result.is_open()) << outputFile;
    std::string contents;
    for (std::string line; std::getline(result, line);) {
        contents += line + '\n';
    }
    result.close();
    std::filesystem::remove(outputFile);
    EXPECT_NE(contents.find("BUS DATA FOLLOWS"), std::string::npos);
    EXPECT_NE(contents.find("BRANCH DATA FOLLOWS"), std::string::npos);
}
