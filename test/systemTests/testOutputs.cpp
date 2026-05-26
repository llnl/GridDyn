/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test cases for the simulation outputs

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/simulation/GridDynSimulationFileOps.h"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace griddyn;
using gmlc::utilities::countDiffs;

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

class OutputTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(OutputTests, OutputTest1)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b2.xml";

    simpleStageCheck(fileName, GridSimulation::GridState::POWERFLOW_COMPLETE);
    savePowerFlowCdf(gds.get(), "testout.cdf");

    ASSERT_TRUE(std::filesystem::exists("testout.cdf"));

    gds2 = std::make_unique<GridDynSimulation>();
    loadFile(gds2.get(), "testout.cdf");
    gds2->powerflow();

    std::vector<double> st1 = gds->getState(cPflowSolverMode);
    std::vector<double> st2 = gds2->getState(cPflowSolverMode);

    auto diff = countDiffs(st1, st2, 0.000001);
    EXPECT_EQ(diff, 0U);
    static_cast<void>(remove("testout.cdf"));
}
