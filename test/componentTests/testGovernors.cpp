/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/objectFactory.hpp"
#include "griddyn/Generator.h"
#include "griddyn/simulation/diagnostics.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <vector>
// test case for coreObject object

#define GOVERNOR_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/governor_tests/"

using namespace griddyn;

class GovernorTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(GovernorTests, GovStabilityTest)
{
    std::string fileName = std::string(GOVERNOR_TEST_DIRECTORY "test_gov_stability.xml");

    gds->resetObjectCounters();
    gds = readSimXMLFile(fileName);
    Generator* gen = static_cast<Generator*>(gds->findByUserID("gen", 2));
    ASSERT_NE(gen, nullptr);

    auto cof = coreObjectFactory::instance();
    coreObject* obj = cof->createObject("governor", "basic");
    ASSERT_NE(obj, nullptr);

    gen->add(obj);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);
    gds->run(0.005);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);

    gds->run(400.0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    std::vector<double> st = gds->getState();
    gds->run(500.0);
    gds->saveRecorders();
    std::vector<double> st2 = gds->getState();

    // check for stability
    ASSERT_EQ(st.size(), st2.size());
    int ncnt = 0;
    double a0 = st2[0];
    for (size_t kk = 0; kk < st.size(); ++kk) {
        if (std::abs(st[kk] - st2[kk]) > 0.0001) {
            if (std::abs(st[kk] - st2[kk] + a0) > 0.005 * ((std::max)(st[kk], st2[kk]))) {
                printf("state[%zd] orig=%f new=%f\n", kk, st[kk], st2[kk]);
                ncnt++;
            }
        }
    }
    EXPECT_EQ(ncnt, 0);
}
