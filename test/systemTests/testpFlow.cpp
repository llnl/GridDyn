/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/solvers/solverInterface.h"
#include <array>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;
using gmlc::utilities::countDiffs;

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

class PowerflowSystemTests: public gridDynSimulationTestFixture, public ::testing::Test {};

/** test to make sure the basic power flow loads and runs*/
TEST_F(PowerflowSystemTests, PFlowTest1)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);

    gds->pFlowInitialize();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::INITIALIZED);

    int count = gds->getInt("totalbuscount");

    EXPECT_EQ(count, 9);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 9);

    EXPECT_EQ(runJacobianCheck(gds, cPflowSolverMode, false), 0);
    EXPECT_EQ(gds->getInt("jacsize"), 108);
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

// testcase for power flow from initial start
TEST_F(PowerflowSystemTests, PFlowTest2)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b2.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);
    gds->pFlowInitialize();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::INITIALIZED);

    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->getVoltage(volts2);
    gds->getAngle(ang2);
    // ensure the sizes are equal
    ASSERT_EQ(volts1.size(), volts2.size());
    // check the bus voltages and angles
    auto vdiff = countDiffs(volts1, volts2, 0.0001);
    auto adiff = countDiffs(ang1, ang2, 0.0001);

    EXPECT_EQ(vdiff, 0);
    EXPECT_EQ(adiff, 0);
}

// testcase for power flow from zeros start
TEST_F(PowerflowSystemTests, PFlowTest3)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b.xml";
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);
    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    std::string fname2 = std::string(pFlow_test_directory) + "test_powerflow3m9b2.xml";
    gds2 = readSimXMLFile(fname2);
    requireState2(gridDynSimulation::gridState_t::STARTUP);
    gds2->pFlowInitialize();
    requireState2(gridDynSimulation::gridState_t::INITIALIZED);

    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    gds2->powerflow();
    requireState2(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds2->getVoltage(volts2);
    gds2->getAngle(ang2);
    // ensure the sizes are equal
    ASSERT_EQ(volts1.size(), volts2.size());
    // check the bus voltages and angles
    auto vdiff = countDiffs(volts1, volts2, 0.0001);
    auto adiff = countDiffs(ang1, ang2, 0.0001);
    EXPECT_EQ(vdiff, 0);
    EXPECT_EQ(adiff, 0);
}

/** test the ieee 30 bus case with no shunts*/
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PowerflowSystemTests, PflowTest30NoShunt)
{
    gds = std::make_unique<gridDynSimulation>();
    std::string fileName = std::string(ieee_test_directory) + "ieee30_no_shunt_cap_tap_limit.cdf";

    loadCdf(gds.get(), fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 30);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 41);
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->getVoltage(volts2);
    gds->getAngle(ang2);

    auto vdiff = countDiffs(volts1, volts2, 0.001);
    auto adiff = countDiffs(ang1, ang2, 0.001);

    if ((vdiff > 0) || (adiff > 0)) {
        printBusResultDeviations(volts1, volts2, ang1, ang2);
    }
    EXPECT_EQ(vdiff, 0U);
    EXPECT_EQ(adiff, 0U);

    // check that the reset works correctly
    gds->reset(reset_levels::voltage_angle);
    gds->getAngle(ang1);
    for (double angleValue : ang1) {
        EXPECT_NEAR(angleValue, 0.0, 1e-6);
    }

    gds->pFlowInitialize();
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->getVoltage(volts1);
    gds->getAngle(ang1);
    auto vdiff2 = countDiffs(volts1, volts2, 0.0005);
    auto adiff2 = countDiffs(volts1, volts2, 0.0009);

    EXPECT_EQ(vdiff2, 0U);
    EXPECT_EQ(adiff2, 0U);
}

/** test the IEEE 30 bus case with no reactive limits*/
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PowerflowSystemTests, PflowTest30NoLimit)
{
    gds = std::make_unique<gridDynSimulation>();
    std::string fileName = std::string(ieee_test_directory) + "ieee30_no_limit.cdf";

    loadCdf(gds.get(), fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 30);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 41);
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->getVoltage(volts2);
    gds->getAngle(ang2);

    auto vdiff = countDiffs(volts1, volts2, 0.001);
    auto adiff = countDiffs(ang1, ang2, 0.01 * kPI / 180.0);  // 0.01 degrees
    if ((vdiff > 0) || (adiff > 0)) {
        printBusResultDeviations(volts1, volts2, ang1, ang2);
    }
    EXPECT_EQ(vdiff, 0U);
    EXPECT_EQ(adiff, 0U);

    // check that the reset works correctly
    gds->reset(reset_levels::voltage_angle);
    gds->getAngle(ang1);
    for (double angleValue : ang1) {
        EXPECT_NEAR(angleValue, 0.0, 1e-6);
    }
    gds->pFlowInitialize();
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->getVoltage(volts1);
    gds->getAngle(ang1);
    auto vdiff2 = countDiffs(volts1, volts2, 0.0005);
    auto adiff2 = countDiffs(volts1, volts2, 0.0009);

    EXPECT_EQ(vdiff2, 0U);
    EXPECT_EQ(adiff2, 0U);
}

