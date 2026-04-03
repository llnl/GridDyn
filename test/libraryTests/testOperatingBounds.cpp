/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for operatingBoundary class

#include "../gtestHelper.h"
#include "utilities/OperatingBoundary.h"

#include <gtest/gtest.h>

using namespace utilities;
TEST(OperatingBoundaryTests, BasicLimits)
{
    OperatingBoundary Bound(-10, 10, 0, 5);

    auto lim = Bound.getLimits(0.0);
    EXPECT_NEAR(lim.first, 0.0, 1e-8);
    EXPECT_NEAR(lim.second, 5.0, 1e-8);

    EXPECT_NEAR(Bound.dMaxROC(0.0), 0.0, 1e-6);
    EXPECT_NEAR(Bound.dMinROC(0.0), 0.0, 1e-7);
}
