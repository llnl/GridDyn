/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/Link.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/simulation/Contingency.h"
#include "griddyn/simulation/GridDynActions.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

using namespace griddyn;
/** these test cases test out the contingency capabilities in GridDyn
 */

static constexpr std::string_view contingencyTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                           "/contingency_tests/"};

static std::string makeContingencyTestPath(std::string_view fileName)
{
    return std::string{contingencyTestDirectory} + std::string{fileName};
}

static std::vector<std::string> readContingencyCsvRow(std::istream& input)
{
    std::string line;
    if (!std::getline(input, line)) {
        return {};
    }
    auto fields = gmlc::utilities::stringOps::splitlineQuotes(line, ",", "\"");
    for (auto& field : fields) {
        gmlc::utilities::stringOps::trimString(field);
        if (field.starts_with('"') && field.ends_with('"')) {
            field = field.substr(1, field.size() - 2);
        }
    }
    return fields;
}

static void expectParallelLineSolution(const std::vector<std::string>& row,
                                       std::size_t summaryStart)
{
    // Solve the lossless two-bus equations for the surviving x = 0.2 link.
    constexpr double transfer = 0.9 * 0.2;
    const auto voltage = std::sqrt(0.5 * (1.0 + std::sqrt(1.0 - 4.0 * transfer * transfer)));
    if (summaryStart > 3) {
        EXPECT_DOUBLE_EQ(std::stod(row[3]), 1.0);
        EXPECT_NEAR(std::stod(row[4]), voltage, 5e-5);
        EXPECT_DOUBLE_EQ(std::stod(row[5]), 0.0);
        EXPECT_NEAR(std::stod(row[6]), -std::asin(transfer / voltage), 1e-5);
        EXPECT_NEAR(std::stod(row[7]), 0.9, 5e-5);
        EXPECT_DOUBLE_EQ(std::stod(row[8]), 0.0);
    }
    EXPECT_NEAR(std::stod(row[summaryStart]), voltage, 5e-5);
    EXPECT_EQ(std::stoi(row[summaryStart + 1]), 1);
    EXPECT_NEAR(std::stod(row[summaryStart + 2]), 0.0, 1e-6);
    EXPECT_NEAR(std::stod(row[summaryStart + 3]), 0.0, 1e-6);
    EXPECT_NEAR(std::stod(row[summaryStart + 4]), 0.0, 1e-6);
}

class ContingencyTests: public GridDynSimulationTestFixture, public ::testing::Test {};

using namespace std::filesystem;
TEST_F(ContingencyTests, ContingencyTest1)
{
    const auto fileName = makeContingencyTestPath("contingency_test1.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();
    EXPECT_TRUE(exists("contout.csv"));
    static_cast<void>(remove("contout.csv"));
}

TEST_F(ContingencyTests, ContingencyTest2)
{
    const auto fileName = makeContingencyTestPath("contingency_test2.xml");
    gds = readSimXMLFile(fileName);
    gds->set("printlevel", 0);
    gds->run();

    EXPECT_TRUE(exists("contout_load.csv"));
    EXPECT_TRUE(exists("contout_bus.csv"));
    EXPECT_TRUE(exists("contout_gen.csv"));
    EXPECT_TRUE(exists("contout_line.csv"));
    static_cast<void>(remove("contout_load.csv"));
    static_cast<void>(remove("contout_bus.csv"));
    static_cast<void>(remove("contout_gen.csv"));
    static_cast<void>(remove("contout_line.csv"));
}

TEST_F(ContingencyTests, ActionRangesSelectExactOutagesInBothReportFormats)
{
    gds = readSimXMLFile(makeContingencyTestPath("contingency_test1.xml"));
    ASSERT_NE(gds, nullptr);
    gds->set("printlevel", 0);
    ASSERT_EQ(gds->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    const auto report = temp_directory_path() / "gdc12 outage report.csv";
    const auto destination = '"' + report.string() + '"';
    remove(report);

    ASSERT_EQ(gds->execute("contingency LINE " + destination + " 2"), FUNCTION_EXECUTION_SUCCESS);
    std::ifstream contents(report);
    std::string header;
    std::string first;
    std::string second;
    std::string extra;
    ASSERT_TRUE(std::getline(contents, header));
    EXPECT_TRUE(header.contains(":flow"));
    ASSERT_TRUE(std::getline(contents, first));
    ASSERT_TRUE(std::getline(contents, second));
    EXPECT_FALSE(std::getline(contents, extra));
    EXPECT_LT(std::stoi(first), std::stoi(second));
    EXPECT_TRUE(first.contains(gds->getLink(0)->getName() + ":connected"));
    EXPECT_TRUE(second.contains(gds->getLink(1)->getName() + ":connected"));
    contents.close();
    remove(report);

    const auto prefixed = R"("file:/)" + report.generic_string() + '"';
    ASSERT_EQ(gds->execute("contingency SIMPLIFIED LINE " + prefixed + " 1 1"),
              FUNCTION_EXECUTION_SUCCESS);
    contents.open(report);
    ASSERT_TRUE(std::getline(contents, header));
    EXPECT_FALSE(header.contains(":flow"));
    EXPECT_TRUE(header.ends_with(", violations"));
    ASSERT_TRUE(std::getline(contents, second));
    EXPECT_TRUE(second.contains(gds->getLink(1)->getName() + ":connected"));
    constexpr std::string_view endOfOutage = R"( = 0;", )";
    EXPECT_TRUE(second.contains(endOfOutage)) << second;
    EXPECT_FALSE(std::getline(contents, extra));
    contents.close();
    remove(report);

    EXPECT_THROW(gds->execute("contingency line " + destination + " 9999 1"), ActionError);
    EXPECT_THROW(gds->execute("contingency line " + destination + " 9999"), ActionError);
    EXPECT_THROW(gds->execute("contingency \"scenarios.txt\" " + destination), ActionError);
    EXPECT_THROW(gds->execute("contingency line database:/study 1"), ActionError);
    EXPECT_THROW(gds->execute("contingency line . 1"), ActionError);
    EXPECT_FALSE(exists(report));
}

