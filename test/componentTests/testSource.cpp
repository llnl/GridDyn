/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/readElement.h"
#include "griddyn/sources/sourceTypes.h"
#include <cstdio>

#include <gtest/gtest.h>

#define SOURCE_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/source_tests/"

class SourceTests: public gridDynSimulationTestFixture, public ::testing::Test {
};

using namespace griddyn;
using namespace griddyn::sources;

TEST_F(SourceTests, SourceTest1)
{
    Source* src1 = new Source("gs", 45.0);

    EXPECT_NEAR(src1->getOutput(), 45.0, 0.001);
    src1->set("value", 22.0);
    EXPECT_NEAR(src1->getOutput(), 22.0, 0.001);
    delete src1;

    src1 = new rampSource("rs", 1.0);

    EXPECT_NEAR(src1->getOutput(), 1.0, 0.001);
    src1->set("rate", 0.5);
    src1->dynInitializeA(0.0, 0);
    src1->timestep(3.0, noInputs, cLocalSolverMode);
    double rval = src1->getOutput();
    EXPECT_NEAR(rval, 2.5, 0.001);
    src1->set("rate", -0.75);
    src1->timestep(5.0, noInputs, cLocalSolverMode);
    rval = src1->getOutput();
    EXPECT_NEAR(rval, 1.0, 0.001);
    delete src1;

    src1 = new pulseSource("ps", 1.0);

    EXPECT_NEAR(src1->getOutput(), 1.0, 0.001);
    src1->set("type", "square");
    src1->set("period", 2);
    src1->set("a", 1.0);
    src1->dynInitializeA(0.0, 0);
    src1->timestep(2.5, noInputs, cLocalSolverMode);
    rval = src1->getOutput();
    EXPECT_NEAR(rval, 2.0, 0.001);
    src1->set("period", 1);
    src1->timestep(2.75, noInputs, cLocalSolverMode);
    rval = src1->getOutput();
    EXPECT_NEAR(rval, 1.0, 0.001);
    delete src1;
}

TEST_F(SourceTests, SineSourceTest)
{
    auto src1 = new sineSource("ss", 0.0);
    src1->set("freq", 1.0);
    src1->set("amp", 1.0);
    auto src2 = new sineSource("ss", 0.0);
    src2->set("freq", 1.0);
    src2->set("amp", 1.0);
    src1->pFlowInitializeA(0.0, 0);
    src2->pFlowInitializeA(0.0, 0);
    src1->dynInitializeA(0.0, 0);
    src2->dynInitializeA(0.0, 0);
    for (double t = 0.2; t < 4.9; t += 0.2) {
        src1->updateOutput(t);
        EXPECT_NEAR(std::abs(src1->getOutput() - src2->computeOutput(t)), 0.0, 1e-7);
        EXPECT_NEAR(std::abs(src1->getOutput() - sin(2.0 * griddyn::kPI * t)), 0.0, 1e-7);
    }
    delete src1;
    delete src2;
}
