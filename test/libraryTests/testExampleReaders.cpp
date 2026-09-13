/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/links/AcLine.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ExampleReaderCase {
    std::string_view fileName;
    int busCount;
    int linkCount;
    int genCount;
    int loadCount;
};

std::filesystem::path makeExamplePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / ".." / ".." / "examples" /
        std::string{fileName};
}

void expectInternalLinksBelongToArea(griddyn::GridArea* area)
{
    const auto linkCount = area->getInt("linkcount");
    for (int linkIndex = 0; linkIndex < linkCount; ++linkIndex) {
        auto* link = area->getLink(linkIndex);
        ASSERT_NE(link, nullptr);
        ASSERT_NE(link->getBus(1), nullptr);
        ASSERT_NE(link->getBus(2), nullptr);
        EXPECT_EQ(link->getBus(1)->getParent(), area);
        EXPECT_EQ(link->getBus(2)->getParent(), area);
    }
}

}  // namespace

TEST(ExampleReaderTests, LoadTopLevelExamples)
{
    static constexpr std::array<ExampleReaderCase, 7> exampleCases{{
        {.fileName = "two_bus_example.xml",
         .busCount = 2,
         .linkCount = 1,
         .genCount = 1,
         .loadCount = 2},
        {.fileName = "IEEE_14bus.cdf",
         .busCount = 14,
         .linkCount = 20,
         .genCount = 5,
         .loadCount = 11},
        {.fileName = "IEEE39.raw",
         .busCount = 39,
         .linkCount = 46,
         .genCount = 10,
         .loadCount = 33},
        {.fileName = "powerflow.raw",
         .busCount = 179,
         .linkCount = 263,
         .genCount = 29,
         .loadCount = 144},
        {.fileName = "powerflowWECC.raw",
         .busCount = 179,
         .linkCount = 263,
         .genCount = 29,
         .loadCount = 144},
        {.fileName = "test_griddyn39.xml",
         .busCount = 39,
         .linkCount = 46,
         .genCount = 10,
         .loadCount = 20},
        {.fileName = "test_faulting.xml",
         .busCount = 3,
         .linkCount = 3,
         .genCount = 1,
         .loadCount = 1},
    }};

    for (const auto& exampleCase : exampleCases) {
        SCOPED_TRACE(exampleCase.fileName);
        auto gds = std::make_unique<griddyn::GridDynSimulation>();
        const auto filePath = makeExamplePath(exampleCase.fileName);

        ASSERT_TRUE(std::filesystem::exists(filePath));
        griddyn::loadFile(gds, filePath.string());

        ASSERT_EQ(gds->currentProcessState(), griddyn::GridDynSimulation::GridState::STARTUP);
        EXPECT_EQ(gds->getInt("totalbuscount"), exampleCase.busCount);
        EXPECT_EQ(gds->getInt("totallinkcount"), exampleCase.linkCount);
        EXPECT_EQ(gds->getInt("gencount"), exampleCase.genCount);
        EXPECT_EQ(gds->getInt("loadcount"), exampleCase.loadCount);
    }
}

TEST(ExampleReaderTests, LoadRawAreaDefinitions)
{
    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    const auto filePath = makeExamplePath("powerflow.raw");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    ASSERT_EQ(gds->getInt("totalareacount"), 3);
    auto* area = gds->getGridArea(0);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->getInt("buscount"), 72);
    expectInternalLinksBelongToArea(area);
    int internalLinkCount = area->getInt("linkcount");
    area = gds->getGridArea(1);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->getInt("buscount"), 76);
    expectInternalLinksBelongToArea(area);
    internalLinkCount += area->getInt("linkcount");
    area = gds->getGridArea(2);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->getInt("buscount"), 31);
    expectInternalLinksBelongToArea(area);
    internalLinkCount += area->getInt("linkcount");
    EXPECT_GT(internalLinkCount, 0);
    EXPECT_GT(gds->getInt("linkcount"), 0);
    EXPECT_EQ(internalLinkCount + gds->getInt("linkcount"), gds->getInt("totallinkcount"));
    EXPECT_EQ(gds->getInt("buscount"), 0);
}

TEST(ExampleReaderTests, LoadEpcAreaDefinitions)
{
    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    const auto filePath =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "IEEE_test_cases" / "IEEE 14 bus.epc";

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    ASSERT_EQ(gds->getInt("totalareacount"), 1);
    auto* area = gds->getGridArea(0);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->getInt("buscount"), 14);
    expectInternalLinksBelongToArea(area);
    const auto internalLinkCount = area->getInt("linkcount");
    EXPECT_EQ(internalLinkCount + gds->getInt("linkcount"), gds->getInt("totallinkcount"));
    EXPECT_EQ(gds->getInt("buscount"), 0);
}

TEST(ExampleReaderTests, LoadMatPowerAreaDefinitions)
{
    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    const auto filePath =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "matlab_test_files" / "case24_ieee_rts.m";

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    ASSERT_EQ(gds->getInt("totalareacount"), 4);
    const std::array<int, 4> expectedBusCounts{6, 4, 7, 7};
    for (size_t ii = 0; ii < expectedBusCounts.size(); ++ii) {
        auto* area = gds->getGridArea(static_cast<index_t>(ii));
        ASSERT_NE(area, nullptr);
        EXPECT_EQ(area->getInt("buscount"), expectedBusCounts[ii]);
    }
    EXPECT_EQ(gds->getInt("buscount"), 0);
}

TEST(ExampleReaderTests, MatPowerPreservesBusVoltageAndUnconstrainedAngles)
{
    const auto filePath =
        std::filesystem::temp_directory_path() / "griddyn_matpower_voltage_angle_limits.m";
    {
        std::ofstream output(filePath);
        ASSERT_TRUE(output.is_open());
        output << "function mpc = griddyn_matpower_voltage_angle_limits\n"
                  "mpc.version = '2';\n"
                  "mpc.baseMVA = 100;\n"
                  "mpc.bus = [\n"
                  "1 2 0 0 0 0 1 1.0000 0.0000 230 1 1.1000 0.9000;\n"
                  "2 1 10 2 0 0 1 0.9800 -1.0000 230 1 1.1000 0.9000;\n"
                  "];\n"
                  "mpc.gen = [\n"
                  "1 10 3 50 -20 1.0500 100 1 50 0;\n"
                  "];\n"
                  "mpc.branch = [\n"
                  "1 2 0.01 0.05 0.01 100 100 100 0 0 1 0 0;\n"
                  "1 2 0.02 0.06 0.01 100 100 100 0 0 1 -30 0;\n"
                  "];\n";
    }

    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(gds, filePath.string());

    auto* bus = dynamic_cast<griddyn::GridBus*>(gds->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), 1.0);
    EXPECT_DOUBLE_EQ(bus->get("vtarget"), 1.0);
    EXPECT_DOUBLE_EQ(bus->get("vmax"), 1.1);
    EXPECT_DOUBLE_EQ(bus->get("vmin"), 0.9);

    auto* link = dynamic_cast<griddyn::AcLine*>(gds->findByUserID("link", 1));
    ASSERT_NE(link, nullptr);
    EXPECT_NEAR(link->get("minangle"), -2.0 * griddyn::kPI, 1.0e-6);
    EXPECT_NEAR(link->get("maxangle"), 2.0 * griddyn::kPI, 1.0e-6);

    auto* oneSidedLink = dynamic_cast<griddyn::AcLine*>(gds->findByUserID("link", 2));
    ASSERT_NE(oneSidedLink, nullptr);
    EXPECT_NEAR(oneSidedLink->get("minangle"), -30.0 * griddyn::kPI / 180.0, 1.0e-6);
    EXPECT_NEAR(oneSidedLink->get("maxangle"), 0.0, 1.0e-12);

    std::error_code ec;
    std::filesystem::remove(filePath, ec);
}

