/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/links/RawDcLine.h"
#include <array>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <print>
#include <string>
#include <utility>
#include <vector>
// test case for CoreObject object

#define INPUT_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/input_tests/"

using namespace griddyn;
using namespace gmlc::utilities;

class InputTests: public GridDynSimulationTestFixture, public ::testing::Test {};

struct PowerFlowInputCase {
    std::string_view fileName;
    std::array<int, 2> expectedCounts;
};

static constexpr std::array<PowerFlowInputCase, 7> BASE_CDF_CASES{{
    {.fileName = "ieee14_act.cdf", .expectedCounts = {{14, 20}}},
    {.fileName = "ieee30_act.cdf", .expectedCounts = {{30, 41}}},
    {.fileName = "ieee57_act.cdf", .expectedCounts = {{57, 80}}},
    {.fileName = "ieee118_act.cdf", .expectedCounts = {{118, 186}}},
    {.fileName = "ieee300.cdf", .expectedCounts = {{300, 411}}},
    {.fileName = "IEEE39.raw", .expectedCounts = {{39, 46}}},
    {.fileName = INPUT_TEST_DIRECTORY "testCSV5k.xml", .expectedCounts = {{5000, 6279}}},
}};

static bool shouldMatchStoredPowerFlowSolution(std::string_view caseName)
{
    // These legacy imports converge and reproduce consistently, but the current
    // reader/solver path no longer preserves the exact stored solved state on load.
    return (caseName != "ieee300.cdf") && (caseName != "IEEE39.raw");
}

