/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/measurement/collector.h"

#include <gtest/gtest.h>

#define EXTRAMODEL_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/extraModel_tests/"

using namespace griddyn;

class ExtraModelTests: public gridDynSimulationTestFixture, public ::testing::Test {
};

TEST_F(ExtraModelTests, TestThermaltxTxage)
{
    std::string fileName = std::string(EXTRAMODEL_TEST_DIRECTORY "test_thermaltx_txage.xml");
    loadLibraries();
    runTestXML(fileName, gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    auto col = gds->findCollector("collector#0");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->numberOfPoints(), 14);

    EXPECT_EQ(col->getWarningCount(), 0);
}
