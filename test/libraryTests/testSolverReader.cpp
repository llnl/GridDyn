/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace griddyn {
namespace {
    constexpr std::string_view xmlTestDirectory{GRIDDYN_TEST_DIRECTORY "/xml_tests/"};

    class SolverReaderTests: public GridDynSimulationTestFixture, public ::testing::Test {};

    TEST_F(SolverReaderTests, NamedSolverCanBeSelectedAsDefault)
    {
        const std::string fileName =
            std::string{xmlTestDirectory} + "test_named_default_solver.xml";
        gds = readSimXMLFile(fileName);

        ASSERT_NE(gds, nullptr);
        const auto namedSolver = gds->getSolverInterface("basic");
        ASSERT_NE(namedSolver, nullptr);
        EXPECT_EQ(gds->getSolverInterface("powerflow"), namedSolver);
        EXPECT_EQ(readerConfig::warnCount, 0);
    }
}  // namespace
}  // namespace griddyn