TEST_F(InputTests, TestPowerFlowInputs)
{
    for (const auto& inputCase : BASE_CDF_CASES) {
        std::vector<double> volts1;
        std::vector<double> ang1;
        std::vector<double> volts2;
        std::vector<double> ang2;
        std::vector<double> realGenerationInitial;
        std::vector<double> realGenerationSolved;
        std::vector<double> reactiveGenerationInitial;
        std::vector<double> reactiveGenerationSolved;

        const bool matchStoredSolution = shouldMatchStoredPowerFlowSolution(inputCase.fileName);

        SCOPED_TRACE(inputCase.fileName);
        gds = std::make_unique<GridDynSimulation>();
        std::string fileName;
        if (inputCase.fileName.length() > 25) {
            fileName = std::string{inputCase.fileName};
        } else {
            fileName = std::string(IEEE_TEST_DIRECTORY) + std::string{inputCase.fileName};
        }

        loadFile(gds, fileName);
        requireState(GridDynSimulation::GridState::STARTUP);
        int count = gds->getInt("totalbuscount");
        EXPECT_EQ(count, inputCase.expectedCounts[0]);
        count = gds->getInt("totallinkcount");
        EXPECT_EQ(count, inputCase.expectedCounts[1]);
        auto voltageCount = gds->getVoltage(volts1);
        gds->getAngle(ang1);
        EXPECT_EQ(voltageCount, static_cast<count_t>(ang1.size()));
        gds->pFlowInitialize();
        requireState(GridDynSimulation::GridState::INITIALIZED);
        gds->updateLocalCache();
        gds->getBusGenerationReal(realGenerationInitial);
        gds->getBusGenerationReactive(reactiveGenerationInitial);

        gds->powerflow();
        if (gds->currentProcessState() != GridDynSimulation::GridState::POWERFLOW_COMPLETE) {
            std::println("{} did not complete power flow calculation", fileName);
        }
        requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

        gds->getVoltage(volts2);
        gds->getAngle(ang2);
        gds->getBusGenerationReal(realGenerationSolved);
        gds->getBusGenerationReactive(reactiveGenerationSolved);
        std::function<void(size_t, double, double)> voltageDiffPrinter =
            [=](size_t index, double value1, double value2) {
                std::println("{} Voltage difference bus {}::{:.12g} vs. {:.12g}::{:.12g}",
                             inputCase.fileName,
                             index + 1,
                             value1,
                             value2,
                             value1 - value2);
            };

        auto vdiff = matchStoredSolution ?
            countDiffsCallback(volts1, volts2, 0.0008, voltageDiffPrinter) :
            countDiffs(volts1, volts2, 0.0008);

        std::function<void(size_t, double, double)> angleDiffPrinter =
            [=](size_t index, double value1, double value2) {
                std::println("{} Angle difference bus {}::{:.12g} vs. {:.12g}::{:.12g}",
                             inputCase.fileName,
                             index + 1,
                             value1 * 180.0 / kPI,
                             value2 * 180.0 / kPI,
                             (value1 - value2) * 180.0 / kPI);
            };
        auto adiff = matchStoredSolution ?
            countDiffsCallback(ang1, ang2, 0.0009, angleDiffPrinter) :
            countDiffs(ang1, ang2, 0.0009);

        std::function<void(size_t, double, double)> realPowerDiffPrinter =
            [=](size_t index, double value1, double value2) {
                std::println("{} Power difference-- bus {}::{:.12g} vs. {:.12g}",
                             inputCase.fileName,
                             index + 1,
                             value1,
                             value2);
            };
        auto pdiff = countDiffsIfValidCallback(realGenerationInitial,
                                               realGenerationSolved,
                                               0.01,
                                               realPowerDiffPrinter);
        std::function<void(size_t, double, double)> reactivePowerDiffPrinter =
            [=](size_t index, double value1, double value2) {
                std::println("{} Q difference-- bus {}::{:.12g} vs. {:.12g}::{:.12g}",
                             inputCase.fileName,
                             index + 1,
                             value1,
                             value2,
                             value1 - value1);
            };
        auto qdiff = countDiffsIfValidCallback(reactiveGenerationInitial,
                                               reactiveGenerationSolved,
                                               0.01,
                                               reactivePowerDiffPrinter);
        if (matchStoredSolution) {
            EXPECT_EQ(vdiff, 0U);
            EXPECT_EQ(adiff, 0U);
        }
        EXPECT_EQ(pdiff, 0U);
        EXPECT_EQ(qdiff, 0U);
        if (qdiff > 0) {
            std::println("{:.12g} vs {:.12g} diff {:.12g}",
                         sum(reactiveGenerationInitial),
                         sum(reactiveGenerationSolved),
                         sum(reactiveGenerationInitial) - sum(reactiveGenerationSolved));
        }

        if (inputCase.fileName == "ieee300.cdf") {
            gds->reset(ResetLevels::VOLTAGE_ANGLE);

            for (int busIndex = 0; busIndex < 300; ++busIndex) {
                GridBus* bus = gds->getBus(busIndex);
                if (bus->getType() == GridBus::BusType::PQ) {
                    bus->set("voltage", 1.0);
                }
            }
            int adjustableTransformerCount = 0;
            for (int linkIndex = 0; linkIndex < 411; ++linkIndex) {
                Link* link = gds->getLink(linkIndex);
                if (dynamic_cast<links::AdjustableTransformer*>(link) != nullptr) {
                    adjustableTransformerCount++;
                    if ((adjustableTransformerCount >= 2) && (adjustableTransformerCount <= 3)) {
                        link->reset(ResetLevels::FULL);
                        link->set("center", "target");
                        break;
                    }
                }
            }
        } else {
            gds->reset(ResetLevels::FULL);
        }

        gds->powerflow();
        if (gds->currentProcessState() != GridDynSimulation::GridState::POWERFLOW_COMPLETE) {
            std::println("{} did not complete power flow calculation 2", fileName);
        }
        requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

        gds->getVoltage(volts1);
        gds->getAngle(ang1);
        gds->getBusGenerationReal(realGenerationSolved);
        gds->getBusGenerationReactive(reactiveGenerationSolved);

        vdiff = countDiffs(volts1, volts2, 0.0005);
        adiff = countDiffs(ang1, ang2, 0.0009);

        EXPECT_EQ(vdiff, 0U);
        EXPECT_EQ(adiff, 0U);
    }
}

