/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "coupling/GhostSwingBusManager.h"
#include "griddyn/loads/approximatingLoad.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <string>

#define GRIDLAB_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/gridlabD_tests/"

using namespace griddyn;

class GridLabTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(GridLabTests, GridlabTest1)
{
    int argc = 0;
    GhostSwingBusManager::initialize(&argc, nullptr);
    GhostSwingBusManager::setDebug(false);
    std::string fileName = std::string(GRIDLAB_TEST_DIRECTORY "Simple_3Bus_mod.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int glb = gds->countMpiObjects();
    EXPECT_EQ(glb, 1);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    zipLoad* ld = static_cast<zipLoad*>(gds->find("bus2::gload1"));

    ASSERT_NE(ld, nullptr);
    double val = ld->get("p");
    EXPECT_NEAR(val, 0.9, std::abs(0.9) * 1e-4 + 1e-12);
    val = ld->get("yp");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("yq");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("ip");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("iq");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("q");
    EXPECT_NEAR(val, 0.27, std::abs(0.27) * 1e-4 + 1e-12);

    GhostSwingBusManager::instance()->endSimulation();
}

TEST_F(GridLabTests, GridlabTest2)
{
    int argc = 0;
    GhostSwingBusManager::initialize(&argc, nullptr);
    GhostSwingBusManager::setDebug(false);
    std::string fileName = std::string(GRIDLAB_TEST_DIRECTORY "Simple_3Bus_mod3x.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int glb = gds->countMpiObjects();
    EXPECT_EQ(glb, 3);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    zipLoad* ld = static_cast<zipLoad*>(gds->find("bus2::gload2"));

    // P = 0.27 Q = -0.1 Ir = 0.34 Iq = -0.13
    ASSERT_NE(ld, nullptr);
    double val = ld->get("p");
    EXPECT_NEAR(val, 0.3, std::abs(0.3) * 1e-4 + 1e-12);
    val = ld->get("yp");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("yq");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("ip");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("iq");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("q");
    EXPECT_NEAR(val, 0.09, std::abs(0.09) * 1e-4 + 1e-12);

    gds->run(30.0);
    GhostSwingBusManager::instance()->endSimulation();
}

TEST_F(GridLabTests, GridlabTest3)
{
    int argc = 0;
    GhostSwingBusManager::initialize(&argc, nullptr);
    GhostSwingBusManager::setDebug(false);
    std::string fileName = std::string(GRIDLAB_TEST_DIRECTORY "Simple_3Bus_mod3x_current.xml");
    gds = readSimXMLFile(fileName);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int glb = gds->countMpiObjects();
    EXPECT_EQ(glb, 3);

    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    zipLoad* ld = static_cast<zipLoad*>(gds->find("bus2::gload2"));

    // P = 0.27 Q = -0.1 Ir = 0.34 Iq = -0.13
    ASSERT_NE(ld, nullptr);
    double val = ld->get("p");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("yp");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("yq");
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld->get("ip");
    EXPECT_NEAR(val, 0.3, std::abs(0.3) * 1e-4 + 1e-12);
    val = ld->get("iq");
    EXPECT_NEAR(val, 0.09, std::abs(0.09) * 1e-4 + 1e-12);
    val = ld->get("q");
    EXPECT_NEAR(val, 0.0, 1e-6);

    gds->run(30.0);
    GhostSwingBusManager::instance()->endSimulation();
}

TEST_F(GridLabTests, TestGridlabArray)
{
    // test the define functionality
    int argc = 0;
    std::string fileName = std::string(GRIDLAB_TEST_DIRECTORY "Simple_3Bus_mod3x_mix_scale.xml");
    GhostSwingBusManager::initialize(&argc, nullptr);
    GhostSwingBusManager::setDebug(false);
    readerInfo ri;
    ri.keepdefines = true;
    gds = readSimXMLFile(fileName, &ri);
    requireState(gridDynSimulation::gridState_t::STARTUP);

    int glb = gds->countMpiObjects();
    int cnt = 60;
    std::string b = ri.checkDefines("garraySize");
    if (b != "garraySize") {
        cnt = 3 * std::stoi(b);
    }
    b = ri.checkDefines("outerSize");
    if (b != "outerSize") {
        cnt = cnt * std::stoi(b);
    }
    EXPECT_EQ(glb, cnt);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
