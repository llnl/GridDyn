/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreObject.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/events/CompoundEventPlayer.h"
#include "griddyn/events/Event.h"
#include "griddyn/events/Player.h"
#include "griddyn/measurement/Recorder.h"
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

// test case for CoreObject object

static const char event_test_directory[] = GRIDDYN_TEST_DIRECTORY "/event_tests/";

class EventTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(EventTests, HelpersIgnoreEmptyAndCommentKeys)
{
    griddyn::events::CompoundEventPlayer player;
    griddyn::Recorder recorder;
    for (auto* helper : {static_cast<griddyn::HelperObject*>(&player),
                         static_cast<griddyn::HelperObject*>(&recorder)}) {
        for (const auto* key : {"", "# comment"}) {
            EXPECT_NO_THROW(helper->set(key, "ignored"));
            EXPECT_NO_THROW(helper->set(key, std::numeric_limits<double>::quiet_NaN()));
            EXPECT_NO_THROW(helper->setFlag(key, true));
        }
        EXPECT_THROW(helper->set("unknown", "value"), griddyn::UnrecognizedParameter);
    }
}

TEST_F(EventTests, NumericArmedUsesTheHelperFlagRule)
{
    griddyn::Event event;
    event.set("armed", -0.05);
    EXPECT_TRUE(event.isArmed());
    event.set("armed", 0.0);
    EXPECT_FALSE(event.isArmed());
    EXPECT_THROW(event.set("armed", std::numeric_limits<double>::quiet_NaN()),
                 griddyn::InvalidParameterValue);
    EXPECT_FALSE(event.isArmed());
}

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
    EXPECT_EQ(gdEI.time[0], griddyn::CoreTime(2));
    EXPECT_EQ(gdEI.period, griddyn::CoreTime(3));
}

TEST_F(EventTests, PlayerPeriodChangesRepeatTimeWithoutChangingTheSample)
{
    griddyn::CoreObject target;
    griddyn::events::Player player;
    ASSERT_TRUE(player.setTarget(&target, "id"));
    player.setTimeValue(std::vector<griddyn::CoreTime>{1.0, 2.0},
                        std::vector<double>{3.0, 4.0});
    player.set("period", 5.0);

    player.trigger(1.0);
    EXPECT_EQ(target.getUserID(), 3);
    player.trigger(2.0);
    EXPECT_EQ(target.getUserID(), 4);
    EXPECT_EQ(player.nextTriggerTime(), griddyn::CoreTime(6.0));
}
