/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Link.h"
#include "griddyn/gridBus.h"
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <string>
// testP case for coreObject object

#define AREA_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/area_tests/"

using namespace griddyn;

class AreaTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(AreaTests, AreaTest1)
{
    std::string fileName = std::string(AREA_TEST_DIRECTORY "area_test1.xml");

    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    int count;
    count = gds->getInt("totalareacount");
    EXPECT_EQ(count, 1);
    count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 9);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 9);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    auto st = gds->getState();

    fileName = std::string(AREA_TEST_DIRECTORY "area_test0.xml");

    gds2 = readSimXMLFile(fileName);

    gds2->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    auto st2 = gds2->getState();
    auto diffs = gmlc::utilities::countDiffs(st, st2, 0.00001);
    EXPECT_EQ(diffs, 0);
}

TEST_F(AreaTests, AreaTestAdd)
{
    auto area = std::make_unique<Area>("area1");

    auto bus1 = new gridBus("bus1");
    try {
        area->add(bus1);
        area->add(bus1);
    }
    catch (...) {
        FAIL();
    }

    auto bus2 = bus1->clone();
    try {
        area->add(bus2);
        // this is testing failure
        FAIL();
    }
    catch (const objectAddFailure& oaf) {
        EXPECT_EQ(oaf.who(), "area1");
    }
    bus2->setName("bus2");
    try {
        area->add(bus2);
        EXPECT_TRUE(isSameObject(bus2->getParent(), area.get()));
    }
    catch (...) {
        FAIL();
    }
}