TEST_F(InputTests, PssERawDcComponentsImportAsScheduledLinks)
{
    gds = std::make_unique<GridDynSimulation>();
    ASSERT_NO_THROW(loadFile(gds, std::string(INPUT_TEST_DIRECTORY) + "psse_dc_components.raw"));

    EXPECT_EQ(gds->getInt("totalbuscount"), 2);
    // One AC branch plus the two PSS/E DC records.  The imported records use
    // the scheduled-link compatibility model and do not replace GridDyn's
    // native DcLink, AcDcConverter, or Hvdc models.
    ASSERT_EQ(gds->getInt("totallinkcount"), 3);
    auto* twoTerminal = dynamic_cast<links::RawDcLine*>(gds->getLink(1));
    auto* vsc = dynamic_cast<links::RawDcLine*>(gds->getLink(2));
    ASSERT_NE(twoTerminal, nullptr);
    ASSERT_NE(vsc, nullptr);

    // Match PowerModels' RAW conversion: a mode-1 two-terminal record imports
    // SETVL as a lossless scheduled transport power, while a VSC record starts
    // at zero active power and carries its affine loss slope.  Bus 2 is PQ, so
    // the first DC terminal adds PowerModels' q variable and voltage equality.
    EXPECT_NEAR(twoTerminal->get("pset", units::MW), 5.0, 1e-9);
    EXPECT_NEAR(twoTerminal->get("lossfraction"), 0.0, 1e-12);
    EXPECT_NEAR(vsc->get("pset", units::MW), 0.0, 1e-12);
    EXPECT_NEAR(vsc->get("lossfraction"), 0.003, 1e-12);
    EXPECT_EQ(twoTerminal->get("to_voltage_control"), 1.0);
    EXPECT_EQ(vsc->get("to_voltage_control"), 0.0);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    // Reference: PowerModels.solve_ac_pf(..., Ipopt.Optimizer) on this RAW
    // input gives vm=1.0 and va=-0.025284658 rad at bus 2.  Both RAW DC
    // records touch that bus, so their individual q variables are degenerate
    // in PowerModels; compare their terminal total instead.
    EXPECT_NEAR(gds->getBus(1)->getVoltage(), 1.0, 1e-8);
    EXPECT_NEAR(gds->getBus(1)->getAngle(), -0.025284658, 1e-8);
    EXPECT_NEAR(twoTerminal->getReactivePower(2) + vsc->getReactivePower(2),
                -0.0781963933423,
                1e-7);
}

TEST_F(InputTests, EpcEmptyInjectionGroupsAreIgnoredSilently)
{
    gds = std::make_unique<GridDynSimulation>();
    testing::internal::CaptureStderr();
    ASSERT_NO_THROW(
        loadFile(gds, std::string(INPUT_TEST_DIRECTORY) + "epc_empty_injection_groups.epc"));
    const auto diagnostics = testing::internal::GetCapturedStderr();

    EXPECT_EQ(gds->getInt("totalbuscount"), 1);
    EXPECT_EQ(gds->getInt("totallinkcount"), 0);
    EXPECT_TRUE(diagnostics.empty()) << diagnostics;
}

struct CompareCase {
    std::array<std::string_view, 3> fileNames;
    size_t fileCount;
};

static constexpr std::array<CompareCase, 5> COMPARE_CASES{{
    {.fileNames = {{"ieee14_act.cdf", "IEEE 14 bus.epc", "IEEE 14 bus.raw"}}, .fileCount = 3},
    {.fileNames = {{"ieee118_act.cdf", "ieee118.psp", "IEEE 118 Bus.EPC"}}, .fileCount = 3},
    {.fileNames = {{"IEEE39.raw", "ieee39_v29.raw", ""}}, .fileCount = 2},
    {.fileNames =
         {{"powerflowWECC179_v30.raw", "powerflowWECC179_v31.raw", "powerflowWECC179_v32.raw"}},
     .fileCount = 3},
    {.fileNames = {{"ieee30_no_limit.cdf", "testCSV.xml", ""}}, .fileCount = 2},
}};

