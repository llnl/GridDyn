/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <string>

using namespace griddyn;

#define CONSTRAINT_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/constraint_tests/"

class ConstraintTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(ConstraintTests, ConstraintTest1)
{
    std::string fileName = std::string(CONSTRAINT_TEST_DIRECTORY "test_constSimple1.xml");
    gds = readSimXMLFile(fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    gds->consolePrintLevel = PrintLevel::NO_PRINT;

    gds->powerflow();
    printf("completed power flow\n");
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds->run(30.0);
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

