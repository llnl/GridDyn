/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <array>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

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
    const auto filePath = std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / "case2.py";

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    EXPECT_EQ(gds->getInt("totalbuscount"), 2);
    EXPECT_EQ(gds->getInt("totallinkcount"), 1);
    EXPECT_EQ(gds->getInt("gencount"), 1);
    EXPECT_EQ(gds->getInt("loadcount"), 1);
}

TEST(ExampleReaderTests, ExportPyPowerRoundTrip)
{
    const auto source = std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "matlab_test_files" / "case9.m";
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