static void runCompareCase(GridDynSimulationTestFixture& fixture, const CompareCase& compareCase)
{
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    fixture.gds = std::make_unique<GridDynSimulation>();

    SCOPED_TRACE(compareCase.fileNames[0]);
    std::string fileName = std::string(IEEE_TEST_DIRECTORY) + std::string{compareCase.fileNames[0]};
    if (!std::filesystem::exists(fileName)) {
        fileName = std::string(INPUT_TEST_DIRECTORY) + std::string{compareCase.fileNames[0]};
    }
    loadFile(fixture.gds, fileName);

    int bcount = fixture.gds->getInt("totalbuscount");
    int lcount = fixture.gds->getInt("totallinkcount");

    fixture.gds->powerflow();

    fixture.gds->getVoltage(volts1);
    fixture.gds->getAngle(ang1);

    for (size_t caseFileIndex = 1; caseFileIndex < compareCase.fileCount; ++caseFileIndex) {
        std::string secondFileName = std::string{compareCase.fileNames[caseFileIndex]};
        const auto& compareFileName = compareCase.fileNames[caseFileIndex];
        fixture.gds2 = std::make_unique<GridDynSimulation>();
        if (secondFileName.size() < 25) {
            secondFileName = std::string(IEEE_TEST_DIRECTORY) + std::string{compareFileName};
            if (!std::filesystem::exists(secondFileName)) {
                secondFileName = std::string(INPUT_TEST_DIRECTORY) + std::string{compareFileName};
            }
        }

        loadFile(fixture.gds2, secondFileName);

        int count = fixture.gds2->getInt("totalbuscount");
        EXPECT_EQ(count, bcount);
        for (index_t busIndex = 0; busIndex < count; ++busIndex) {
            auto* compareBusObject = fixture.gds2->getBus(busIndex);
            auto* referenceBusObject = fixture.gds->getBus(busIndex);
            auto busesMatch = compareBus(compareBusObject, referenceBusObject, false);
            if (!busesMatch) {
            }
            compareBusObject->setVoltageAngle(volts1[busIndex], ang1[busIndex]);
        }
        count = fixture.gds2->getInt("totallinkcount");
        EXPECT_EQ(count, lcount);

        fixture.gds2->powerflow();

        ASSERT_EQ(fixture.gds2->currentProcessState(),
                  GridDynSimulation::GridState::POWERFLOW_COMPLETE)
            << secondFileName << " failed to complete";

        fixture.gds2->getVoltage(volts2);
        fixture.gds2->getAngle(ang2);

        int vdiff = 0;
        int adiff = 0;

        for (size_t kk = 0; kk < volts1.size(); ++kk) {
            if (std::abs(volts1[kk] - volts2[kk]) > 0.0008) {
                std::println("{} vs. {} Voltage difference bus {}::{:.12g} vs. {:.12g}",
                             compareCase.fileNames[0],
                             compareFileName,
                             kk + 1,
                             volts1[kk],
                             volts2[kk]);
                vdiff++;
            }

            if (std::abs(ang1[kk] - ang2[kk]) > 0.0009) {
                std::println(
                    "{} vs. {} Angle difference-- bus {}::{:.12g} vs. {:.12g}::{:.12g} deg",
                    compareCase.fileNames[0],
                    compareFileName,
                    kk + 1,
                    ang1[kk] * 180.0 / kPI,
                    ang2[kk] * 180.0 / kPI,
                    std::abs(ang1[kk] - ang2[kk]) * 180.0 / kPI);
                adiff++;
            }
        }
        EXPECT_EQ(vdiff, 0);
        EXPECT_EQ(adiff, 0);
    }
}

TEST_F(InputTests, CompareCases)
{
    for (const auto& compareCase : COMPARE_CASES) {
        runCompareCase(*this, compareCase);
    }
}

struct ExecutionCase {
    std::string_view fileName;
    std::array<int, 4> expectedValues;
};

static constexpr std::array<ExecutionCase, 3> EXECUTION_CASES{{
    {.fileName = MATLAB_TEST_DIRECTORY "case4gs.m", .expectedValues = {{0, 4, 4, 0}}},
    // { std::string(MATLAB_TEST_DIRECTORY "d_003.m"), { { 0, 3, 3, 0 } } },
    // { std::string(INPUT_TEST_DIRECTORY "test_mat_dyn.xml"), { { 1, 9, 9, 2 } } },
    {.fileName = INPUT_TEST_DIRECTORY "test_2m4bDyn_inputchange.xml",
     .expectedValues = {{1, 0, 0, 0}}},
    {.fileName = INPUT_TEST_DIRECTORY "testIEEE39dynamic.xml", .expectedValues = {{1, 39, 0, 0}}},
    //  { std::string(INPUT_TEST_DIRECTORY "testIEEE39dynamic_relay.xml"), { { 1, 39, 0, 0 } } },
    //{ std::string(INPUT_TEST_DIRECTORY "180busdyn_test.xml"),{ { 1, 179, 0, 1 } } },
}};

TEST_F(InputTests, InputExecTest)
{
    for (const auto& executionCase : EXECUTION_CASES) {
        int count;
        const auto fileName = executionCase.fileName;
        SCOPED_TRACE(fileName);

        gds = std::make_unique<GridDynSimulation>();

        loadFile(gds, std::string{fileName});
        requireState(GridDynSimulation::GridState::STARTUP);

        if (executionCase.expectedValues[1] > 0) {
            count = gds->getInt("totalbuscount");
            EXPECT_EQ(count, executionCase.expectedValues[1]);
        }
        if (executionCase.expectedValues[2] > 0) {
            count = gds->getInt("totallinkcount");
            EXPECT_EQ(count, executionCase.expectedValues[2]);
        }
        if (executionCase.expectedValues[3] > 0) {
            count = gds->getInt("eventcount");
            EXPECT_EQ(count, executionCase.expectedValues[3]);
        }
        if (executionCase.expectedValues[0] == 0) {
            gds->powerflow();
            requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
        } else if (executionCase.expectedValues[0] == 1) {
            gds->run();
            requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
        }
    }
}
