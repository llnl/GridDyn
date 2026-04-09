/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "griddyn/links/acLine.h"
#include "griddyn/links/adjustableTransformer.h"
#include "griddyn/simulation/diagnostics.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
// testP case for coreObject object

#define TADJ_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/adj_tests/"

using namespace griddyn;

class AdjustableTransformerTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(AdjustableTransformerTests, AdjTestSimple)
{
    std::string fileName = std::string(TADJ_TEST_DIRECTORY "adj_test1.xml");

    gds = readSimXMLFile(fileName);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> st;
    gds->getVoltage(st);
    EXPECT_GE(st[2], 0.99);
    EXPECT_LE(st[2], 1.01);

    // tap changing doesn't do anything in this case we are checking to make sure the tap goes all
    // the way
    fileName = std::string(TADJ_TEST_DIRECTORY "adj_test2.xml");
    gds2 = readSimXMLFile(fileName);
    gds2->powerflow();
    requireStates(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    auto adj = dynamic_cast<links::adjustableTransformer*>(gds2->getLink(1));
    ASSERT_NE(adj, nullptr);
    EXPECT_NEAR(adj->getTap() - 1.1, 0.0, 1e-3);
}

TEST_F(AdjustableTransformerTests, AdjTestSimple2)
{
    // test multiple interacting controllers
    std::string fileName = std::string(TADJ_TEST_DIRECTORY "adj_test3.xml");

    gds = readSimXMLFile(fileName);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> st;
    gds->getVoltage(st);
    EXPECT_GE(st[1], 0.99);
    EXPECT_LE(st[1], 1.01);
    EXPECT_GE(st[2], 0.99);
    EXPECT_LE(st[2], 1.01);

    // test multiple interacting controllers voltage reduction mode
    fileName = std::string(TADJ_TEST_DIRECTORY "adj_test4.xml");

    gds2 = readSimXMLFile(fileName);
    gds2->powerflow();
    ASSERT_EQ(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds2->getVoltage(st);
    EXPECT_GE(st[1], 0.99);
    EXPECT_LE(st[1], 1.01);
    EXPECT_GE(st[2], 0.99);
    EXPECT_LE(st[2], 1.01);

    // test a remote control bus adjustable link between 1 and 3 and controlling bus 4
    fileName = std::string(TADJ_TEST_DIRECTORY "adj_test5.xml");

    gds = readSimXMLFile(fileName);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds->getVoltage(st);
    EXPECT_GE(st[2], 0.99);
    EXPECT_LE(st[2], 1.011);
}

// now test the stepped MW control
TEST_F(AdjustableTransformerTests, AdjTestMw)
{
    std::string fileName = std::string(TADJ_TEST_DIRECTORY "adj_test6.xml");

    gds = readSimXMLFile(fileName);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> st;
    gds->getLinkRealPower(st);

    EXPECT_LE(st[0], 1.05);
    EXPECT_GE(st[0], 0.95);
}

// now test the stepped MVar control
TEST_F(AdjustableTransformerTests, AdjTestMvar)
{
    std::string fileName = std::string(TADJ_TEST_DIRECTORY "adj_test7.xml");

    gds = readSimXMLFile(fileName);
    gds->pFlowInitialize();
    int mmatch = runJacobianCheck(gds, cPflowSolverMode);

    ASSERT_EQ(mmatch, 0);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> st;
    gds->getLinkReactivePower(st, 0, 2);

    EXPECT_LE(-st[0], 0.55);
    EXPECT_GE(-st[0], 0.50);

    fileName = std::string(TADJ_TEST_DIRECTORY "adj_test8.xml");

    gds2 = readSimXMLFile(fileName);
    gds2->powerflow();
    requireStates(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds2->getLinkReactivePower(st, 0, 2);

    EXPECT_LE(-st[0], 1.1);
    EXPECT_GE(-st[0], 1.0);
}

TEST_F(AdjustableTransformerTests, AdjTestContMvar)
{
    std::string fileName = std::string(TADJ_TEST_DIRECTORY "adj_test7c.xml");

    gds = readSimXMLFile(fileName);
    gds->pFlowInitialize();
    int mmatch = runJacobianCheck(gds, cPflowSolverMode);

    ASSERT_EQ(mmatch, 0);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> st;
    gds->getLinkReactivePower(st, 0, 2);

    EXPECT_NEAR(st[0], -0.50, std::abs(-0.50) * 1e-4 + 1e-12);

    fileName = std::string(TADJ_TEST_DIRECTORY "adj_test8c.xml");

    gds2 = readSimXMLFile(fileName);
    gds2->powerflow();
    requireStates(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds2->getLinkReactivePower(st, 0, 2);

    EXPECT_NEAR(st[0], -1.05, std::abs(-1.05) * 1e-4 + 1e-12);
}

// now test the continuous Voltage control
TEST_F(AdjustableTransformerTests, AdjTestContV)
{
    std::string fileName = std::string(TADJ_TEST_DIRECTORY "adj_test9.xml");

    gds = readSimXMLFile(fileName);
    gds->pFlowInitialize();
    int mmatch = runJacobianCheck(gds, cPflowSolverMode);

    ASSERT_EQ(mmatch, 0);
    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    std::vector<double> st;
    gds->getVoltage(st);

    EXPECT_NEAR(st[2], 1.0, 1e-7);
    // test multiple continuous controllers
    fileName = std::string(TADJ_TEST_DIRECTORY "adj_test10.xml");

    gds2 = readSimXMLFile(fileName);
    gds2->powerflow();
    requireStates(gds2->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    gds2->getVoltage(st);
    EXPECT_NEAR(st[1], 1.0, 1e-7);
    EXPECT_NEAR(st[2], 1.0, 1e-7);
}
