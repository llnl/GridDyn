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
#include <gtest/gtest.h>
// test case for coreObject object

#define GENMODEL_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/genmodel_tests/"

using namespace griddyn;

class GenModelTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(GenModelTests, ModelTest1)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    gds = readSimXMLFile(fileName);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    std::vector<double> st = gds->getState();
    runResidualCheck(gds, cDaeSolverMode);
    // gds->saveJacobian(std::string(GENMODEL_TEST_DIRECTORY "mjac5.bin"));
    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st2 = gds->getState();

    auto cdiff = gmlc::utilities::countDiffs(st, st2, 0.001, 0.01);

    EXPECT_EQ(cdiff, 0u);
}

TEST_F(GenModelTests, ModelTest2)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    auto cof = coreObjectFactory::instance();
    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        // skip any fmi model
        if (gname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);

        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);

        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr) << "Failed to create model " << gname;
        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Model " << gname << " dynInitialize issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " residual issue";
        mmatch = runJacobianCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian issue";
        mmatch = runDerivativeCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " derivative issue";
        mmatch = runAlgebraicCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " algebraic issue";
    }
}

TEST_F(GenModelTests, ModelTest2WithR)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    auto cof = coreObjectFactory::instance();
    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        if (gname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);

        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr) << "Failed to create model " << gname;
        // just set the resistance to make sure the models can handle that parameter
        obj->set("r", 0.001);
        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Model " << gname << " dynInitialize r issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " residual r issue";
        mmatch = runJacobianCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian r issue";
    }
}

#ifdef LOAD_CVODE
TEST_F(GenModelTests, ModelTest2AlgDiffTests)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    auto cof = coreObjectFactory::instance();

    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        if (gname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);

        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr) << "Failed to create model " << gname;
        // just set the resistance to make sure the models can handle that parameter
        obj->set("r", 0.001);
        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Model " << gname << " dynInitialize issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " residual issue";
        mmatch = runDerivativeCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " derivative issue";
        mmatch = runAlgebraicCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " algebraic issue";
        if (gds->diffSize(cDaeSolverMode) > 0) {
            mmatch = runJacobianCheck(gds, cDynDiffSolverMode, false);
            ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian dynDiff issue";
            mmatch = runJacobianCheck(gds, cDynAlgSolverMode, false);
            ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian dynAlg issue";
        }
    }
}
#endif

TEST_F(GenModelTests, ModelTest3)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model2.xml");

    gds = readSimXMLFile(fileName);

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
