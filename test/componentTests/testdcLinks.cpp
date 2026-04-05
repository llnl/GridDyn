/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/TimeSeries.hpp"
#include "griddyn/simulation/diagnostics.h"
#include <cstdio>
#include <gtest/gtest.h>

#define HVDC_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/dcLink_tests/"

class HvdcTests: public gridDynSimulationTestFixture, public ::testing::Test {};

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(HvdcTests, HvdcTest1)
{
    std::string fileName = std::string(HVDC_TEST_DIRECTORY "test_hvdc1.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    int mmatch = JacobianCheck(gds, cPflowSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cPflowSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    mmatch = JacobianCheck(gds, cDaeSolverMode);
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
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    int mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    mmatch = JacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->run(20);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);

    gds->run(40);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(HvdcTests, HvdcTest3)
{
    std::string fileName = std::string(HVDC_TEST_DIRECTORY "test_hvdc3_sc.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    int mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    mmatch = JacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    gds->run(20);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);

    gds->run(40);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif
