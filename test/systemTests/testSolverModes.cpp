/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/simulation/Diagnostics.h"
#include "griddyn/solvers/SolverInterface.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <string>

using namespace griddyn;

class SolverModeTests: public GridDynSimulationTestFixture, public ::testing::Test {};
