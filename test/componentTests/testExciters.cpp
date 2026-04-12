/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/objectFactory.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Generator.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
// test case for coreObject object

#define EXCITER_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/exciter_tests/"

using namespace griddyn;

class ExciterTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(ExciterTests, RootExciterTest)
{
    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_root_exciter.xml");

    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    std::vector<double> st = gds->getState();

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds->getState();

    // check for stability
    auto diff = gmlc::utilities::countDiffs(st, st2, 0.0001);
    EXPECT_EQ(diff, 0u);
}

TEST_F(ExciterTests, BasicStabilityTest1)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");

    auto cof = coreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;

        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);

        int retval = gds->dynInitialize();
        requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";

        int badresid = runResidualCheck(gds, cDaeSolverMode, false);

        ASSERT_EQ(badresid, 0) << "exciter type " << excname << " resid issue";
        int badjacobian = runJacobianCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(badjacobian, 0) << "exciter type " << excname << " Jacobian issue";

        std::vector<double> volt1;
        gds->getVoltage(volt1);

        gds->run();

        ASSERT_GE(gds->getSimulationTime(), 30.0)
            << "exciter type " << excname << " didn't complete";
        std::vector<double> volt2;
        gds->getVoltage(volt2);
        EXPECT_TRUE((volt2[0] > 0.95) && (volt2[0] < 1.00));
        EXPECT_TRUE((volt2[1] > 0.95) && (volt2[1] < 1.000));

        // check for stability
    }
}

TEST_F(ExciterTests, BasicStabilityTest2)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability2.xml");

    auto cof = coreObjectFactory::instance();
    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);

        int retval = gds->dynInitialize();
        ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";

        int badresid = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(badresid, 0) << "Exciter " << excname << " residual issue";
        int badjacobian = runJacobianCheck(gds, cDaeSolverMode, false);

        ASSERT_EQ(badjacobian, 0) << "Exciter " << excname << " Jacobian issue";

        std::vector<double> volt1;
        gds->getVoltage(volt1);

        gds->run();
        if (gds->getSimulationTime() < 30.0) {
            printf("exciter didn't complete %s\n", excname.c_str());
            gds->saveRecorders();
        }
        ASSERT_GE(gds->getSimulationTime(), 30.0) << "Exciter " << excname << " didn't complete";
        std::vector<double> volt2;
        gds->getVoltage(volt2);
        EXPECT_TRUE((volt2[0] > 1.00) && (volt2[0] < 1.05));
        EXPECT_TRUE((volt2[1] > 0.99) && (volt2[1] < 1.04));

        // check for stability
    }
}

TEST_F(ExciterTests, BasicStabilityTest3)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        //{ "basic",{ { "ta",0.2 },{ "ka",11.0 } } },
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.3}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability3.xml");

    auto cof = coreObjectFactory::instance();
    coreObject* obj = nullptr;

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        if (excname == "dc1a") {
            // TODO(phlpt): Figure out why this still fails.
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);

        int retval = gds->dynInitialize();
        ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";

        int badresid = runResidualCheck(gds, cDaeSolverMode, false);

        ASSERT_EQ(badresid, 0) << "Exciter " << excname << " residual issue";
        int badjacobian = runJacobianCheck(gds, cDaeSolverMode, false);

        ASSERT_EQ(badjacobian, 0) << "Exciter " << excname << " Jacobian issue";

        std::vector<double> volt1;
        gds->getVoltage(volt1);

        gds->run();
        if (gds->getSimulationTime() < 30.0) {
            printf("exciter didn't complete %s\n", excname.c_str());
            gds->saveRecorders();
        }
        ASSERT_GE(gds->getSimulationTime(), 30.0) << "Exciter " << excname << " didn't complete";
        std::vector<double> volt2;
        gds->getVoltage(volt2);
        EXPECT_TRUE((volt2[0] > 0.98) && (volt2[0] < 1.02));
        EXPECT_TRUE((volt2[1] > 0.97) && (volt2[1] < 1.02));
        // check for stability
    }
}

TEST_F(ExciterTests, BasicStabilityTest4)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        //{ "basic",{ { "ta",0.2 },{ "ka",11.0 } } },
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.3}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability4.xml");

    auto cof = coreObjectFactory::instance();
    coreObject* obj = nullptr;

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        if (excname == "dc1a") {
            // TODO(phlpt): Figure out why this still fails.
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);

        int retval = gds->dynInitialize();
        ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";

        int badresid = runResidualCheck(gds, cDaeSolverMode, false);

        ASSERT_EQ(badresid, 0) << "Exciter " << excname << " residual issue";
        int badjacobian = runJacobianCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(badjacobian, 0) << "Exciter " << excname << " Jacobian issue";

        std::vector<double> volt1;
        gds->getVoltage(volt1);

        gds->run();
        if (gds->getSimulationTime() < 30.0) {
            printf("exciter didn't complete %s\n", excname.c_str());
            gds->saveRecorders();
        }
        ASSERT_GE(gds->getSimulationTime(), 30.0) << "Exciter " << excname << " didn't complete";
        std::vector<double> volt2;
        gds->getVoltage(volt2);
        EXPECT_TRUE((volt2[0] > 0.98) && (volt2[0] < 1.02));
        EXPECT_TRUE((volt2[1] > 0.97) && (volt2[1] < 1.02));
        // check for stability
    }
}

#ifdef LOAD_CVODE
TEST_F(ExciterTests, ExciterTest2AlgDiffTests)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");

    auto cof = coreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " residual issue";
        mmatch = runDerivativeCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " derivative issue";
        mmatch = runAlgebraicCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " algebraic issue";
    }
}

TEST_F(ExciterTests, ExciterAlgDiffJacobianTests)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");

    auto cof = coreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);
        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";
        auto mmatch = runJacobianCheck(gds, cDynDiffSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " Jacobian dynDiff issue";
        mmatch = runJacobianCheck(gds, cDynAlgSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " Jacobian dynAlg issue";
    }
}
#endif