TEST(ExampleReaderTests, PyPowerUsesMatPowerVoltageAndAngleSemantics)
{
    const auto filePath = std::filesystem::temp_directory_path() /
        "griddyn_pypower_voltage_angle_limits.py";
    {
        std::ofstream output(filePath);
        ASSERT_TRUE(output.is_open());
        output << "from numpy import array\n"
                  "ppc = {}\n"
                  "ppc['baseMVA'] = 100\n"
                  "ppc['bus'] = array([\n"
                  "[1, 2, 0, 0, 0, 0, 1, 1.0000, 0.0000, 230, 1, 1.1000, 0.9000],\n"
                  "[2, 1, 10, 2, 0, 0, 1, 0.9800, -1.0000, 230, 1, 1.1000, 0.9000]\n"
                  "])\n"
                  "ppc['gen'] = array([\n"
                  "[1, 10, 3, 50, -20, 1.0500, 100, 1, 50, 0]\n"
                  "])\n"
                  "ppc['branch'] = array([\n"
                  "[1, 2, 0.01, 0.05, 0.01, 100, 100, 100, 0, 0, 1, 0, 0],\n"
                  "[1, 2, 0.02, 0.06, 0.01, 100, 100, 100, 0, 0, 1, -30, 0]\n"
                  "])\n";
    }

    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(gds, filePath.string());

    auto* bus = dynamic_cast<griddyn::GridBus*>(gds->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), 1.0);
    EXPECT_DOUBLE_EQ(bus->get("vtarget"), 1.0);
    EXPECT_DOUBLE_EQ(bus->get("vmax"), 1.1);
    EXPECT_DOUBLE_EQ(bus->get("vmin"), 0.9);

    auto* link = dynamic_cast<griddyn::AcLine*>(gds->findByUserID("link", 1));
    ASSERT_NE(link, nullptr);
    EXPECT_NEAR(link->get("minangle"), -2.0 * griddyn::kPI, 1.0e-6);
    EXPECT_NEAR(link->get("maxangle"), 2.0 * griddyn::kPI, 1.0e-6);

    auto* oneSidedLink = dynamic_cast<griddyn::AcLine*>(gds->findByUserID("link", 2));
    ASSERT_NE(oneSidedLink, nullptr);
    EXPECT_NEAR(oneSidedLink->get("minangle"), -30.0 * griddyn::kPI / 180.0, 1.0e-6);
    EXPECT_NEAR(oneSidedLink->get("maxangle"), 0.0, 1.0e-12);

    std::error_code ec;
    std::filesystem::remove(filePath, ec);
}

TEST(ExampleReaderTests, LoadDynamicImportExampleWithoutRunningDynamics)
{
    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    const auto filePath = makeExamplePath("179busDynamicTest.xml");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    ASSERT_EQ(gds->currentProcessState(), griddyn::GridDynSimulation::GridState::STARTUP);
    EXPECT_EQ(gds->getInt("totalbuscount"), 179);
    EXPECT_EQ(gds->getInt("totallinkcount"), 263);
    EXPECT_EQ(gds->getInt("gencount"), 29);
    EXPECT_EQ(gds->getInt("loadcount"), 144);
}

TEST(ExampleReaderTests, LoadPyPowerCase)
{
    auto gds = std::make_unique<griddyn::GridDynSimulation>();
    const auto filePath =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / "case2.py";

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    EXPECT_EQ(gds->getInt("totalbuscount"), 2);
    EXPECT_EQ(gds->getInt("totallinkcount"), 1);
    EXPECT_EQ(gds->getInt("gencount"), 1);
    EXPECT_EQ(gds->getInt("loadcount"), 1);
}

TEST(ExampleReaderTests, ExportPyPowerRoundTrip)
{
    const auto source =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "matlab_test_files" / "case9.m";
    const auto exported = std::filesystem::temp_directory_path() / "griddyn_pypower_roundtrip.py";
    auto original = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(original, source.string());
    griddyn::stringVec warnings;
    ASSERT_TRUE(griddyn::savePyPowerCase(original.get(), exported.string(), &warnings));
    EXPECT_TRUE(warnings.empty());
    auto imported = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(imported, exported.string());
    EXPECT_EQ(imported->getInt("totalbuscount"), original->getInt("totalbuscount"));
    EXPECT_EQ(imported->getInt("totallinkcount"), original->getInt("totallinkcount"));
    EXPECT_EQ(imported->getInt("gencount"), original->getInt("gencount"));
    EXPECT_EQ(imported->getInt("loadcount"), original->getInt("loadcount"));
    std::filesystem::remove(exported);
}
TEST(ExampleReaderTests, ExportMatPowerRoundTrip)
{
    const auto source =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "matlab_test_files" / "case9.m";
    const auto exported = std::filesystem::temp_directory_path() / "griddyn_matpower_roundtrip.m";
    auto original = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(original, source.string());
    griddyn::stringVec warnings;
    ASSERT_TRUE(griddyn::saveMatPowerCase(original.get(), exported.string(), &warnings));
    EXPECT_TRUE(warnings.empty());
    auto imported = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(imported, exported.string());
    EXPECT_EQ(imported->getInt("totalbuscount"), original->getInt("totalbuscount"));
    EXPECT_EQ(imported->getInt("totallinkcount"), original->getInt("totallinkcount"));
    EXPECT_EQ(imported->getInt("gencount"), original->getInt("gencount"));
    EXPECT_EQ(imported->getInt("loadcount"), original->getInt("loadcount"));
    ASSERT_EQ(imported->pFlowInitialize(), 0);
    EXPECT_EQ(imported->powerflow(), 0);
    EXPECT_EQ(imported->currentProcessState(),
              griddyn::GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    std::filesystem::remove(exported);
}
