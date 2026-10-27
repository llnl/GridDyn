/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for element readers

#include "../gtestHelper.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Load.h"
#include "griddyn/events/Event.h"
#include "runner/gridDynRunner.h"
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

void runCoupledTimeStepCase(double timeStep)
{
    auto simulation = std::make_shared<griddyn::GridDynSimulation>();
    simulation->consolePrintLevel = griddyn::PrintLevel::NO_PRINT;
    griddyn::GriddynRunner runner(simulation);

    std::string fileName = std::string(GRIDDYN_TEST_DIRECTORY "/dyn_tests1/test_dynSimple1.xml");
    char executable[] = "griddyn";
    char* argv[] = {executable, fileName.data()};
    ASSERT_EQ(runner.Initialize(2, argv), FUNCTION_EXECUTION_SUCCESS);

    simulation->set("timestep", timeStep);
    simulation->set("stoptime", 7.0);
    runner.simInitialize();
    ASSERT_TRUE(simulation->hasDynamics());
    ASSERT_EQ(simulation->currentProcessState(),
              griddyn::GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    constexpr double grantedTime = 6.695;
    constexpr double precedingGrant = 6.690;
    constexpr double lateEventTime = 6.698;
    constexpr double nextGrant = 6.705;
    constexpr double timeTolerance = 1e-8;

    const griddyn::CoreTime precedingTime = runner.Step(precedingGrant);
    EXPECT_NEAR(static_cast<double>(precedingTime), precedingGrant, timeTolerance);

    const griddyn::CoreTime actualTime = runner.Step(grantedTime);
    EXPECT_NEAR(static_cast<double>(actualTime), grantedTime, timeTolerance)
        << "runner advanced past the co-simulation time grant";
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()),
                static_cast<double>(actualTime),
                timeTolerance);

    auto* load = simulation->getBus(0)->getLoad(0);
    ASSERT_NE(load, nullptr);
    const double initialLoad = load->get("p");
    auto lateEvent = std::make_shared<griddyn::Event>();
    ASSERT_TRUE(lateEvent->setTarget(load, "p"));
    lateEvent->setValue(initialLoad + 0.1);
    lateEvent->setTime(lateEventTime);
    simulation->add(lateEvent);

    const griddyn::CoreTime eventGrantResult = runner.Step(lateEventTime);
    EXPECT_GE(static_cast<double>(eventGrantResult), static_cast<double>(actualTime));
    EXPECT_NEAR(static_cast<double>(eventGrantResult), lateEventTime, timeTolerance)
        << "runner failed to advance to the newly scheduled event time";
    EXPECT_NEAR(load->get("p"), initialLoad + 0.1, timeTolerance)
        << "event scheduled after the prior grant did not execute";

    const griddyn::CoreTime finalTime = runner.Step(nextGrant);
    EXPECT_GT(static_cast<double>(finalTime), static_cast<double>(eventGrantResult))
        << "runner stopped advancing after the late event";
    EXPECT_NEAR(static_cast<double>(finalTime), nextGrant, timeTolerance);
}

