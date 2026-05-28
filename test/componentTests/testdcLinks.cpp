/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/TimeSeries.hpp"
#include "griddyn/simulation/Diagnostics.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <string>

#define HVDC_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/dcLink_tests/"

class HvdcTests: public GridDynSimulationTestFixture, public ::testing::Test {};

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(HvdcTests, HvdcTest1)
{
    std::string fileName = std::string(HVDC_TEST_DIRECTORY "test_hvdc1.xml");
    gds = readSimXMLFile(fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    gds->pFlowInitialize();
    requireState(GridDynSimulation::GridState::INITIALIZED);

    int mmatch = jacobianCheck(gds, cPflowSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cPflowSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->powerflow();

    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    gds->dynInitialize();
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    mmatch = jacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(HvdcTests, HvdcTest2)
{
    std::string fileName = std::string(HVDC_TEST_DIRECTORY "test_hvdc2.xml");
    gds = readSimXMLFile(fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    gds->dynInitialize();
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    int mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    mmatch = jacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->run(20);
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    gds->run(40);
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(HvdcTests, HvdcTest3)
{
    std::string fileName = std::string(HVDC_TEST_DIRECTORY "test_hvdc3_sc.xml");
    gds = readSimXMLFile(fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    gds->dynInitialize();
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    int mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    mmatch = jacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    gds->run(20);
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    gds->run(40);
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}
#endif
