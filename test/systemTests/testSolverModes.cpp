/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/simulation/Diagnostics.h"
#include "griddyn/solvers/BasicOdeSolver.h"
#include "griddyn/solvers/BasicSolver.h"
#include "griddyn/solvers/SolverInterface.h"
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <string>

using namespace griddyn;

class SolverModeTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(SolverModeTests, PackedFlagDescriptionsExposeCanonicalNamesAndAliases)
{
    const auto fields = SolverInterface::flagDescriptions();
    const auto dense = std::ranges::find(fields, "dense", &SolverInterface::FlagDescription::name);
    ASSERT_NE(dense, fields.end());
    EXPECT_EQ(dense->inverseName, "sparse");
    EXPECT_FALSE(dense->description.empty());
    EXPECT_EQ(std::ranges::find(fields, "sparse", &SolverInterface::FlagDescription::name),
              fields.end());

    SolverInterface solver;
    solver.set("flags", "sparse,solver_log");
    EXPECT_TRUE(solver.getFlag("sparse"));
    EXPECT_FALSE(solver.getFlag("dense"));
    EXPECT_TRUE(solver.getFlag("directlogging"));
    solver.setFlag("sparse", false);
    EXPECT_TRUE(solver.getFlag("dense"));
    solver.setFlag("directlogging", false);
    EXPECT_FALSE(solver.getFlag("solver_log"));
    EXPECT_FALSE(solver.getFlag(""));
}

TEST_F(SolverModeTests, SolverDescriptionsDriveInheritedAccessAndCustomFallback)
{
    solvers::BasicOdeSolver euler;
    SolverInterface& solver = euler;
    const auto& description = solver.getSolverDescription();
    ASSERT_EQ(description.numeric.size(), 1U);
    EXPECT_EQ(description.numeric.front().name, "deltat");
    EXPECT_EQ(description.numeric.front().unitType, units::second);
    ASSERT_NE(description.parent, nullptr);
    EXPECT_FALSE(description.parent->flags.empty());

    solver.set("step", 0.02);
    EXPECT_DOUBLE_EQ(solver.get("deltat"), 0.02);
    EXPECT_DOUBLE_EQ(solver.get("steptime"), 0.02);
    solver.set("tolerance", 1e-6);
    EXPECT_DOUBLE_EQ(solver.get("tolerance"), 1e-6);
    solver.set("logfile", "solver.log");
    EXPECT_EQ(solver.getString("logfile"), "solver.log");
    EXPECT_THROW(solver.set("file", "other.log"), UnrecognizedParameter);
    for (const auto* key : {"", "# comment"}) {
        EXPECT_NO_THROW(solver.set(key, "ignored"));
        EXPECT_NO_THROW(solver.set(key, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_NO_THROW(solver.setFlag(key, true));
    }

    solvers::BasicSolver basic;
    basic.set("alpha", 0.9);
    EXPECT_DOUBLE_EQ(basic.get("alpha"), 0.9);
    EXPECT_DOUBLE_EQ(basic.get("iterations"), 0.0);
    basic.set("algorithm", "gauss-seidel");
    EXPECT_TRUE(basic.getSolverMode().approx[FORCE_RECALC]);

    auto ida = makeSolver("ida");
    ASSERT_NE(ida, nullptr);
    EXPECT_DOUBLE_EQ(ida->get("perftotal"), 0.0);
    EXPECT_NO_THROW(ida->set("maxiterations", 42.0));
    EXPECT_DOUBLE_EQ(ida->get("maxiterations"), 42.0);
}