// test case for power flow automatic adjustment
TEST_F(PowerflowSystemTests, TestPFlowPadjust)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b_Padjust.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    std::vector<double> generation1;
    std::vector<double> generation2;

    gds->pFlowInitialize();
    gds->updateLocalCache();
    gds->getBusGenerationReal(generation1);
    gds->powerflow();
    auto warnCount = gds->getInt("warncount");
    EXPECT_EQ(warnCount, 0);
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->getBusGenerationReal(generation2);
    // there should be 2 generators+ the swing bus that had their real power levels adjusted instead
    // of just the swing bus
    auto cnt = countDiffs(generation2, generation1, 0.0002);
    EXPECT_EQ(cnt, 3U);
}

/** test case for dc power flow*/
TEST_F(PowerflowSystemTests, PflowTestDcflow)
{
    gds = std::make_unique<gridDynSimulation>();
    std::string fileName = std::string(ieee_test_directory) + "ieee30_no_limit.cdf";

    loadCdf(gds.get(), fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 30);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 41);
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    auto newSolver = makeSolver("kinsol");
    newSolver->setName("dcflow");
    newSolver->set("mode", "dc, algebraic");
    gds->add(std::shared_ptr<SolverInterface>(std::move(newSolver)));

    auto smode = gds->getSolverMode("dcflow");
    gds->set("defpowerflow", "dcflow");
    gds->pFlowInitialize(0.0);
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    runJacobianCheck(gds, smode);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

// iterated power flow test case
TEST_F(PowerflowSystemTests, DISABLED_TestIteratedPflow)
{
    std::string fileName = std::string(pFlow_test_directory) + "iterated_test_case.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    requireState(gridDynSimulation::gridState_t::STARTUP);
    gds->consolePrintLevel = print_level::no_print;
    gds->set("recorddirectory", pFlow_test_directory);
    gds->run();
    ASSERT_GT(gds->getSimulationTime(), 575.0);
}

/** test case for a floating bus ie a bus off a line with no load*/
TEST_F(PowerflowSystemTests, PFlowTestFloatingBus)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b_float.xml";
    gds = readSimXMLFile(fileName);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    runJacobianCheck(gds, cPflowSolverMode);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

/** test case for a single line breaker trip*/
TEST_F(PowerflowSystemTests, PflowTestSingleBreaker)
{
    std::string fileName = std::string(pFlow_test_directory) + "line_single_breaker_trip.xml";
    gds = readSimXMLFile(fileName);

    gds->run(5.0);
    EXPECT_GE(static_cast<double>(gds->getSimulationTime()), 5.0);
    // gds->timestep(2.05,noInputs,cPflowSolverMode);
    // runJacobianCheck(gds, cPflowSolverMode);
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}
static constexpr std::array<std::string_view, 9> approxModes{
    "normal",
    "simple",
    "decoupled",
    "fast_decoupled",
    "simplified_decoupled",
    "small_angle",
    "small_angle_decoupled",
    "small_angle_simplified",
    "linear",
};

TEST_F(PowerflowSystemTests, PflowTestLineModes)
{
    for (const auto& approx : approxModes) {
        SCOPED_TRACE(approx);
        gds = std::make_unique<gridDynSimulation>();
        std::string fileName = std::string(ieee_test_directory) + "ieee30_no_limit.cdf";

        loadCdf(gds.get(), fileName);

        requireState(gridDynSimulation::gridState_t::STARTUP);

        int count = gds->getInt("totalbuscount");
        EXPECT_EQ(count, 30);
        count = gds->getInt("totallinkcount");
        EXPECT_EQ(count, 41);
        std::vector<double> volts1;
        std::vector<double> ang1;
        std::vector<double> volts2;
        std::vector<double> ang2;

        gds->getVoltage(volts1);
        gds->getAngle(ang1);

        auto newSolver = makeSolver("kinsol");
        newSolver->setName(std::string{approx});
        try {
            newSolver->set("approx", std::string{approx});
        }
        catch (const invalidParameterValue&) {
            ADD_FAILURE() << "unrecognized approx mode " << approx;
            continue;
        }

        gds->add(std::shared_ptr<SolverInterface>(std::move(newSolver)));
        auto smode = gds->getSolverMode(std::string{approx});
        gds->set("defpowerflow", std::string{approx});
        gds->pFlowInitialize(0.0);
        requireState(gridDynSimulation::gridState_t::INITIALIZED);
        int errors;
        if (approx == "small_angle_decoupled") {
            errors = runJacobianCheck(gds, smode, 0.05, false);
        } else {
            errors = runJacobianCheck(gds, smode, false);
        }
        ASSERT_EQ(errors, 0) << "Errors in " << approx << " mode";

        gds->powerflow();
        requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    }
}