void runCoupledTimeStepWithPreScheduledEvent(double timeStep)
{
    auto simulation = std::make_shared<griddyn::GridDynSimulation>();
    simulation->consolePrintLevel = griddyn::PrintLevel::NO_PRINT;
    griddyn::GriddynRunner runner(simulation);

    std::string fileName = std::string(GRIDDYN_TEST_DIRECTORY "/dyn_tests1/test_dynSimple1.xml");
    char executable[] = "griddyn";
    char* argv[] = {executable, fileName.data()};
    ASSERT_EQ(runner.Initialize(2, argv), FUNCTION_EXECUTION_SUCCESS);

    simulation->set("timestep", timeStep);
    simulation->set("stoptime", 7.0);
    runner.simInitialize();
    ASSERT_TRUE(simulation->hasDynamics());
    ASSERT_EQ(simulation->currentProcessState(),
              griddyn::GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    constexpr double precedingGrant = 6.690;
    constexpr double grantedTime = 6.695;
    constexpr double lateEventTime = 6.698;
    constexpr double nextGrant = 6.705;
    constexpr double timeTolerance = 1e-8;

    const griddyn::CoreTime precedingTime = runner.Step(precedingGrant);
    ASSERT_NEAR(static_cast<double>(precedingTime), precedingGrant, timeTolerance);

    auto* load = simulation->getBus(0)->getLoad(0);
    ASSERT_NE(load, nullptr);
    const double initialLoad = load->get("p");
    auto lateEvent = std::make_shared<griddyn::Event>();
    ASSERT_TRUE(lateEvent->setTarget(load, "p"));
    lateEvent->setValue(initialLoad + 0.1);
    lateEvent->setTime(lateEventTime);
    simulation->add(lateEvent);

    const griddyn::CoreTime actualTime = runner.Step(grantedTime);
    EXPECT_NEAR(static_cast<double>(actualTime), grantedTime, timeTolerance)
        << "runner advanced past the co-simulation time grant toward a future event";
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), grantedTime, timeTolerance);
    EXPECT_NEAR(load->get("p"), initialLoad, timeTolerance)
        << "future event executed before its scheduled time";

    const griddyn::CoreTime eventTime = runner.Step(lateEventTime);
    EXPECT_NEAR(static_cast<double>(eventTime), lateEventTime, timeTolerance);
    EXPECT_NEAR(load->get("p"), initialLoad + 0.1, timeTolerance)
        << "future event did not execute when the runner reached it";

    const griddyn::CoreTime finalTime = runner.Step(nextGrant);
    EXPECT_NEAR(static_cast<double>(finalTime), nextGrant, timeTolerance);
}

}  // namespace

TEST(RunnerTests, CoupledStepWithTenMillisecondTimestepHonorsGrantAndLateEvent)
{
    runCoupledTimeStepCase(0.01);
}

TEST(RunnerTests, CoupledStepWithOneMillisecondTimestepHonorsGrantAndLateEvent)
{
    runCoupledTimeStepCase(0.001);
}

TEST(RunnerTests, CoupledStepWithTenMillisecondTimestepStopsBeforePreScheduledEvent)
{
    runCoupledTimeStepWithPreScheduledEvent(0.01);
}

TEST(RunnerTests, CoupledStepWithOneMillisecondTimestepStopsBeforePreScheduledEvent)
{
    runCoupledTimeStepWithPreScheduledEvent(0.001);
}

TEST(RunnerTests, RawCompatibilityOptionsApplyBeforeInputRegardlessOfArgumentOrder)
{
    for (const bool optionsFirst : {false, true}) {
        SCOPED_TRACE(optionsFirst);
        auto simulation = std::make_shared<griddyn::GridDynSimulation>();
        simulation->consolePrintLevel = griddyn::PrintLevel::NO_PRINT;
        griddyn::GriddynRunner runner(simulation);
        std::vector<std::string> arguments{"griddyn"};
        const std::string input = GRIDDYN_TEST_DIRECTORY "/comparison_tests/ieee14.raw";
        if (!optionsFirst) {
            arguments.push_back(input);
        }
        arguments.emplace_back("--file-flags=use_generator_voltage_targets");
        arguments.emplace_back("--flags=ignore_reactive_limits");
        arguments.emplace_back("--powerflow-only");
        if (optionsFirst) {
            arguments.push_back(input);
        }
        std::vector<char*> argv;
        for (auto& argument : arguments) {
            argv.push_back(argument.data());
        }
        ASSERT_EQ(runner.Initialize(static_cast<int>(argv.size()), argv.data()),
                  FUNCTION_EXECUTION_SUCCESS);
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
        ASSERT_NE(bus, nullptr);
        EXPECT_DOUBLE_EQ(bus->get("vtarget"), 1.03);
        EXPECT_DOUBLE_EQ(bus->getVoltage(), 1.01970);
        ASSERT_EQ(simulation->powerflow(), 0);
        EXPECT_NEAR(bus->getVoltage(), 1.03, 1e-6);
        ASSERT_NE(bus->getGen(0), nullptr);
        EXPECT_DOUBLE_EQ(bus->getGen(0)->getQmax(), 0.15);
        EXPECT_GT(-bus->getGen(0)->getReactivePower(), 0.15);
    }
}

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
            return message.contains("command-line event target was not resolved") &&
                message.contains(event);
        }));
}
