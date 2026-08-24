/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModel6.h"
#include "griddyn/governors/GovernorIeeeSimple.h"
#include <gtest/gtest.h>
#include <string>

#define GEN_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/gen_tests/"

using namespace griddyn;

class GeneratorTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST(DynamicGeneratorModelTests, MechanicalPowerSourceCanBeExternalAndIndexed)
{
    DynamicGenerator primary;
    auto* governor = new governors::GovernorIeeeSimple();
    primary.add(governor);

    EXPECT_EQ(primary.getMechanicalPowerSource(), governor);
    EXPECT_EQ(primary.getMechanicalPowerOutput(), 0);
    EXPECT_FALSE(primary.hasExplicitMechanicalPowerSource());

    auto* multiOutputSource = new genmodels::GenModel6();
    primary.add(multiOutputSource);

    DynamicGenerator secondary;
    secondary.setMechanicalPowerSource(multiOutputSource, 1);
    EXPECT_EQ(secondary.getMechanicalPowerSource(), multiOutputSource);
    EXPECT_EQ(secondary.getMechanicalPowerOutput(), 1);
    EXPECT_TRUE(secondary.hasExplicitMechanicalPowerSource());

    EXPECT_THROW(secondary.setMechanicalPowerSource(multiOutputSource, 2), InvalidParameterValue);

    secondary.clearMechanicalPowerSource();
    EXPECT_EQ(secondary.getMechanicalPowerSource(), nullptr);
    EXPECT_FALSE(secondary.hasExplicitMechanicalPowerSource());
}

TEST_F(GeneratorTests, MechanicalPowerSourcePathResolvesAfterTreeAssembly)
{
    const std::string fileName =
        std::string(GRIDDYN_TEST_DIRECTORY "/governor_tests/test_gov_stability.xml");
    gds = readSimXMLFile(fileName);

    auto* bus1 = dynamic_cast<GridBus*>(gds->find("bus1"));
    auto* bus2 = dynamic_cast<GridBus*>(gds->find("bus2"));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);

    auto* primary = dynamic_cast<DynamicGenerator*>(bus1->getGen(0));
    auto* secondary = dynamic_cast<DynamicGenerator*>(bus2->getGen(0));
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);

    auto* governor = new governors::GovernorIeeeSimple("shared_governor");
    primary->add(governor);
    secondary->add(new genmodels::GenModel6());
    ASSERT_NE(dynamic_cast<genmodels::GenModel6*>(secondary->find("genmodel")), nullptr);
    secondary->set("mechanical_power_output", 0.0);
    secondary->set("mechanical_power_source", fullObjectName(governor));
    EXPECT_EQ(secondary->getMechanicalPowerSource(), nullptr);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(secondary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerOutput(), 0);
}

TEST_F(GeneratorTests, GenTestRemote)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_remote.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

TEST_F(GeneratorTests, GenTestRemoteB)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_remote_b.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
}

TEST_F(GeneratorTests, GenTestRemote2)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_dualremote.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

TEST_F(GeneratorTests, GenTestRemote2B)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_dualremote_b.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
}

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(GeneratorTests, GenTestIsoc)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_isoc2.xml");

    gds = readSimXMLFile(fileName);

    gds->set("recorddirectory", GEN_TEST_DIRECTORY);

    gds->run();

    std::string recname = std::string(GEN_TEST_DIRECTORY "datafile.dat");
    TimeSeriesMulti<> ts3(recname);
    ASSERT_GT(ts3.size(), 30u);
    EXPECT_LT(ts3.data(0, 30), 0.995);
    EXPECT_GT(ts3[0].back(), 1.0);

    EXPECT_GT(ts3.data(1, 0) - ts3[1].back(), 0.199);
    remove(recname.c_str());
}
#endif
