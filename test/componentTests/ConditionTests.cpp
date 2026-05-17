/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "griddyn/gridBus.h"
#include "griddyn/links/acLine.h"
#include "griddyn/measurement/Condition.h"
#include <gtest/gtest.h>
// test case for CoreObject object

using namespace griddyn;

/** test basic operations */
TEST(ConditionTests, BasicTests)
{
    gridBus B;
    B.setVoltageAngle(1.0, 0.05);

    auto cond = make_condition("voltage", "<", 0.7, &B);
    ASSERT_NE(cond, nullptr);

    EXPECT_NEAR(cond->evalCondition(), 0.3, std::abs(0.3) * 1e-6 + 1e-12);

    EXPECT_FALSE(cond->checkCondition());
    EXPECT_NEAR(cond->getVal(1), 1.0, 1e-7);

    EXPECT_NEAR(cond->getVal(2), 0.7, 1e-7);
}

/** test basic operations */
TEST(ConditionTests, BasicTest2)
{
    gridBus B;
    B.setVoltageAngle(1.0, 0.05);

    auto cond = make_condition("voltage-0.4", "<", 0.7, &B);
    ASSERT_NE(cond, nullptr);

    EXPECT_NEAR(cond->evalCondition(), -0.1, std::abs(-0.1) * 1e-6 + 1e-12);

    EXPECT_TRUE(cond->checkCondition());
    EXPECT_NEAR(cond->getVal(1), 0.6, 1e-7);

    EXPECT_NEAR(cond->getVal(2), 0.7, 1e-7);
}

/** test basic operations */
TEST(ConditionTests, LinkTests)
{
    gridBus B1;
    B1.setVoltageAngle(1.0, 0.05);
    gridBus B2;
    B2.setVoltageAngle(1.05, -0.05);
    acLine L2;
    L2.set("x", 0.01);
    L2.set("r", 0.001);

    L2.updateBus(&B1, 1);
    L2.updateBus(&B2, 2);
    L2.updateLocalCache();

    auto cond = make_condition("current1>current2", &L2);
    ASSERT_NE(cond, nullptr);

    auto C1 = L2.getCurrent(1);
    auto C2 = L2.getCurrent(2);
    EXPECT_NEAR(cond->evalCondition() - (C1 - C2), 0.0, 1e-4);

    EXPECT_EQ(cond->checkCondition(), C1 > C2);
    EXPECT_NEAR(cond->getVal(1), C1, std::abs(C1) * 1e-6 + 1e-12);
    EXPECT_NEAR(cond->getVal(2), C2, std::abs(C2) * 1e-6 + 1e-12);
}

/** test basic operations */
TEST(ConditionTests, LinkTestsQueries)
{
    gridBus B1;
    B1.setVoltageAngle(1.0, 0.05);
    gridBus B2;
    B2.setVoltageAngle(1.05, -0.05);
    acLine L2;
    L2.set("x", 0.01);
    L2.set("r", 0.001);
    L2.set("g", 0.05);

    L2.updateBus(&B1, 1);
    L2.updateBus(&B2, 2);
    L2.updateLocalCache();

    auto cond = make_condition("current1-current2", ">", 0.01, &L2);
    ASSERT_NE(cond, nullptr);

    auto C1 = L2.getCurrent(1);
    auto C2 = L2.getCurrent(2);

    EXPECT_NEAR(cond->getVal(1), C1 - C2, std::abs(C1 - C2) * 1e-6 + 1e-12);
    EXPECT_NEAR(cond->getVal(2), 0.01, 1e-7);
}

/** test basic operations */
TEST(ConditionTests, LinkTestsQueries2)
{
    gridBus B1;
    B1.setVoltageAngle(1.0, 0.05);
    gridBus B2;
    B2.setVoltageAngle(1.05, -0.05);
    acLine L2;
    L2.set("x", 0.01);
    L2.set("r", 0.001);
    L2.set("g", 0.05);

    L2.updateBus(&B1, 1);
    L2.updateBus(&B2, 2);
    L2.updateLocalCache();

    auto cond = make_condition("(current1-current2)*(current1-current2)", ">", 0.01, &L2);
    ASSERT_NE(cond, nullptr);

    auto C1 = L2.getCurrent(1);
    auto C2 = L2.getCurrent(2);

    EXPECT_NEAR(cond->getVal(1),
                (C1 - C2) * (C1 - C2),
                std::abs((C1 - C2) * (C1 - C2)) * 1e-6 + 1e-12);
    EXPECT_NEAR(cond->getVal(2), 0.01, 1e-7);
}

/** test basic operations */
TEST(ConditionTests, LinkTestsQueries3)
{
    gridBus B1;
    B1.setVoltageAngle(1.0, 0.05);
    gridBus B2;
    B2.setVoltageAngle(1.05, -0.05);
    acLine L2;
    L2.set("x", 0.01);
    L2.set("r", 0.001);
    L2.set("g", 0.05);

    L2.updateBus(&B1, 1);
    L2.updateBus(&B2, 2);
    L2.updateLocalCache();

    auto cond =
        make_condition("hypot(abs(realcurrent1-realcurrent2),abs(imagcurrent1-imagcurrent2))",
                       ">",
                       0.01,
                       &L2);
    ASSERT_NE(cond, nullptr);

    auto R1 = L2.getRealCurrent(1);
    auto R2 = L2.getRealCurrent(2);
    auto I1 = L2.getImagCurrent(1);
    auto I2 = L2.getImagCurrent(2);

    EXPECT_NEAR(cond->getVal(1),
                std::hypot(std::abs(R1 - R2), std::abs(I1 - I2)),
                std::abs(std::hypot(std::abs(R1 - R2), std::abs(I1 - I2))) * 1e-6 + 1e-12);
    EXPECT_NEAR(cond->getVal(2), 0.01, 1e-7);
}
