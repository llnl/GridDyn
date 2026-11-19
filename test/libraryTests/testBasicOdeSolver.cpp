/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/solvers/BasicOdeSolver.h"
#include "griddyn/solvers/SolverInterface.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <memory>

namespace griddyn {
namespace {
    using solvers::BasicOdeSolver;

    TEST(BasicOdeSolverTests, DefaultStepSizeIsPositive)
    {
        BasicOdeSolver solver;
        EXPECT_GT(solver.get("deltat"), 0.0);
        EXPECT_DOUBLE_EQ(solver.get("deltat"), 0.005);
    }

    TEST(BasicOdeSolverTests, StepSizeAliasesAreConsistent)
    {
        BasicOdeSolver solver;
        for (const auto* name : {"delta", "deltat", "step", "steptime"}) {
            solver.set(name, 0.01);
            EXPECT_DOUBLE_EQ(solver.get("deltat"), 0.01) << name;
        }
    }

    TEST(BasicOdeSolverTests, RejectsNonPositiveOrNonFiniteStepSize)
    {
        BasicOdeSolver solver;
        EXPECT_THROW(solver.set("deltat", 0.0), InvalidParameterValue);
        EXPECT_THROW(solver.set("deltat", -0.1), InvalidParameterValue);
        EXPECT_THROW(solver.set("deltat", std::numeric_limits<double>::quiet_NaN()),
                     InvalidParameterValue);
        EXPECT_THROW(solver.set("deltat", std::numeric_limits<double>::infinity()),
                     InvalidParameterValue);
        // a rejected value must not change the stored step size
        EXPECT_DOUBLE_EQ(solver.get("deltat"), 0.005);
    }

    TEST(BasicOdeSolverTests, CloneCopiesStepSize)
    {
        BasicOdeSolver solver;
        solver.set("deltat", 0.02);
        auto copy = solver.clone();
        EXPECT_DOUBLE_EQ(copy->get("deltat"), 0.02);
    }

    TEST(BasicOdeSolverTests, SolveToCurrentTimeIsNoOp)
    {
        BasicOdeSolver solver;
        solver.allocate(2);
        solver.initialize(1.5);
        CoreTime reached = 0.0;
        EXPECT_EQ(solver.solve(1.5, reached), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_DOUBLE_EQ(reached, 1.5);
        EXPECT_DOUBLE_EQ(solver.get("solvercount"), 1.0);
    }

    TEST(BasicOdeSolverTests, SolveBackwardsInTimeFails)
    {
        BasicOdeSolver solver;
        solver.allocate(2);
        solver.initialize(1.0);
        CoreTime reached = 0.0;
        EXPECT_EQ(solver.solve(0.5, reached), FUNCTION_EXECUTION_FAILURE);
        // time must not move
        EXPECT_DOUBLE_EQ(reached, 1.0);
    }

    TEST(BasicOdeSolverTests, TypeDataIsSizedToStateCount)
    {
        BasicOdeSolver solver;
        solver.allocate(3);
        ASSERT_NE(solver.typeData(), nullptr);
        for (int kk = 0; kk < 3; ++kk) {
            EXPECT_DOUBLE_EQ(solver.typeData()[kk], 1.0);
        }
    }
}  // namespace
}  // namespace griddyn
