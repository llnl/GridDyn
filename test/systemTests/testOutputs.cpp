/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test cases for the simulation outputs

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/simulation/GridDynSimulationFileOps.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iterator>
#include <memory>
#include <pugixml.hpp>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace griddyn;
using gmlc::utilities::countDiffs;

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

class OutputTests: public GridDynSimulationTestFixture, public ::testing::Test {};

namespace {
class OutputFileCleanup {
  public:
    void track(const std::string& fileName) { fileNames.push_back(fileName); }

    ~OutputFileCleanup()
    {
        std::error_code error;
        for (const auto& fileName : fileNames) {
            std::filesystem::remove(fileName, error);
            error.clear();
        }
    }

  private:
    std::vector<std::string> fileNames;
};

std::string readTextFile(const std::string& fileName)
{
    std::ifstream input(fileName, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::string> parseCsvRow(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (size_t charIndex = 0; charIndex < line.size(); ++charIndex) {
        const char currentChar = line[charIndex];
        if (currentChar == '"') {
            if (inQuotes && charIndex + 1 < line.size() && line[charIndex + 1] == '"') {
                field.push_back('"');
                ++charIndex;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (currentChar == ',' && !inQuotes) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(currentChar);
        }
    }
    fields.push_back(std::move(field));
    for (auto& parsedField : fields) {
        const auto first = parsedField.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            parsedField.clear();
            continue;
        }
        const auto last = parsedField.find_last_not_of(" \t\r\n");
        parsedField = parsedField.substr(first, last - first + 1);
    }
    return fields;
}

std::vector<std::string>
    findCsvRow(const std::string& contents, const std::string& name, size_t nameColumn)
{
    std::istringstream input(contents);
    std::string line;
    while (std::getline(input, line)) {
        auto fields = parseCsvRow(line);
        if (fields.size() > nameColumn && fields[nameColumn] == name) {
            return fields;
        }
    }
    return {};
}

std::vector<double> findTxtBusValues(const std::string& contents, const std::string& busName)
{
    std::istringstream input(contents);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        int areaNumber = 0;
        int busNumber = 0;
        std::string name;
        double voltage = 0.0;
        double angleDegrees = 0.0;
        if ((row >> areaNumber >> busNumber >> std::quoted(name) >> voltage >> angleDegrees) &&
            name == busName) {
            return {voltage, angleDegrees};
        }
    }
    return {};
}
}  // namespace

TEST_F(OutputTests, OutputTest1)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b2.xml";

    simpleStageCheck(fileName, GridSimulation::GridState::POWERFLOW_COMPLETE);
    const std::string cdfFile = "testout_roundtrip.cdf";
    OutputFileCleanup cleanup;
    cleanup.track(cdfFile);
    savePowerFlow(gds.get(), cdfFile);

    ASSERT_TRUE(std::filesystem::exists(cdfFile));

    gds2 = std::make_unique<GridDynSimulation>();
    loadFile(gds2.get(), cdfFile);
    gds2->powerflow();

    std::vector<double> st1 = gds->getState(cPflowSolverMode);
    std::vector<double> st2 = gds2->getState(cPflowSolverMode);

    auto diff = countDiffs(st1, st2, 0.000001);
    EXPECT_EQ(diff, 0U);

    // XML stores the solved bus voltages and angles. Load it into a deliberately perturbed
    // network so the test proves that the reader restores those values.
    const std::string xmlFile = "testout_roundtrip.xml";
    cleanup.track(xmlFile);
    savePowerFlow(gds.get(), xmlFile);

    pugi::xml_document xmlOutput;
    const auto xmlLoadResult = xmlOutput.load_file(xmlFile.c_str());
    ASSERT_TRUE(static_cast<bool>(xmlLoadResult)) << xmlLoadResult.description();
    auto xmlBuses = xmlOutput.child("PowerFlow").child("buses");
    auto xmlLinks = xmlOutput.child("PowerFlow").child("links");
    EXPECT_EQ(xmlBuses.child("count").text().as_int(), gds->getInt("buscount"));
    EXPECT_EQ(xmlLinks.child("count").text().as_int(), gds->getInt("linkcount"));
    auto* firstLink = gds->getLink(0);
    ASSERT_NE(firstLink, nullptr);
    const auto xmlLink = xmlLinks.child("link");
    ASSERT_TRUE(xmlLink);
    EXPECT_EQ(xmlLink.child("name").text().as_string(), firstLink->getName());
    EXPECT_EQ(xmlLink.child("Bus1").text().as_int(), firstLink->getBus(1)->getUserID());
    EXPECT_EQ(xmlLink.child("Bus2").text().as_int(), firstLink->getBus(2)->getUserID());
    EXPECT_NEAR(xmlLink.child("RealImpedance").text().as_double(), firstLink->get("r"), 1e-12);
    EXPECT_NEAR(xmlLink.child("ImagImpedance").text().as_double(), firstLink->get("x"), 1e-12);
    EXPECT_NEAR(xmlLink.child("RealIn").text().as_double(), firstLink->getRealPower(), 1e-12);
    EXPECT_NEAR(xmlLink.child("RealOut").text().as_double(), firstLink->getRealPower(2), 1e-12);

