/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactory.hpp"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Relay.h"
#include "griddyn/blocks/blockLibrary.h"
#include "griddyn/simulation/Diagnostics.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define BLOCK_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/block_tests/"

static const char block_test_directory[] = GRIDDYN_TEST_DIRECTORY "/block_tests/";
using namespace griddyn;
using namespace griddyn::blocks;
using namespace gmlc::utilities;

class BlockTests: public GridDynSimulationTestFixture, public ::testing::Test {};

class BlockCompareTests:
    public GridDynSimulationTestFixture,
    public ::testing::TestWithParam<int> {};

TEST_F(BlockTests, TestGainBlock)
{
    std::string fileName = std::string(block_test_directory) + "block_tests1.xml";

    gds = readSimXMLFile(fileName);

    dynamicInitializationCheck(fileName);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);
    ASSERT_GE(ts3.size(), 15U);
    EXPECT_NEAR(ts3.data(0, 5) * 5, ts3.data(1, 5), (std::abs(ts3.data(1, 5)) * 1e-8) + 1e-12);
    EXPECT_NEAR(ts3.data(0, 15) * 5, ts3.data(1, 15), (std::abs(ts3.data(1, 15)) * 1e-8) + 1e-12);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(BlockTests, BlockTest2)
{
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_tests2.xml");

    dynamicInitializationCheck(fileName);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3(recname);

    EXPECT_NEAR(ts3.data(0, 5) * 5, ts3.data(1, 5), (std::abs(ts3.data(1, 5)) * 1e-5) + 1e-12);
    EXPECT_NEAR(ts3.data(0, 280) * 5,
                ts3.data(1, 280),
                (std::abs(ts3.data(1, 280)) * 1e-5) + 1e-12);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(BlockTests, BlockTest3)
{
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_tests3.xml");

    dynamicInitializationCheck(fileName);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3(recname);
    // ts3.loadBinaryFile(recname);

    EXPECT_NEAR(ts3.data(1, 5), 0.0, 1e-5);
    EXPECT_NEAR(ts3.data(1, 280), 0.0, 1e-3);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(BlockTests, BlockTest4)
{
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_tests4.xml");

    dynamicInitializationCheck(fileName);

    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);

    EXPECT_NEAR(ts3.data(1, 5), -0.5, (std::abs(-0.5) * 1e-4) + 1e-12);
    double initialValue = -0.5;
    index_t pointIndex = 0;
    for (pointIndex = 0; std::cmp_less(pointIndex, ts3.size()); ++pointIndex) {
        initialValue += 100 * ts3.data(0, pointIndex) * 0.01;
    }
    EXPECT_NEAR(ts3.data(1, pointIndex - 1), initialValue, (std::abs(initialValue) * 1e-2) + 1e-12);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(BlockTests, BlockTest5)
{
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_tests5.xml");

    dynamicInitializationCheck(fileName);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);

    EXPECT_NEAR(ts3.data(1, 5), 0.0, 1e-4);
    auto maxInputMagnitude = absMax(ts3[0]);
    auto maxOutputMagnitude = absMax(ts3[1]);
    EXPECT_NEAR(maxOutputMagnitude,
                maxInputMagnitude * 5,
                (std::abs(maxInputMagnitude * 5) * 1e-2) + 1e-12);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(BlockTests, BlockTest6)
{
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_tests6.xml");

    dynamicInitializationCheck(fileName);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3(recname);
    // ts3.loadBinaryFile(recname);

    EXPECT_NEAR(ts3.data(1, 5), 1.0, 1e-4);

    EXPECT_NEAR(ts3.data(1, 2000), 1.0, 1e-4);
    (void)remove(recname.c_str());

    recname = std::string(BLOCK_TEST_DIRECTORY "blocktest2.dat");
    ts3.loadBinaryFile(recname);

    EXPECT_NEAR(ts3.data(1, 5), 1.0, 1e-4);

    EXPECT_NEAR(ts3.data(1, 200), 1.0, 1e-4);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(BlockTests, DeadbandBlockTest)
{
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_tests_deadband.xml");

    gds = readSimXMLFile(fileName);
    gds->solverSet("powerflow", "printlevel", 0);
    gds->solverSet("dynamic", "printlevel", 0);
    int retval = gds->dynInitialize();

    EXPECT_EQ(retval, 0);

    int mmatch = runJacobianCheck(gds, cDaeSolverMode, 1e-5);

    ASSERT_EQ(mmatch, 0);
    mmatch = runResidualCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->run();
    ASSERT_GT(double(gds->getSimulationTime()), 7.9);

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);

    auto hasDeadbandViolation = std::any_of(ts3[1].begin(), ts3[1].end(), [](double value) {
        return ((value > 0.400001) && (value < 0.4999999));
    });
    EXPECT_FALSE(hasDeadbandViolation);

    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

using blockdescpair = std::pair<std::string, std::vector<std::pair<std::string, double>>>;

std::vector<blockdescpair> makeBlockParameterMap()
{
    return {
        {"basic", {std::make_pair("k", 2.3), std::make_pair("max", 0.1)}},
        {"delay", {std::make_pair("t1", 0.5)}},
        {"integral", {std::make_pair("iv", 0.14)}},
        {"derivative", {std::make_pair("t", 0.25)}},
        {"fder", {std::make_pair("t1", 0.25), std::make_pair("t2", 0.1)}},
        {"deadband", {std::make_pair("db", 0.1), std::make_pair("ramp", 0.03)}},
        {"db", {std::make_pair("db", 0.08)}},
        {"pid",
         {std::make_pair("p", 0.7),
          std::make_pair("i", 0.02),
          std::make_pair("d", 0.28),
          std::make_pair("t", 0.2)}},
        {"control", {std::make_pair("t1", 0.2), std::make_pair("t2", 0.1)}},
        {"function", {std::make_pair("gain", kPI), std::make_pair("bias", -0.05)}},
        {"func", {std::make_pair("arg", 2.35)}},
    };
}

std::map<std::string, std::vector<std::pair<std::string, std::string>>>
    makeStringBlockParameterMap()
{
    return {
        {"function", {std::make_pair("func", "sin")}},
        {"func", {std::make_pair("func", "pow")}},
        {"db", {std::make_pair("flags", "shifted")}},
    };
}

TEST_P(BlockCompareTests, CompareBlockTest)
{
    const auto caseIndex = GetParam();
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_test_compare.xml");
    auto blockFactory = CoreObjectFactory::instance()->getFactory("block");

    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    gds->solverSet("powerflow", "printlevel", 0);
    gds->solverSet("dynamic", "printlevel", 0);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->consolePrintLevel = PrintLevel::WARNING;
    auto rel1 = gds->getRelay(0);
    auto rel2 = gds->getRelay(1);
    const auto blockParameterMap = makeBlockParameterMap();
    const auto stringBlockParameterMap = makeStringBlockParameterMap();
    auto& plist = blockParameterMap[caseIndex];

    auto bb1 = static_cast<GridBlock*>(blockFactory->makeObject(plist.first));
    auto bb2 = static_cast<GridBlock*>(blockFactory->makeObject(plist.first));

    ASSERT_NE(bb1, nullptr);
    ASSERT_NE(bb2, nullptr);
    for (const auto& parameterValue : plist.second) {
        bb1->set(parameterValue.first, parameterValue.second);
        bb2->set(parameterValue.first, parameterValue.second);
    }

    auto kfind = stringBlockParameterMap.find(plist.first);
    if (kfind != stringBlockParameterMap.end()) {
        auto blockParameters = stringBlockParameterMap.at(plist.first);
        for (const auto& parameterValue : blockParameters) {
            bb1->set(parameterValue.first, parameterValue.second);

            bb2->set(parameterValue.first, parameterValue.second);
        }
    }
    rel1->add(bb1);
    rel2->add(bb2);
    int retval = gds->dynInitialize();

    EXPECT_EQ(retval, 0);

    int mmatch = runJacobianCheck(gds, cDaeSolverMode, 1e-5);
    if (mmatch > 0) {
        std::cout << " mismatching Jacobian in " << plist.first << '\n';
        ASSERT_EQ(mmatch, 0);
    }

    mmatch = runResidualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        std::cout << " mismatching residual in " << plist.first << '\n';
        ASSERT_EQ(mmatch, 0);
    }

    gds->run();
    if (gds->getSimulationTime() < 7.99) {
        runJacobianCheck(gds, cDaeSolverMode);
        runResidualCheck(gds, cDaeSolverMode);
        ASSERT_GT(gds->getSimulationTime(), 7.99)
            << "GridBlock " << plist.first << " failed to solve";
    }

    std::string recname = std::string(BLOCK_TEST_DIRECTORY "blocktest.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);
    std::vector<double> difference(ts3.size());
    compareVec(ts3[1], ts3[2], difference);
    auto maxDifference = absMax(difference);
    auto meanDifference = mean(difference);
    EXPECT_TRUE((maxDifference < 1e-2) || (meanDifference < 2e-3));
    int ret = remove(recname.c_str());

    if ((maxDifference > 1e-2) && (meanDifference > 2e-3)) {
        std::cout << " mismatching results in " << plist.first << '\n';
    }
    EXPECT_EQ(ret, 0);
}

INSTANTIATE_TEST_SUITE_P(AllBlocks, BlockCompareTests, ::testing::Range(0, 11));

#ifdef GRIDDYN_ENABLE_CVODE
/** test the control block if they can handle a differential only Jacobian and an algebraic only
 * Jacobian
 */
TEST_P(BlockCompareTests, BlockAlgDiffJacTest)
{
    const auto caseIndex = GetParam();
    std::string fileName = std::string(BLOCK_TEST_DIRECTORY "block_test_compare.xml");

    auto blockFactory = CoreObjectFactory::instance()->getFactory("block");
    const auto blockParameterMap = makeBlockParameterMap();
    const auto stringBlockParameterMap = makeStringBlockParameterMap();
    auto& plist = blockParameterMap[caseIndex];

    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    gds->solverSet("powerflow", "printlevel", 0);
    gds->solverSet("dynamic", "printlevel", 0);
    gds->set("recorddirectory", BLOCK_TEST_DIRECTORY);
    gds->consolePrintLevel = PrintLevel::WARNING;
    auto rel1 = gds->getRelay(0);
    auto rel2 = gds->getRelay(1);

    auto bb1 = static_cast<GridBlock*>(blockFactory->makeObject(plist.first));
    auto bb2 = static_cast<GridBlock*>(blockFactory->makeObject(plist.first));

    ASSERT_NE(bb1, nullptr);
    ASSERT_NE(bb2, nullptr);
    for (const auto& parameterValue : plist.second) {
        bb1->set(parameterValue.first, parameterValue.second);

        bb2->set(parameterValue.first, parameterValue.second);
    }

    auto kfind = stringBlockParameterMap.find(plist.first);
    if (kfind != stringBlockParameterMap.end()) {
        auto blockParameters = stringBlockParameterMap.at(plist.first);
        for (const auto& parameterValue : blockParameters) {
            bb1->set(parameterValue.first, parameterValue.second);

            bb2->set(parameterValue.first, parameterValue.second);
        }
    }
    rel1->add(bb1);
    rel2->add(bb2);
    int retval = gds->dynInitialize();

    EXPECT_EQ(retval, 0);
    auto mmatch = runResidualCheck(gds, cDaeSolverMode);
    ASSERT_EQ(mmatch, 0) << "GridBlock " << plist.first << " residual issue";
    mmatch = runDerivativeCheck(gds, cDaeSolverMode);
    ASSERT_EQ(mmatch, 0) << "GridBlock " << plist.first << " derivative issue";
    mmatch = runAlgebraicCheck(gds, cDaeSolverMode);
    ASSERT_EQ(mmatch, 0) << "GridBlock " << plist.first << " algebraic issue";
    mmatch = runJacobianCheck(gds, cDynDiffSolverMode);
    ASSERT_EQ(mmatch, 0) << "GridBlock " << plist.first << " Jacobian dynDiff issue";
    mmatch = runJacobianCheck(gds, cDynAlgSolverMode);
    ASSERT_EQ(mmatch, 0) << "GridBlock " << plist.first << " Jacobian dynAlg issue";
}

#endif
