/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <set>
#include <string>
#include <utility>

class SimulationTests: public gridDynSimulationTestFixture, public ::testing::Test {};

static const std::string validationTestDirectory(GRIDDYN_TEST_DIRECTORY "/validation_tests/");

TEST_F(SimulationTests, SimulationOrderingTests) {}
