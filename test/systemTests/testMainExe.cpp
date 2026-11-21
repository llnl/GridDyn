/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test cases for the main executable

#include "../exeTestHelper.h"
#include "griddyn/griddyn-config.h"
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

TEST(MainExeTests, ExecutesDcOpfWithOptimizationOption)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const auto output = mainExeRunner.runCaptureOutput(
        "\"" GRIDDYN_TEST_DIRECTORY
        "/validation_tests/case9.m\" -P \"action=opf preview\" --optimization");
#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
    EXPECT_TRUE(output.contains("opf native optimal objective=")) << output;
    EXPECT_TRUE(output.contains("applied=false")) << output;
    EXPECT_FALSE(output.contains(" returned ")) << output;
#else
    EXPECT_TRUE(output.contains("optimization is unavailable in this build")) << output;
    EXPECT_TRUE(output.contains(" returned ")) << output;
#endif
}

TEST(MainExeTests, AcceptsContinuationMonitorsInParamAction)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const auto output = mainExeRunner.runCaptureOutput(
        "\"" GRIDDYN_TEST_DIRECTORY
        "/pFlow_tests/continuation_two_bus.xml\" -P \"timestop=2,action=continuation 6 target 0.5 monitor 20,10\"");
    EXPECT_TRUE(output.contains("continuation target_reached")) << output;
    EXPECT_FALSE(output.contains(" returned ")) << output;
}

TEST(MainExeTests, ScriptFilesAndXmlActionsReachTheSameTime)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    struct TestCase {
        const char* arguments;
        const char* expectedName;
    };
    constexpr TestCase cases[]{
        {.arguments = "\"" GRIDDYN_TEST_DIRECTORY
                      "/dyn_tests1/test_dynSimple1.xml\" --script \"" GRIDDYN_TEST_DIRECTORY
                      "/runnerTests/scripts/outer actions.gds\" --script \"" GRIDDYN_TEST_DIRECTORY
                      "/runnerTests/scripts/report.gds\"",
         .expectedName = "after_nested"},
        {.arguments = "\"" GRIDDYN_TEST_DIRECTORY "/runnerTests/action_script.xml\"",
         .expectedName = "after_nested"},
        {.arguments = "\"" GRIDDYN_TEST_DIRECTORY
                      "/runnerTests/action_script.xml\" --script \"" GRIDDYN_TEST_DIRECTORY
                      "/runnerTests/scripts/report.gds\" -P \"action=set name after_param\"",
         .expectedName = "after_param"},
    };
    const std::string reportFile = "gdc17-powerflow.txt";
    for (const auto& test : cases) {
        SCOPED_TRACE(test.arguments);
        std::filesystem::remove(reportFile);
        const auto output = mainExeRunner.runCaptureOutput(test.arguments);
        EXPECT_FALSE(output.contains(" returned ")) << output;
        std::ifstream report(reportFile);
        std::string header;
        for (std::string line; std::getline(report, line);) {
            if (line.starts_with("Violations for ")) {
                header = line;
            }
        }
        ASSERT_FALSE(header.empty()) << output;
        EXPECT_TRUE(header.starts_with(std::string("Violations for ") + test.expectedName))
            << header;
        const auto timeSeparator = header.rfind("at ");
        ASSERT_NE(timeSeparator, std::string::npos) << header;
        std::istringstream timeField(header.substr(timeSeparator + 3));
        double reportedTime = 0.0;
        ASSERT_TRUE(timeField >> reportedTime) << header;
        EXPECT_NEAR(reportedTime, 0.2, 1e-8) << header;
        report.close();
        std::filesystem::remove(reportFile);
    }
}

TEST(MainExeTests, ScriptFailureReportsFileAndLineAndStopsLaterFiles)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const std::string reportFile = "gdc17-powerflow.txt";
    std::filesystem::remove(reportFile);
    const auto output = mainExeRunner.runCaptureOutput(
        "\"" GRIDDYN_TEST_DIRECTORY
        "/dyn_tests1/test_dynSimple1.xml\" --script \"" GRIDDYN_TEST_DIRECTORY
        "/runnerTests/scripts/broken.gds\" --script \"" GRIDDYN_TEST_DIRECTORY
        "/runnerTests/scripts/report.gds\"");
    EXPECT_TRUE(output.contains("broken.gds:3: action 'badverb' failed")) << output;
    EXPECT_TRUE(output.contains(" returned ")) << output;
    EXPECT_FALSE(std::filesystem::exists(reportFile));
}

TEST(MainExeTests, QueriesValuesAndListsDeclaredFields)
{
    ExeTestRunner mainExeRunner(GRIDDYN_TEST_MAIN_DIRECTORY, "gridDynMain");
    ASSERT_TRUE(mainExeRunner.isActive());
    const auto output = mainExeRunner.runCaptureOutput(
        "\"" GRIDDYN_TEST_DIRECTORY
        "/dyn_tests1/test_dynSimple1.xml\" -P \"action=run 0.2; get time(ms); get bus1:name; get --help bus1::load#0\"");
    EXPECT_FALSE(output.contains(" returned ")) << output;
    std::istringstream lines(output);
    bool foundTime = false;
    for (std::string line; std::getline(lines, line);) {
        std::istringstream value(line);
        double time = 0.0;
        if ((value >> time) && (value >> std::ws).eof()) {
            EXPECT_NEAR(time, 200.0, 1e-4);
            foundTime = true;
            break;
        }
    }
    EXPECT_TRUE(foundTime) << output;
    EXPECT_TRUE(output.contains("\n\"bus1\"\n")) << output;
    EXPECT_TRUE(output.contains("get <field|object:field> [file]")) << output;
    EXPECT_TRUE(output.contains("Declared fields on load3")) << output;
    EXPECT_TRUE(output.contains("Real power at nominal voltage")) << output;
}
