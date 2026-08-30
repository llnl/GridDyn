/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/readerHelper.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "units/units.hpp"
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

namespace griddyn {
namespace {
    constexpr std::string_view xmlTestDirectory{GRIDDYN_TEST_DIRECTORY "/xml_tests/"};
    constexpr std::string_view inputTestDirectory{GRIDDYN_TEST_DIRECTORY "/input_tests/"};

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

    TEST(MatlabArrayReaderTests, AcceptsNewlineRowsWithoutChangingSemicolonRows)
    {
        mArray semicolonRows;
        readMatlabArray("matrix = [1 2; 3 4];", 0, semicolonRows);
        ASSERT_EQ(semicolonRows.size(), 2U);
        EXPECT_EQ(semicolonRows[0], (std::vector<double>{1.0, 2.0}));
        EXPECT_EQ(semicolonRows[1], (std::vector<double>{3.0, 4.0}));

        mArray newlineRows;
        readMatlabArray("matrix = [1 2\r\n3 4\n];", 0, newlineRows);
        ASSERT_EQ(newlineRows.size(), 2U);
        EXPECT_EQ(newlineRows[0], (std::vector<double>{1.0, 2.0}));
        EXPECT_EQ(newlineRows[1], (std::vector<double>{3.0, 4.0}));

        mArray continuationRows;
        readMatlabArray("matrix = [1 2 ...\n3 4\n5 6];", 0, continuationRows);
        ASSERT_EQ(continuationRows.size(), 2U);
        EXPECT_EQ(continuationRows[0], (std::vector<double>{1.0, 2.0, 3.0, 4.0}));
        EXPECT_EQ(continuationRows[1], (std::vector<double>{5.0, 6.0}));
    }

    TEST_F(SolverReaderTests, RawNegativePhaseControlRetainsFixedTap)
    {
        const std::string fileName =
            std::string{inputTestDirectory} + "raw_negative_phase_control.raw";
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), fileName);

        auto* transformer =
            dynamic_cast<links::AdjustableTransformer*>(gds->find("padjtx_FROM BUS_to_TO BUS"));
        ASSERT_NE(transformer, nullptr);
        EXPECT_NEAR(transformer->getTap(), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->getTapAngle(),
                    units::convert(-26.0, units::deg, units::rad),
                    1.0e-12);

        ASSERT_EQ(gds->pFlowInitialize(), 0);
        EXPECT_NEAR(transformer->getTap(), 1.0, 1.0e-12);
        EXPECT_NEAR(transformer->getTapAngle(),
                    units::convert(-26.0, units::deg, units::rad),
                    1.0e-12);
    }

    TEST_F(SolverReaderTests, RawThreeWindingTransformerCreatesStarEquivalent)
    {
        const std::string fileName =
            std::string{inputTestDirectory} + "raw_three_winding_transformer.raw";
        gds = std::make_unique<GridDynSimulation>();
        loadFile(gds.get(), fileName);

        ASSERT_EQ(gds->getInt("totalbuscount"), 4);
        ASSERT_EQ(gds->getInt("totallinkcount"), 3);
        const std::array<std::array<double, 4>, 3> expected{{
            {0.020, 0.200, 0.98, 2.0},
            {0.010, 0.100, 1.02, -1.0},
            {0.030, 0.300, 1.00, 0.0},
        }};
        for (index_t ii = 0; ii < expected.size(); ++ii) {
            const auto* leg = dynamic_cast<const AcLine*>(gds->getLink(ii));
            ASSERT_NE(leg, nullptr);
            EXPECT_NEAR(leg->get("r"), expected[ii][0], 1.0e-12);
            EXPECT_NEAR(leg->get("x"), expected[ii][1], 1.0e-12);
            EXPECT_NEAR(leg->get("tap"), expected[ii][2], 1.0e-12);
            EXPECT_NEAR(leg->get("tapangle"),
                        units::convert(expected[ii][3], units::deg, units::rad),
                        1.0e-12);
            ASSERT_NE(leg->getBus(2), nullptr);
            EXPECT_EQ(leg->getBus(2), gds->getBus(3));
        }
    }
}  // namespace
}  // namespace griddyn
