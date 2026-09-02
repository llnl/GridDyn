/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "utilities/Saturation.h"
#include <cmath>
#include <gtest/gtest.h>
#include <utility>

using utilities::Saturation;

TEST(SaturationTests, CutoffScaledQuadraticReferencePoints)
{
    Saturation saturation(Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    saturation.setParam(0.1, 0.3);

    const double ratio = std::sqrt(0.1 / (1.2 * 0.3));
    const double saturationStart = (1.0 - (1.2 * ratio)) / (1.0 - ratio);

    EXPECT_DOUBLE_EQ(saturation.compute(0.5 * saturationStart), 0.0);
    EXPECT_DOUBLE_EQ(saturation.deriv(0.5 * saturationStart), 0.0);
    EXPECT_NEAR(saturation.compute(1.0), 0.1, 1e-14);
    EXPECT_NEAR(saturation.compute(1.2), 0.3, 1e-14);

    const auto evaluation = saturation.evaluate(1.1);
    const double step = 1e-6;
    const double finiteDifference =
        (saturation.compute(1.1 + step) - saturation.compute(1.1 - step)) / (2.0 * step);
    EXPECT_NEAR(evaluation.value, saturation.compute(1.1), 1e-15);
    EXPECT_NEAR(evaluation.derivative, finiteDifference, 1e-9);
}

TEST(SaturationTests, CutoffQuadraticMatchesAndesExciterSaturation)
{
    Saturation saturation(Saturation::SaturationType::CUTOFF_QUADRATIC);
    // These are E * SE values from the two PSS/E/ANDES saturation records.
    saturation.setParam(3.0, 0.3, 4.0, 0.8);

    const double ratio = std::sqrt(0.3 / 0.8);
    const double saturationStart = (3.0 - 4.0 * ratio) / (1.0 - ratio);
    EXPECT_DOUBLE_EQ(saturation.compute(0.5 * saturationStart), 0.0);
    EXPECT_DOUBLE_EQ(saturation.deriv(0.5 * saturationStart), 0.0);
    EXPECT_NEAR(saturation.compute(3.0), 0.3, 1e-14);
    EXPECT_NEAR(saturation.compute(4.0), 0.8, 1e-14);

    // ANDES also permits its common zero-first-point convention.
    saturation.setParam(0.0, 0.0, 2.0, 0.4);
    EXPECT_DOUBLE_EQ(saturation.compute(-0.1), 0.0);
    EXPECT_NEAR(saturation.compute(2.0), 0.4, 1e-14);
}

TEST(SaturationTests, DisabledCharacteristicIsFinite)
{
    Saturation saturation(Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);

    saturation.setParam(0.0, 1.0);
    EXPECT_DOUBLE_EQ(saturation.compute(0.0), 0.0);
    EXPECT_DOUBLE_EQ(saturation.compute(1.0), 0.0);
    EXPECT_DOUBLE_EQ(saturation.deriv(1.0), 0.0);
    EXPECT_TRUE(std::isfinite(saturation.compute(1.0)));

    saturation.setParam(0.1, 0.0);
    EXPECT_DOUBLE_EQ(saturation.compute(1.2), 0.0);
    EXPECT_DOUBLE_EQ(saturation.deriv(1.2), 0.0);
}

TEST(SaturationTests, CopiesOwnTheirEvaluationFunctions)
{
    Saturation original(Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    original.setParam(0.1, 0.3);
    Saturation copied(original);
    Saturation assigned;
    assigned = original;
    Saturation moved(std::move(copied));

    original.setParam(0.2, 0.5);
    EXPECT_NEAR(moved.compute(1.0), 0.1, 1e-14);
    EXPECT_NEAR(assigned.compute(1.2), 0.3, 1e-14);
    EXPECT_NE(moved.compute(1.0), original.compute(1.0));
}

TEST(SaturationTests, StringTypeAndInverse)
{
    Saturation saturation("cutoff_scaled_quadratic");
    saturation.setParam(0.1, 0.3);

    EXPECT_EQ(saturation.getType(), Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    EXPECT_NEAR(saturation.inv(saturation.compute(1.1)), 1.1, 1e-12);
}
