/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <cstdio>

#include <gtest/gtest.h>

using namespace griddyn;

#define CONSTRAINT_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/constraint_tests/"

class ConstraintTests: public gridDynSimulationTestFixture, public ::testing::Test {
};

TEST_F(ConstraintTests, ConstraintTest1)
{
    std::string fileName = std::string(CONSTRAINT_TEST_DIRECTORY "test_constSimple1.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->consolePrintLevel = print_level::no_print;

    gds->powerflow();
    printf("completed power flow\n");
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->run(30.0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