    index_t busIndex = 0;
    auto* savedBus = gds->getBus(busIndex);
    auto* loadedBus = gds2->getBus(busIndex);
    while (savedBus != nullptr && loadedBus != nullptr) {
        loadedBus->setVoltageAngle(0.5 + (0.01 * static_cast<double>(busIndex)),
                                   -0.2 - (0.01 * static_cast<double>(busIndex)));
        ++busIndex;
        savedBus = gds->getBus(busIndex);
        loadedBus = gds2->getBus(busIndex);
    }
    ASSERT_EQ(savedBus, nullptr);
    ASSERT_EQ(loadedBus, nullptr);
    loadPowerFlow(gds2.get(), xmlFile);

    for (busIndex = 0; (savedBus = gds->getBus(busIndex)) != nullptr; ++busIndex) {
        loadedBus = gds2->getBus(busIndex);
        ASSERT_NE(loadedBus, nullptr);
        EXPECT_NEAR(loadedBus->getVoltage(), savedBus->getVoltage(), 1e-10);
        EXPECT_NEAR(loadedBus->getAngle(), savedBus->getAngle(), 1e-10);
    }

    // CSV is machine-readable output but has no GridDyn power-flow reader, so validate its
    // serialized bus values directly.
    const std::string csvFile = "testout_roundtrip.csv";
    cleanup.track(csvFile);
    savePowerFlow(gds.get(), csvFile);
    const auto csvContents = readTextFile(csvFile);
    EXPECT_NE(csvContents.find("basepower="), std::string::npos);
    EXPECT_NE(csvContents.find("\"voltage(pu)\""), std::string::npos);
    auto* firstBus = gds->getBus(0);
    ASSERT_NE(firstBus, nullptr);
    const auto csvBusRow = findCsvRow(csvContents, firstBus->getName(), 3);
    ASSERT_EQ(csvBusRow.size(), 14U);
    EXPECT_NEAR(std::stod(csvBusRow[4]), firstBus->getVoltage(), 1e-6);
    EXPECT_NEAR(std::stod(csvBusRow[5]), firstBus->getAngle() * 180.0 / kPI, 1e-4);

    // TXT and its .out alias are human-readable reports without a matching loader.
    const std::string txtFile = "testout_roundtrip.txt";
    const std::string outFile = "testout_roundtrip.out";
    cleanup.track(txtFile);
    cleanup.track(outFile);
    savePowerFlow(gds.get(), txtFile);
    savePowerFlow(gds.get(), outFile);
    const auto txtContents = readTextFile(txtFile);
    EXPECT_NE(txtContents.find("BUS INFORMATION"), std::string::npos);
    EXPECT_NE(txtContents.find("LINE INFORMATION"), std::string::npos);
    EXPECT_EQ(txtContents, readTextFile(outFile));
    const auto txtBusValues = findTxtBusValues(txtContents, firstBus->getName());
    ASSERT_EQ(txtBusValues.size(), 2U);
    EXPECT_NEAR(txtBusValues[0], firstBus->getVoltage(), 1e-6);
    EXPECT_NEAR(txtBusValues[1], firstBus->getAngle() * 180.0 / kPI, 1e-4);

    // The standalone bus and line tables do not have input counterparts; check that they
    // preserve representative values from the solved model.
    const std::string busDataFile = "testout_bus_data.csv";
    const std::string lineDataFile = "testout_line_data.csv";
    cleanup.track(busDataFile);
    cleanup.track(lineDataFile);
    saveBusData(gds.get(), busDataFile);
    saveLineData(gds.get(), lineDataFile);

    const auto busDataRow = findCsvRow(readTextFile(busDataFile), firstBus->getName(), 0);
    ASSERT_EQ(busDataRow.size(), 7U);
    EXPECT_NEAR(std::stod(busDataRow[1]), firstBus->getVoltage(), 1e-5);
    EXPECT_NEAR(std::stod(busDataRow[2]), firstBus->getAngle(), 1e-5);

    std::vector<std::string> linkNames;
    std::vector<double> linkRealPower;
    std::vector<double> linkReactivePower;
    std::vector<double> linkReverseRealPower;
    std::vector<double> linkReverseReactivePower;
    std::vector<double> linkLoss;
    gds->getLinkName(linkNames);
    gds->getLinkRealPower(linkRealPower, 0, 1);
    gds->getLinkReactivePower(linkReactivePower, 0, 1);
    gds->getLinkRealPower(linkReverseRealPower, 0, 2);
    gds->getLinkReactivePower(linkReverseReactivePower, 0, 2);
    gds->getLinkLoss(linkLoss);
    ASSERT_FALSE(linkNames.empty());
    const auto lineDataRow = findCsvRow(readTextFile(lineDataFile), linkNames.front(), 0);
    ASSERT_EQ(lineDataRow.size(), 6U);
    EXPECT_NEAR(std::stod(lineDataRow[1]), linkRealPower.front(), 1e-5);
    EXPECT_NEAR(std::stod(lineDataRow[2]), linkReactivePower.front(), 1e-5);
    EXPECT_NEAR(std::stod(lineDataRow[3]), linkReverseRealPower.front(), 1e-5);
    EXPECT_NEAR(std::stod(lineDataRow[4]), linkReverseReactivePower.front(), 1e-5);
    EXPECT_NEAR(std::stod(lineDataRow[5]), linkLoss.front(), 1e-5);
}
