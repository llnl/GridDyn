/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "griddyn/events/Event.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>

// test case for coreObject object

static const char event_test_directory[] = GRIDDYN_TEST_DIRECTORY "/event_tests/";

class EventTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(EventTests, EventTestLoadstring)
{
    std::string fileName = std::string(event_test_directory) + "event_test_loadstring.xml";
    gds = griddyn::readSimXMLFile(fileName);

    griddyn::EventInfo gdEI;
    gdEI.loadString("@2+3|LINK#1:param(MW)=13", gds->getRoot());

    ASSERT_NE(gdEI.targetObjs[0], nullptr);
    EXPECT_EQ(gdEI.fieldList[0], "param");
    EXPECT_EQ(gdEI.value[0], 13);
    EXPECT_EQ(gdEI.units[0], units::MW);
    EXPECT_EQ(gdEI.time[0], griddyn::coreTime(2));
    EXPECT_EQ(gdEI.period, griddyn::coreTime(3));
}

TEST_F(EventTests, EventTest1) {}
TEST_F(EventTests, EventTest2) {}
TEST_F(EventTests, EventTest3) {}
TEST_F(EventTests, EventTest4) {}
