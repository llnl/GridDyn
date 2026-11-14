/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/Link.h"
#include "griddyn/griddyn-config.h"
#include "griddyn/relays/Sensor.h"
#include "runner/gridDynRunner.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

TEST(RunnerTests, ReplaysRecordedTimeGrants)
{
    constexpr char fixtureDirectory[] = GRIDDYN_TEST_DIRECTORY "/runnerTests/";
    const std::string traceFile = std::string{fixtureDirectory} + "coupled-time-solvererror.txt";
    std::ifstream trace(traceFile);
    ASSERT_TRUE(trace.is_open()) << traceFile;

    auto simulation = std::make_shared<griddyn::GridDynSimulation>();
    simulation->consolePrintLevel = griddyn::PrintLevel::WARNING;
    griddyn::GriddynRunner runner(simulation);
    char executable[] = "griddyn";
    std::string caseFile = std::string{fixtureDirectory} + "test_180_trip.xml";
    char* argv[] = {executable, caseFile.data()};
    ASSERT_EQ(runner.Initialize(2, argv), FUNCTION_EXECUTION_SUCCESS);
    runner.simInitialize();
    ASSERT_FALSE(simulation->hasDynamics());
    ASSERT_EQ(simulation->getInt("totalbuscount"), 179);
    ASSERT_EQ(simulation->getInt("totallinkcount"), 263);
    for (const auto* name : {"sensor1", "sensor2"}) {
        SCOPED_TRACE(name);
        const auto* sensor = dynamic_cast<const griddyn::Sensor*>(simulation->find(name));
        ASSERT_NE(sensor, nullptr);
        EXPECT_EQ(sensor->outputNames(),
                  (std::vector<griddyn::stringVec>{{"current"}, {"voltage"}}));
        for (const auto output : {sensor->getOutput(0), sensor->getOutput(1)}) {
            EXPECT_TRUE(std::isfinite(output));
            EXPECT_NE(output, griddyn::kNullVal);
        }
    }
    const auto* roundMountain =
        dynamic_cast<const griddyn::Link*>(simulation->find("tx_ROUND MT_to_ROUND MT_200"));
    const auto* tevatr =
        dynamic_cast<const griddyn::Link*>(simulation->find("tx_TEVATR_to_TEVATR_200"));
    ASSERT_NE(roundMountain, nullptr);
    ASSERT_NE(tevatr, nullptr);
    EXPECT_FALSE(roundMountain->switchTest(1));
    EXPECT_FALSE(tevatr->switchTest(1));

    constexpr double secondsPerNanosecond = 1e-9;
    constexpr std::int64_t tripTime = 2'000'000'000;
    constexpr std::int64_t recordedEnd = 16'250'000'000;
    std::int64_t previous = -1;
    bool sawRepeatedGrant = false;
    std::string line;
    while (std::getline(trace, line)) {
        SCOPED_TRACE(line);
        const auto comma = line.find(',');
        ASSERT_NE(comma, std::string::npos);
        const auto timestamp = std::stoll(line.substr(0, comma));
        ASSERT_GE(timestamp, previous);
        sawRepeatedGrant = sawRepeatedGrant || timestamp == previous;
        const double grant = static_cast<double>(timestamp) * secondsPerNanosecond;
        ASSERT_NEAR(static_cast<double>(runner.Step(grant)), grant, 1e-8);
        ASSERT_NEAR(static_cast<double>(simulation->getSimulationTime()), grant, 1e-8);
        EXPECT_EQ(roundMountain->switchTest(1), timestamp >= tripTime);
        EXPECT_EQ(tevatr->switchTest(1), timestamp >= tripTime);
        previous = timestamp;
    }
    ASSERT_TRUE(trace.eof());
    ASSERT_EQ(previous, recordedEnd);
    EXPECT_TRUE(sawRepeatedGrant);

#ifdef GRIDDYN_ENABLE_EXPERIMENTAL_TEST_CASES
    constexpr std::int64_t step = 10'000'000;
    constexpr std::int64_t stop = 60'000'000'000;
    for (auto timestamp = previous + step; timestamp <= stop; timestamp += step) {
        const double grant = static_cast<double>(timestamp) * secondsPerNanosecond;
        ASSERT_NEAR(static_cast<double>(runner.Step(grant)), grant, 1e-8);
        ASSERT_NEAR(static_cast<double>(runner.Step(grant)), grant, 1e-8);
    }
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 60.0, 1e-8);
#endif
    runner.Finalize();
}
