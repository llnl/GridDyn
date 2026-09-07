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
#include <vector>

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#    include "optimization/models/gridBusOpt.h"
#    include "optimization/models/gridGenOpt.h"
#    include "optimization/models/gridLinkOpt.h"
#    include "optimization/optHelperClasses.h"

#endif

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
    const auto filePath =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / "case2.py";

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds, filePath.string());

    EXPECT_EQ(gds->getInt("totalbuscount"), 2);
    EXPECT_EQ(gds->getInt("totallinkcount"), 1);
    EXPECT_EQ(gds->getInt("gencount"), 1);
    EXPECT_EQ(gds->getInt("loadcount"), 1);
}

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
TEST(ExampleReaderTests, LoadPyPowerGeneratorCost)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / "case2.py";

    griddyn::loadFile(gds.get(), filePath.string());

    auto* generator = gds->findByUserID("gen", 1);
    ASSERT_NE(generator, nullptr);
    auto* generatorOpt = dynamic_cast<griddyn::GridGenOpt*>(gds->getOptimizationObject(generator));
    ASSERT_NE(generatorOpt, nullptr);

    const griddyn::OptimizationMode mode{griddyn::FlowModel::DC,
                                         griddyn::LinearityMode::QUADRATIC,
                                         0,
                                         1,
                                         1.0};
    generatorOpt->loadSizes(mode);
    generatorOpt->setOffset(0, 0, mode);
    // MATPOWER gencost uses MW, whereas GridDyn's optimization variables use pu.
    const double dispatch = 0.1;
    const griddyn::OptimizationData optimizationData{0.0, &dispatch, 0};

    // case2.py specifies 0.01 * Pg^2 + Pg.
    EXPECT_DOUBLE_EQ(generatorOpt->objValue(optimizationData, mode), 11.0);
}


TEST(ExampleReaderTests, TwoBusDcBranchSourceAndFlow)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath =
        std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / "case2.py";
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = gds->findByUserID("link", 1);
    ASSERT_NE(physicalBranch, nullptr);
    EXPECT_NEAR(physicalBranch->get("x"), 0.1, 1e-12);

    const griddyn::OptimizationMode mode{griddyn::FlowModel::DC,
                                         griddyn::LinearityMode::LINEAR,
                                         0,
                                         1,
                                         1.0};
    gds->initializeOptimizationModel(mode, 0);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(branch, nullptr);
    ASSERT_NE(generator, nullptr);
    ASSERT_EQ(branch->getBus(1), bus1);
    ASSERT_EQ(branch->getBus(2), bus2);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);
    // Flat optimizer offsets follow the simulation convention: index zero is reserved.
    std::vector<double> values(root->objSize(mode) + 1, 0.0);
    ASSERT_LT(bus1Offsets.aOffset, values.size());
    ASSERT_LT(bus2Offsets.aOffset, values.size());
    ASSERT_LT(generatorOffsets.gOffset, values.size());
    ASSERT_NE(bus1Offsets.aOffset, bus2Offsets.aOffset);
    values[generatorOffsets.gOffset] = 0.5;
    values[bus1Offsets.aOffset] = 0.0;
    values[bus2Offsets.aOffset] = -0.05;

    const griddyn::OptimizationData data{0.0, values.data(), 0};
    EXPECT_NEAR(branch->dcPowerFlow(bus1, data, mode), 0.5, 1e-12);
    EXPECT_NEAR(branch->dcPowerFlow(bus2, data, mode), -0.5, 1e-12);
    std::vector<double> residuals(root->constraintSize(mode), 0.0);
    root->constraintValue(data, residuals.data(), mode);
    EXPECT_NEAR(residuals[bus1Offsets.constraintOffset], 0.0, 1e-12);
    EXPECT_NEAR(residuals[bus2Offsets.constraintOffset], 0.0, 1e-12);
}
#endif

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