TEST_F(ContingencyTests, ActionRunsTwoDistinctOutagesInOneScenario)
{
    gds = readSimXMLFile(GRIDDYN_TEST_DIRECTORY "/pFlow_tests/continuation_two_bus.xml");
    ASSERT_NE(gds, nullptr);
    gds->set("printlevel", 0);
    ASSERT_EQ(gds->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    const auto report = temp_directory_path() / "gdc12 double outage.csv";
    remove(report);
    ASSERT_EQ(gds->execute("contingency simplified N-2 \"" + report.string() + "\" 0 1"),
              FUNCTION_EXECUTION_SUCCESS);
    std::ifstream contents(report);
    std::string header;
    std::string row;
    ASSERT_TRUE(std::getline(contents, header));
    ASSERT_TRUE(std::getline(contents, row));
    EXPECT_TRUE(row.contains("line:connected"));
    EXPECT_TRUE(row.contains("supply:connected"));
    EXPECT_FALSE(std::getline(contents, row));
    contents.close();
    remove(report);

    testing::internal::CaptureStdout();
    EXPECT_EQ(gds->execute("contingency simplified line"), FUNCTION_EXECUTION_SUCCESS);
    const auto standardOutput = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(standardOutput.contains("index, name, event, min Voltage"));
    EXPECT_TRUE(standardOutput.contains("line:connected"));
}

TEST_F(ContingencyTests, ReportsConvergedAndFailedRowsUnderTheSameColumns)
{
    gds = readSimXMLFile(makeContingencyTestPath("parallel_line_limits.xml"));
    ASSERT_NE(gds, nullptr);
    gds->set("printlevel", 0);
    ASSERT_EQ(gds->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_NEAR(gds->getLoadReal(), 0.9, 1e-6);
    ASSERT_NEAR(gds->getGenerationReal(), -0.9, 1e-6);

    const std::vector<std::string> summaryColumns{"min Voltage",
                                                  "islands",
                                                  "loadLossEvent",
                                                  "loadLoss",
                                                  "genLossEvent",
                                                  "genLoss",
                                                  "violations"};
    std::vector<double> fullSummary;
    for (const bool simplified : {false, true}) {
        SCOPED_TRACE(simplified);
        const auto report = temp_directory_path() /
            (simplified ? "gdc12 simplified outcome.csv" : "gdc12 full outcome.csv");
        remove(report);
        const auto command =
            std::string(simplified ? "contingency simplified line " : "contingency line ") + '"' +
            report.string() + '"';
        ASSERT_EQ(gds->execute(command), FUNCTION_EXECUTION_SUCCESS);
        std::ifstream contents(report);
        const auto header = readContingencyCsvRow(contents);
        std::vector<std::string> expected{"index", "name", "event"};
        if (!simplified) {
            expected.insert(expected.end(),
                            {"source:V",
                             "demand:V",
                             "source:A",
                             "demand:A",
                             "strong:flow",
                             "weak, backup:flow"});
        }
        const auto summaryStart = expected.size();
        expected.insert(expected.end(), summaryColumns.begin(), summaryColumns.end());
        ASSERT_EQ(header, expected);

        const auto failed = readContingencyCsvRow(contents);
        const auto solved = readContingencyCsvRow(contents);
        ASSERT_EQ(failed.size(), header.size());
        ASSERT_EQ(solved.size(), header.size());
        EXPECT_LT(std::stoi(failed[0]), std::stoi(solved[0]));
        EXPECT_TRUE(failed[1].starts_with("contingency_"));
        EXPECT_EQ(failed[2], "strong:connected = 0;");
        EXPECT_EQ(solved[2], "weak, backup:connected = 0;");
        for (std::size_t index = 3; index + 1 < failed.size(); ++index) {
            EXPECT_TRUE(failed[index].empty()) << header[index];
        }
        EXPECT_EQ(failed.back(), "parallel_contingency[solver failed to converge(45)];");
        EXPECT_TRUE(solved.back().empty());

        expectParallelLineSolution(solved, summaryStart);
        for (auto index = summaryStart; index + 1 < solved.size(); ++index) {
            const auto value = std::stod(solved[index]);
            EXPECT_TRUE(std::isfinite(value)) << header[index];
            if (simplified) {
                ASSERT_LT(index - summaryStart, fullSummary.size());
                EXPECT_NEAR(value, fullSummary[index - summaryStart], 5e-5) << header[index];
            } else {
                fullSummary.push_back(value);
            }
        }
        EXPECT_TRUE(readContingencyCsvRow(contents).empty());
        contents.close();
        remove(report);
    }
}

TEST_F(ContingencyTests, ReportHeaderIncludesNestedAndDirectLinks)
{
    GridDynSimulation simulation;
    auto* area = new GridArea("nested");
    area->add(new AcLine("nestedLink"));
    simulation.add(area);
    simulation.add(new AcLine("directLink"));

    const Contingency report(&simulation);
    const auto header = report.generateHeader();
    EXPECT_TRUE(header.contains("nestedLink:flow, directLink:flow, min Voltage"));
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
