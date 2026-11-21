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
#include <sstream>
#include <string>

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

TEST(MainExeTests, PrintsVersion)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const auto output = mainExeRunner.runCaptureOutput("--version");
    EXPECT_TRUE(output.starts_with("GridDyn version:") || output.contains("\nGridDyn version:"))
        << output;
    EXPECT_EQ(output.find(" returned "), std::string::npos) << output;
}

TEST(MainExeTests, HelpSucceedsAndBadOptionsFail)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const auto help = mainExeRunner.runCaptureOutput("--help");
    EXPECT_NE(help.find("--powerflow-only"), std::string::npos) << help;
    EXPECT_EQ(help.find(" returned "), std::string::npos) << help;
    const std::string inputFile = std::string{pFlow_test_directory} + "continuation_two_bus.xml";
    const auto badOption = mainExeRunner.runCaptureOutput("\"" + inputFile + "\" --bad-option");
    EXPECT_NE(badOption.find("--bad-option"), std::string::npos) << badOption;
    EXPECT_NE(badOption.find(" returned "), std::string::npos) << badOption;
}

TEST(MainExeTests, ReportsScriptErrorsAndStopsBeforeLaterActions)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    struct TestCase {
        const char* arguments;
        const char* source;
        const char* command;
    };
    constexpr TestCase cases[]{
        {.arguments = "\"" GRIDDYN_TEST_DIRECTORY "/runnerTests/action_error_list.xml\"",
         .source = "action_error_list.xml",
         .command = "badverb"},
        {.arguments = "\"" GRIDDYN_TEST_DIRECTORY "/runnerTests/action_error_elements.xml\"",
         .source = "action_error_elements.xml",
         .command = "contingency simplified"},
        {.arguments =
             "\"" GRIDDYN_TEST_DIRECTORY "/pFlow_tests/continuation_two_bus.xml\" "
             "-P \"action=save powerflow gdc04-before.csv; check powerflow .; save powerflow gdc04-after.csv\"",
         .source = "--param action",
         .command = "check powerflow ."},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.arguments);
        std::filesystem::remove("gdc04-before.csv");
        std::filesystem::remove("gdc04-after.csv");
        const auto output = mainExeRunner.runCaptureOutput(test.arguments);
        EXPECT_NE(output.find(std::string(test.source) + ": action '" + test.command + "' failed"),
                  std::string::npos)
            << output;
        EXPECT_NE(output.find(" returned "), std::string::npos) << output;
        EXPECT_TRUE(std::filesystem::exists("gdc04-before.csv"));
        EXPECT_FALSE(std::filesystem::exists("gdc04-after.csv"));
        std::filesystem::remove("gdc04-before.csv");
        std::filesystem::remove("gdc04-after.csv");
    }
}

TEST(MainExeTests, DynamicActionsRequireTheSolverModeToBeConfigured)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const std::string caseFile = "\"" GRIDDYN_TEST_DIRECTORY "/dyn_tests1/test_dynSimple1.xml\"";
    const std::string reportFile = "gdc05-powerflow.txt";
    const std::string commands = "dynamic part 0.2 0.05; check powerflow " + reportFile;
    std::filesystem::remove(reportFile);

    const auto missing =
        mainExeRunner.runCaptureOutput(caseFile + " -P \"action=" + commands + "\"");
    EXPECT_NE(missing.find("partitioned dynamics requires configured"), std::string::npos)
        << missing;
    EXPECT_NE(missing.find("action 'dynamic part 0.2 0.05' failed"), std::string::npos) << missing;
    EXPECT_NE(missing.find(" returned "), std::string::npos) << missing;
    EXPECT_FALSE(std::filesystem::exists(reportFile));

    const std::string configuredFile =
        "\"" GRIDDYN_TEST_DIRECTORY "/runnerTests/action_partitioned.xml\"";
    const std::string args[]{
        configuredFile + " -P \"action=" + commands + "\"",
        caseFile + " -P \"action=set dynamicsolvermethod partitioned; " + commands + "\"",
    };
    for (const auto& commandLine : args) {
        SCOPED_TRACE(commandLine);
        const auto output = mainExeRunner.runCaptureOutput(commandLine);
        EXPECT_EQ(output.find(" returned "), std::string::npos) << output;
        std::ifstream report(reportFile);
        std::string header;
        ASSERT_TRUE(std::getline(report, header)) << output;
        const auto timeSeparator = header.rfind("at ");
        ASSERT_NE(timeSeparator, std::string::npos) << header;
        std::istringstream timeField(header.substr(timeSeparator + 3));
        double reportedTime = 0.0;
        ASSERT_TRUE(timeField >> reportedTime) << header;
        EXPECT_TRUE((timeField >> std::ws).eof()) << header;
        EXPECT_NEAR(reportedTime, 0.2, 1e-8) << header;
        report.close();
        std::filesystem::remove(reportFile);
    }
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
