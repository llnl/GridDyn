/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for element readers

#include "../gtestHelper.h"
#include "runner/gridDynRunner.h"
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

TEST(RunnerTests, RunnerTest1) {}

TEST(RunnerTests, UnresolvedCommandLineEventWarnsAndIsIgnored)
{
    auto simulation = std::make_shared<griddyn::GridDynSimulation>();
    simulation->consolePrintLevel = griddyn::PrintLevel::WARNING;
    std::vector<std::string> messages;
    simulation->setLogger(
        [&messages](int, const std::string& message) { messages.push_back(message); });

    griddyn::GriddynRunner runner(simulation);
    std::string fileName = std::string(GRIDDYN_TEST_DIRECTORY "/runnerTests/test_180_trip.xml");
    std::string event = "@1|BUS$999999::LOAD#0:p(MW)=1";
    char executable[] = "griddyn";
    char eventOption[] = "--event";
    char* argv[] = {executable, fileName.data(), eventOption, event.data()};

    EXPECT_EQ(runner.Initialize(4, argv), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(
        std::any_of(messages.cbegin(), messages.cend(), [&event](const std::string& message) {
            return message.find("command-line event target was not resolved") !=
                std::string::npos &&
                message.find(event) != std::string::npos;
        }));
}
