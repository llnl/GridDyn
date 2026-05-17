/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/simulation/diagnostics.h"
#include "griddyn/simulation/gridDynSimulationFileOps.h"
#include "griddyn/solvers/solverInterface.h"
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class LargeValidationTests: public gridDynSimulationTestFixture, public ::testing::Test {};

constexpr std::string_view validationTestDirectory{GRIDDYN_TEST_DIRECTORY "/validation_tests/"};
using namespace griddyn;

#ifdef ENABLE_IN_DEVELOPMENT_CASES
#    ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(LargeValidationTests, TestPjmPflow)
{
    std::string fileName = std::string(OTHER_TEST_DIRECTORY "pf.output.raw");
    gds = std::make_unique<gridDynSimulation>();
    readerInfo readerInformation;
    addFlags(readerInformation, "ignore_step_up_transformers");
    loadFile(gds, fileName, &readerInformation);
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);
    std::vector<double> gv1;
    std::vector<double> gv2;
    gds->getVoltage(gv1);
    gds->pFlowInitialize();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::INITIALIZED);
    int mmatch = residualCheck(gds, cPflowSolverMode, 0.2, true);
    if (mmatch > 0) {
        std::println("Mmatch failures={}", mmatch);
    }

    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->getVoltage(gv2);
    ASSERT_EQ(gv1.size(), gv2.size());
    int diffc = 0;
    double bdiff = 0;
    count_t bdiffi = 0;
    for (size_t kk = 0; kk < gv1.size(); ++kk) {
        if (std::abs(gv1[kk] - gv2[kk]) > 0.0015) {
            ++diffc;
            if (std::abs(gv1[kk] - gv2[kk]) > bdiff) {
                bdiff = std::abs(gv1[kk] - gv2[kk]);
                bdiffi = static_cast<count_t>(kk);
            }
        }
    }
    EXPECT_EQ(diffc, 0);
    if (diffc > 0) {
        std::println("{} diffs, difference bus {} orig={:f}, result={:f}",
                     diffc,
                     static_cast<int>(bdiffi),
                     gv1[bdiffi],
                     gv2[bdiffi]);
    }
}
#    endif
#endif

TEST_F(LargeValidationTests, TestPgePflow)
{
    std::string fileName =
        std::string(R"(C:\Users\top1\Documents\PG&E Basecases (epc)\a16_2018LSP.epc)");
    gds = std::make_unique<gridDynSimulation>();
    readerInfo readerInformation;
    loadFile(gds, fileName, &readerInformation);
    requireState(gridDynSimulation::gridState_t::STARTUP);
    std::vector<double> gv1;
    std::vector<double> gv2;
    gds->getVoltage(gv1);
    gds->pFlowInitialize();
    requireState(gridDynSimulation::gridState_t::INITIALIZED);
    residualCheck(gds.get(), cPflowSolverMode, 0.2, true);

    gds->powerflow();
    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->getVoltage(gv2);
    ASSERT_EQ(gv1.size(), gv2.size());
    int diffc = 0;
    double bdiff = 0;
    count_t bdiffi = 0;
    for (size_t kk = 0; kk < gv1.size(); ++kk) {
        if (std::abs(gv1[kk] - gv2[kk]) > 0.0015) {
            ++diffc;
            if (std::abs(gv1[kk] - gv2[kk]) > bdiff) {
                bdiff = std::abs(gv1[kk] - gv2[kk]);
                bdiffi = static_cast<count_t>(kk);
            }
        }
    }
    EXPECT_EQ(diffc, 0);
    if (diffc > 0) {
        std::println("{} diffs, difference bus {} orig={:f}, result={:f}",
                     diffc,
                     static_cast<int>(bdiffi),
                     gv1[bdiffi],
                     gv2[bdiffi]);
    }
}
