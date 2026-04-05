/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/simulation/diagnostics.h"
#include "griddyn/solvers/solverInterface.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>

using namespace griddyn;
static std::string solverMode_test_directory =
    std::string(GRIDDYN_TEST_DIRECTORY "/solvermode_tests/");

class SolverModeTests: public gridDynSimulationTestFixture, public ::testing::Test {};
