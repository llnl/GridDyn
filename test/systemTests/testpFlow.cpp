/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/exciters/ExciterSEXS.h"
#include "griddyn/measurement/Recorder.h"
#include "griddyn/simulation/GridDynActions.h"
#include "griddyn/simulation/GridDynSimulationFileOps.h"
#include "griddyn/solvers/SolverInterface.h"
#include "utilities/Hdf5DataFile.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;
using gmlc::utilities::countDiffs;

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

class PowerflowSystemTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(PowerflowSystemTests, ContinuationFindsAnalyticalNoseAndPreservesBase)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_two_bus.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), 0);
    const auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    const auto baseVoltage = bus->getVoltage();
    const auto baseState = gds->getState();

    ContinuationRequest request;
    request.loadScale = 6.0;
    request.monitorBuses = {20};
    const auto result = gds->continuationPowerFlow(request);
    ASSERT_EQ(result.status, ContinuationStatus::NOSE_FOUND);
    if (!result.nose) {
        FAIL() << "Expected an analytical nose point";
    }
    EXPECT_NEAR(result.nose->lambda, 0.8, 1e-5);
    ASSERT_EQ(result.nose->voltages.size(), 1);
    EXPECT_NEAR(result.nose->voltages.front(), std::sqrt(0.5), 1e-5);
    EXPECT_DOUBLE_EQ(result.points.front().lambda, 0.0);
    EXPECT_NEAR(result.points.front().voltages.front(), baseVoltage, 1e-8);
    for (const auto& sample : result.points) {
        EXPECT_LE(sample.residualNorm, 1e-8);
    }
    EXPECT_EQ(gds->getState(), baseState);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), baseVoltage);
    EXPECT_DOUBLE_EQ(bus->getLoad()->get("p"), 0.2);
}

TEST_F(PowerflowSystemTests, ContinuationStopsAtTargetOrReportsNoseBeforeIt)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_two_bus.xml");
    ASSERT_NE(gds, nullptr);
    const auto simple = gds->continuationPowerFlow(6.0);
    EXPECT_EQ(simple.status, ContinuationStatus::NOSE_FOUND);

    ContinuationRequest request;
    request.loadTargets = {{.load = "demand_load", .pMW = 120.0, .qMvar = 0.0}};
    request.monitorBuses = {20, 10};
    request.stopLambda = 0.5;
    const auto reached = gds->continuationPowerFlow(request);
    ASSERT_EQ(reached.status, ContinuationStatus::TARGET_REACHED);
    EXPECT_FALSE(reached.nose.has_value());
    ASSERT_FALSE(reached.points.empty());
    EXPECT_DOUBLE_EQ(reached.points.back().lambda, 0.5);
    EXPECT_EQ(reached.monitorBuses, (std::vector<index_t>{20, 10}));
    EXPECT_NEAR(reached.points.back().voltages[0],
                std::sqrt(0.5 * (1.0 + std::sqrt(1.0 - (0.7 * 0.7)))),
                1e-7);
    EXPECT_DOUBLE_EQ(reached.points.back().voltages[1], 1.0);

    request.stopLambda = 1.0;
    const auto beyond = gds->continuationPowerFlow(request);
    ASSERT_EQ(beyond.status, ContinuationStatus::NOSE_BEFORE_TARGET);
    if (!beyond.nose) {
        FAIL() << "Expected a nose point before the requested target";
    }
    EXPECT_NEAR(beyond.nose->lambda, 0.8, 1e-5);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getLoad()->get("p"), 0.2);

    EXPECT_THROW(gds->continuationPowerFlow(1.0), InvalidParameterValue);
}

TEST_F(PowerflowSystemTests, ContinuationMatchesCase9Reference)
{
    const auto file = std::string{GRIDDYN_TEST_DIRECTORY} + "/matlab_test_files/case9.m";
    gds = std::make_unique<GridDynSimulation>();
    loadFile(gds, file);
    gds->setAll("load", "pqlowvlimit", 0.1);

    ContinuationRequest request;
    request.loadScale = 1.5;
    request.stopLambda = 1.0;
    request.monitorBuses = {5, 7, 9};
    const auto target = gds->continuationPowerFlow(request);
    ASSERT_EQ(target.status, ContinuationStatus::TARGET_REACHED);
    ASSERT_FALSE(target.points.empty());
    EXPECT_DOUBLE_EQ(target.points.back().lambda, 1.0);
    // PYPOWER 5.1.21 runpf with 150% Pd and Qd, unchanged Pg and no Q limits.
    const std::array targetVoltages{0.9347529052, 0.9538736468, 0.9096351980};
    for (std::size_t busIndex = 0; busIndex < targetVoltages.size(); ++busIndex) {
        EXPECT_NEAR(target.points.back().voltages[busIndex], targetVoltages[busIndex], 1e-7);
    }

    request.stopLambda.reset();
    const auto curve = gds->continuationPowerFlow(request);
    ASSERT_EQ(curve.status, ContinuationStatus::NOSE_FOUND);
    if (!curve.nose) {
        FAIL() << "Expected a nose point for case9";
    }
    // PYPOWER runcpf with pseudo-arclength and step 0.003, using its best sampled point.
    EXPECT_NEAR(curve.nose->lambda, 2.5093133149, 1e-5);
    EXPECT_NEAR(curve.nose->voltages.back(), 0.6439267375, 0.002);

    request.stopLambda = 1.0;
    request.generatorTargets = {{.generator = "gen2", .pMW = 185.0}};
    const auto redispatch = gds->continuationPowerFlow(request);
    ASSERT_EQ(redispatch.status, ContinuationStatus::TARGET_REACHED);
    ASSERT_FALSE(redispatch.points.empty());
    EXPECT_NEAR(redispatch.points.back().voltages.back(), 0.9090427439, 1e-7);
}

TEST_F(PowerflowSystemTests, ContinuationEnforcesUpperQLimitThroughTheNose)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_q_limits.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), 0);
    const auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    const double originalQ = bus->getGen()->getReactivePower();

    ContinuationRequest request;
    request.enforceQLimits = true;
    request.loadTargets = {{.load = "reactive_load", .pMW = 0.0, .qMvar = 110.0}};
    request.monitorBuses = {20};
    const auto result = gds->continuationPowerFlow(request);
    ASSERT_EQ(result.status, ContinuationStatus::NOSE_FOUND);
    ASSERT_EQ(result.qLimitEvents.size(), 1);
    const auto& event = result.qLimitEvents.front();
    EXPECT_EQ(event.busId, 20);
    EXPECT_EQ(event.generator, "regulator");
    EXPECT_TRUE(event.upper);
    EXPECT_NEAR(event.lambda, 0.15, 2e-7);
    EXPECT_DOUBLE_EQ(event.qMvar, 25.0);

    // After the switch, V(1-V)/X = Qload - Qmax. Its maximum is 0.5 p.u. at V=0.5.
    if (!result.nose) {
        FAIL() << "Expected a nose point after the upper Q limit was reached";
    }
    EXPECT_NEAR(result.nose->lambda, 0.65, 1e-6);
    EXPECT_NEAR(result.nose->voltages.front(), 0.5, 1e-5);
    EXPECT_LE(result.nose->residualNorm, 1e-8);
    EXPECT_EQ(bus->getType(), GridBus::BusType::PV);
    EXPECT_DOUBLE_EQ(bus->getGen()->getReactivePower(), originalQ);
}

TEST_F(PowerflowSystemTests, ContinuationEnforcesLowerQLimitBeforeExactTarget)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_q_limits.xml");
    ASSERT_NE(gds, nullptr);
    ContinuationRequest request;
    request.enforceQLimits = true;
    request.loadTargets = {{.load = "reactive_load", .pMW = 0.0, .qMvar = -90.0}};
    request.monitorBuses = {20};
    request.stopLambda = 0.55;

    const auto result = gds->continuationPowerFlow(request);
    ASSERT_EQ(result.status, ContinuationStatus::TARGET_REACHED);
    ASSERT_EQ(result.qLimitEvents.size(), 1);
    EXPECT_FALSE(result.qLimitEvents.front().upper);
    EXPECT_NEAR(result.qLimitEvents.front().lambda, 0.3, 2e-7);
    EXPECT_DOUBLE_EQ(result.qLimitEvents.front().qMvar, -20.0);
    EXPECT_DOUBLE_EQ(result.points.back().lambda, 0.55);
    EXPECT_NEAR(result.points.back().voltages.front(), (1.0 + std::sqrt(1.5)) / 2.0, 1e-8);

    request.stopLambda = 0.2;
    const auto before = gds->continuationPowerFlow(request);
    ASSERT_EQ(before.status, ContinuationStatus::TARGET_REACHED);
    EXPECT_TRUE(before.qLimitEvents.empty());
    EXPECT_DOUBLE_EQ(before.points.back().voltages.front(), 1.0);
}

TEST_F(PowerflowSystemTests, ContinuationHandlesQBoundAtBaseAndLimitInducedTurn)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_q_limits.xml");
    ASSERT_NE(gds, nullptr);
    auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen();
    ASSERT_NE(generator, nullptr);

    ContinuationRequest request;
    request.enforceQLimits = true;
    request.loadTargets = {{.load = "reactive_load", .pMW = 0.0, .qMvar = 110.0}};
    request.monitorBuses = {20};
    request.stopLambda = 0.0;
    generator->set("qmax", 5.0, units::MVAR);
    const auto base = gds->continuationPowerFlow(request);
    ASSERT_EQ(base.status, ContinuationStatus::TARGET_REACHED);
    ASSERT_EQ(base.qLimitEvents.size(), 1);
    EXPECT_DOUBLE_EQ(base.qLimitEvents.front().lambda, 0.0);
    EXPECT_NEAR(base.points.front().voltages.front(), (1.0 + std::sqrt(0.9)) / 2.0, 1e-8);

    // This voltage-controlled state lies below the maximum-power voltage of
    // the saturated two-bus system. With Q fixed at its upper bound, increasing
    // load would require voltage to increase back above its control target.
    bus->set("voltage", 0.4);
    generator->set("qmin", -90.0, units::MVAR);
    generator->set("qmax", 25.0, units::MVAR);
    request.stopLambda.reset();
    const auto turn = gds->continuationPowerFlow(request);
    ASSERT_EQ(turn.status, ContinuationStatus::Q_LIMIT_TURN);
    ASSERT_EQ(turn.qLimitEvents.size(), 1);
    EXPECT_NEAR(turn.qLimitEvents.front().lambda, 0.63, 2e-7);
    EXPECT_NEAR(turn.points.back().voltages.front(), 0.4, 1e-6);
    EXPECT_FALSE(turn.nose.has_value());
}

TEST_F(PowerflowSystemTests, ContinuationHoldsLocalVoltageUntilBothGeneratorsReachQLimits)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_q_limits.xml");
    ASSERT_NE(gds, nullptr);
    auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    auto* second = new Generator("second_regulator");
    second->set("qmax", 0.35);
    bus->add(second);

    ContinuationRequest request;
    request.enforceQLimits = true;
    request.loadScale = 11.0;
    request.monitorBuses = {20};
    request.stopLambda = 0.65;
    const auto result = gds->continuationPowerFlow(request);
    ASSERT_EQ(result.status, ContinuationStatus::TARGET_REACHED);
    ASSERT_EQ(result.qLimitEvents.size(), 2);
    const auto& firstEvent = result.qLimitEvents.front();
    const auto& lastEvent = result.qLimitEvents.back();
    EXPECT_EQ(firstEvent.generator, "regulator");
    EXPECT_EQ(lastEvent.generator, "second_regulator");
    EXPECT_NEAR(firstEvent.lambda, 0.4, 2e-7);
    EXPECT_NEAR(lastEvent.lambda, 0.5, 2e-7);
    EXPECT_EQ(firstEvent.regulatedBusId, firstEvent.busId);
    for (const auto& point : result.points) {
        if (point.lambda <= lastEvent.lambda) {
            EXPECT_NEAR(point.voltages.front(), 1.0, 2e-7);
        }
    }
    EXPECT_NEAR(result.points.back().voltages.front(), (1.0 + std::sqrt(0.7)) / 2.0, 1e-7);
    EXPECT_EQ(bus->getType(), GridBus::BusType::PV);
}

namespace {
class TemporaryStateFile {
  public:
    explicit TemporaryStateFile(std::string_view suffix = ".h5")
    {
        static std::atomic<unsigned int> sequence{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("griddyn-state-" + std::to_string(timestamp) + "-" + std::to_string(sequence++) +
             std::string(suffix));
    }
    ~TemporaryStateFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    std::string quotedPath() const { return '"' + path.string() + '"'; }

    std::filesystem::path path;
};

void exportMatlabStateFixture(const std::filesystem::path& source, const char* name)
{
    const auto* directory = std::getenv("GRIDDYN_MATLAB_STATE_FIXTURES");
    if (directory != nullptr && *directory != '\0') {
        std::filesystem::copy_file(source,
                                   std::filesystem::path(directory) / name,
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

void disableFixtureRecorderFiles(GridDynSimulation& simulation)
{
    for (const auto& collector : simulation.getCollectors()) {
        if (auto* recorder = dynamic_cast<Recorder*>(collector.get())) {
            recorder->set("file", "");
        }
    }
}

void nameCheckpointGeneratorModels(GridDynSimulation& simulation)
{
    for (index_t ii = 0; ii < 2; ++ii) {
        auto* generator = simulation.getBus(ii)->getGen();
        for (const auto* role : {"genmodel", "exciter", "governor"}) {
            generator->find(role)->setName(role);
        }
    }
}
}  // namespace

TEST_F(PowerflowSystemTests, ScriptedContinuationArchivesTargetAndUnmetTarget)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_two_bus.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    const auto baseState = gds->getState();
    const auto baseTime = gds->getSimulationTime();
    const auto baseVoltage = bus->getVoltage();
    const auto baseName = gds->getName();
    const TemporaryStateFile archive(" continuation.h5");

    ASSERT_EQ(gds->execute("continuation 6 target 0.5 monitor 20,10 archive " +
                           archive.quotedPath()),
              FUNCTION_EXECUTION_SUCCESS);
    const auto target = readContinuationPowerFlow(archive.path.string());
    ASSERT_EQ(target.status, ContinuationStatus::TARGET_REACHED);
    ASSERT_FALSE(target.points.empty());
    EXPECT_EQ(target.monitorBuses, (std::vector<index_t>{20, 10}));
    EXPECT_DOUBLE_EQ(target.points.back().lambda, 0.5);
    EXPECT_NEAR(target.points.back().voltages.front(),
                std::sqrt(0.5 * (1.0 + std::sqrt(1.0 - (0.7 * 0.7)))),
                1e-7);

    gds->addActions("continuation 6 target 1 archive " + archive.quotedPath() +
                        "; set name resumed",
                    "studies.xml");
    try {
        gds->run();
        ADD_FAILURE() << "expected an unmet continuation target";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::EXECUTION);
        const std::string_view message(error.what());
        EXPECT_TRUE(message.starts_with("studies.xml: action 'continuation 6 target 1")) << message;
        EXPECT_TRUE(message.contains("nose_before_target")) << message;
    }
    const auto limit = readContinuationPowerFlow(archive.path.string());
    EXPECT_EQ(limit.status, ContinuationStatus::NOSE_BEFORE_TARGET);
    ASSERT_TRUE(limit.nose.has_value());
    EXPECT_NEAR(limit.nose->lambda, 0.8, 1e-5);
    EXPECT_EQ(gds->getName(), baseName);
    EXPECT_EQ(gds->getState(), baseState);
    EXPECT_EQ(gds->getSimulationTime(), baseTime);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), baseVoltage);
    EXPECT_DOUBLE_EQ(bus->getLoad()->get("p"), 0.2);
    ASSERT_EQ(gds->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(gds->getName(), "resumed");
}

TEST_F(PowerflowSystemTests, ContinuationArchiveRoundTripsAndAppendsStudies)
{
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "continuation_q_limits.xml");
    ASSERT_NE(gds, nullptr);
    ContinuationRequest request;
    request.loadScale = 11.0;
    request.monitorBuses = {20, 10};
    request.enforceQLimits = true;
    const auto result = gds->continuationPowerFlow(request);
    ASSERT_EQ(result.status, ContinuationStatus::NOSE_FOUND);
    ASSERT_EQ(result.qLimitEvents.size(), 1);

    const TemporaryStateFile file(" q limits.h5");
    const auto path = file.path.string();
    ASSERT_EQ(gds->execute("continuation 11 monitor 20,10 qlimits archive " + file.quotedPath()),
              FUNCTION_EXECUTION_SUCCESS);

    ContinuationResult large;
    constexpr std::size_t busCount = 12000;
    large.status = ContinuationStatus::TARGET_REACHED;
    large.targetLambda = 1.0;
    large.monitorBuses.resize(busCount);
    std::iota(large.monitorBuses.begin(), large.monitorBuses.end(), index_t{100});
    ContinuationPoint row{.lambda = 0.0,
                          .residualNorm = 1e-9,
                          .voltages = std::vector<double>(busCount),
                          .angles = std::vector<double>(busCount)};
    for (std::size_t bus = 0; bus < busCount; ++bus) {
        row.voltages[bus] = 0.8 + (static_cast<double>(bus) * 1e-5);
        row.angles[bus] = -static_cast<double>(bus) * 1e-5;
    }
    large.points.push_back(row);
    row.lambda = 1.0;
    row.voltages.back() = 1.2;
    large.points.push_back(std::move(row));
    saveContinuationPowerFlow(large, path);

    const auto first = readContinuationPowerFlow(path, 0);
    ASSERT_EQ(first.points.size(), result.points.size());
    ASSERT_EQ(first.qLimitEvents.size(), 1);
    EXPECT_EQ(first.status, result.status);
    EXPECT_EQ(first.monitorBuses, result.monitorBuses);
    EXPECT_EQ(first.limitingLambda(), result.limitingLambda());
    if (!first.nose || !result.nose) {
        FAIL() << "Expected the archive to preserve the nose point";
    }
    EXPECT_EQ(first.nose->voltages, result.nose->voltages);
    EXPECT_EQ(first.qLimitEvents.front().generator, result.qLimitEvents.front().generator);
    EXPECT_EQ(first.qLimitEvents.front().regulatedBusId, 20);
    EXPECT_DOUBLE_EQ(first.qLimitEvents.front().qMvar, 25.0);

    const auto latest = readContinuationPowerFlow(path);
    ASSERT_EQ(latest.points.size(), 2);
    EXPECT_EQ(latest.status, large.status);
    EXPECT_EQ(latest.targetLambda, large.targetLambda);
    EXPECT_EQ(latest.monitorBuses, large.monitorBuses);
    EXPECT_EQ(latest.points.front().voltages, large.points.front().voltages);
    EXPECT_EQ(latest.points.back().angles, large.points.back().angles);
    EXPECT_DOUBLE_EQ(latest.points.back().lambda, 1.0);
    EXPECT_DOUBLE_EQ(latest.points.back().residualNorm, 1e-9);
    EXPECT_TRUE(latest.qLimitEvents.empty());
    EXPECT_THROW(readContinuationPowerFlow(path, 2), FileOperationError);

    large.points.back().angles.pop_back();
    EXPECT_THROW(saveContinuationPowerFlow(large, path, false), FileOperationError);
    EXPECT_EQ(readContinuationPowerFlow(path).monitorBuses, latest.monitorBuses);

    const ContinuationResult failed;
    saveContinuationPowerFlow(failed, path, false);
    const auto replacement = readContinuationPowerFlow(path);
    EXPECT_EQ(replacement.status, ContinuationStatus::BASE_SOLVE_FAILED);
    EXPECT_TRUE(replacement.points.empty());
    EXPECT_THROW(readContinuationPowerFlow(path, 1), FileOperationError);
}

class FlagRecordingPowerFlowSimulation: public GridDynSimulation {
  public:
    std::vector<std::pair<std::uint32_t, CheckLevel>> adjustmentCalls;

    ChangeCode
        powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, CheckLevel level) override
    {
        adjustmentCalls.emplace_back(flags, level);
        return ChangeCode::NO_CHANGE;
    }
};

TEST_F(PowerflowSystemTests, FirstRunLimitsOnlyPreservesFlagsAndDisablesSteppedControls)
{
    FlagRecordingPowerFlowSimulation simulation;
    loadCdf(&simulation, std::string(ieee_test_directory) + "ieee30_no_limit.cdf");
    simulation.setFlag("first_run_limits_only");
    simulation.setFlag("ignore_bus_limits");
    ASSERT_TRUE(simulation.isFlagSet(FIRST_RUN_LIMITS_ONLY));

    ASSERT_EQ(simulation.powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation.adjustmentCalls.size(), 2U);
    EXPECT_EQ(simulation.adjustmentCalls[0].second, CheckLevel::REVERSABLE_ONLY);
    EXPECT_EQ(simulation.adjustmentCalls[1].second, CheckLevel::FULL_CHECK);
    for (const auto& call : simulation.adjustmentCalls) {
        const auto flags = call.first;
        EXPECT_TRUE(CHECK_CONTROLFLAG(flags, DISABLE_SHUNT_ADJUSTMENTS));
        EXPECT_TRUE(CHECK_CONTROLFLAG(flags, DISABLE_TAP_CHANGER_ADJUSTMENTS));
        EXPECT_TRUE(CHECK_CONTROLFLAG(flags, IGNORE_BUS_LIMITS));
    }

    simulation.adjustmentCalls.clear();
    ASSERT_EQ(simulation.powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation.adjustmentCalls.size(), 2U);
    for (const auto& call : simulation.adjustmentCalls) {
        const auto flags = call.first;
        EXPECT_FALSE(CHECK_CONTROLFLAG(flags, DISABLE_SHUNT_ADJUSTMENTS));
        EXPECT_FALSE(CHECK_CONTROLFLAG(flags, DISABLE_TAP_CHANGER_ADJUSTMENTS));
        EXPECT_TRUE(CHECK_CONTROLFLAG(flags, IGNORE_BUS_LIMITS));
    }
}

TEST_F(PowerflowSystemTests, RecoveryAndNetworkFlagsAreConfigurable)
{
    GridDynSimulation simulation;
    simulation.setFlag("no_auto_slack_bus");
    simulation.setFlag("no_auto_disconnect");
    simulation.setFlag("no_automatic_load_loss");
    simulation.setFlag("no_reset");
    simulation.setFlag("force_event_powerflow");
    EXPECT_TRUE(simulation.isFlagSet(DISABLE_AUTO_SLACK_BUS));
    EXPECT_TRUE(simulation.isFlagSet(DISABLE_AUTO_DISCONNECT));
    EXPECT_TRUE(simulation.isFlagSet(DISABLE_AUTOMATIC_LOAD_LOSS));
    EXPECT_TRUE(simulation.isFlagSet(NO_RESET));
    EXPECT_TRUE(simulation.isFlagSet(FORCE_POWER_FLOW));
}

TEST_F(PowerflowSystemTests, NoPowerFlowAdjustmentsSkipsNormalAdjustmentPasses)
{
    FlagRecordingPowerFlowSimulation simulation;
    loadCdf(&simulation, std::string(ieee_test_directory) + "ieee30_no_limit.cdf");
    simulation.setFlag("no_powerflow_adjustments");

    ASSERT_EQ(simulation.powerflow(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(simulation.adjustmentCalls.empty());
}

/** test to make sure the basic power flow loads and runs*/
TEST_F(PowerflowSystemTests, PFlowTest1)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);

    gds->pFlowInitialize();
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::INITIALIZED);

    int count = gds->getInt("totalbuscount");

    EXPECT_EQ(count, 9);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 9);

    EXPECT_EQ(runJacobianCheck(gds, cPflowSolverMode, false), 0);
    EXPECT_EQ(gds->getInt("jacsize"), 108);
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

// testcase for power flow from initial start
TEST_F(PowerflowSystemTests, PFlowTest2)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b2.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);
    gds->pFlowInitialize();
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::INITIALIZED);

    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);
    gds->powerflow();
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    gds->getVoltage(volts2);
    gds->getAngle(ang2);
    // ensure the sizes are equal
    ASSERT_EQ(volts1.size(), volts2.size());
    // check the bus voltages and angles
    auto vdiff = countDiffs(volts1, volts2, 0.0001);
    auto adiff = countDiffs(ang1, ang2, 0.0001);

    EXPECT_EQ(vdiff, 0);
    EXPECT_EQ(adiff, 0);
}

// testcase for power flow from zeros start
TEST_F(PowerflowSystemTests, PFlowTest3)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b.xml";
    gds = readSimXMLFile(fileName);
    requireState(GridDynSimulation::GridState::STARTUP);
    gds->pFlowInitialize();
    requireState(GridDynSimulation::GridState::INITIALIZED);

    std::string fname2 = std::string(pFlow_test_directory) + "test_powerflow3m9b2.xml";
    gds2 = readSimXMLFile(fname2);
    requireState2(GridDynSimulation::GridState::STARTUP);
    gds2->pFlowInitialize();
    requireState2(GridDynSimulation::GridState::INITIALIZED);

    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    gds2->powerflow();
    requireState2(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    gds2->getVoltage(volts2);
    gds2->getAngle(ang2);
    // ensure the sizes are equal
    ASSERT_EQ(volts1.size(), volts2.size());
    // check the bus voltages and angles
    auto vdiff = countDiffs(volts1, volts2, 0.0001);
    auto adiff = countDiffs(ang1, ang2, 0.0001);
    EXPECT_EQ(vdiff, 0);
    EXPECT_EQ(adiff, 0);
}

/** test the ieee 30 bus case with no shunts*/
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PowerflowSystemTests, PflowTest30NoShunt)
{
    gds = std::make_unique<GridDynSimulation>();
    std::string fileName = std::string(ieee_test_directory) + "ieee30_no_shunt_cap_tap_limit.cdf";

    loadCdf(gds.get(), fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 30);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 41);
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    gds->pFlowInitialize();
    requireState(GridDynSimulation::GridState::INITIALIZED);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds->getVoltage(volts2);
    gds->getAngle(ang2);

    auto vdiff = countDiffs(volts1, volts2, 0.001);
    auto adiff = countDiffs(ang1, ang2, 0.001);

    if ((vdiff > 0) || (adiff > 0)) {
        printBusResultDeviations(volts1, volts2, ang1, ang2);
    }
    EXPECT_EQ(vdiff, 0U);
    EXPECT_EQ(adiff, 0U);

    // check that the reset works correctly
    gds->reset(ResetLevels::VOLTAGE_ANGLE);
    gds->getAngle(ang1);
    for (double angleValue : ang1) {
        EXPECT_NEAR(angleValue, 0.0, 1e-6);
    }

    gds->pFlowInitialize();
    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds->getVoltage(volts1);
    gds->getAngle(ang1);
    auto vdiff2 = countDiffs(volts1, volts2, 0.0005);
    auto adiff2 = countDiffs(volts1, volts2, 0.0009);

    EXPECT_EQ(vdiff2, 0U);
    EXPECT_EQ(adiff2, 0U);
}

/** test the IEEE 30 bus case with no reactive limits*/
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PowerflowSystemTests, PflowTest30NoLimit)
{
    gds = std::make_unique<GridDynSimulation>();
    std::string fileName = std::string(ieee_test_directory) + "ieee30_no_limit.cdf";

    loadCdf(gds.get(), fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 30);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 41);
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    gds->pFlowInitialize();
    requireState(GridDynSimulation::GridState::INITIALIZED);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds->getVoltage(volts2);
    gds->getAngle(ang2);

    auto vdiff = countDiffs(volts1, volts2, 0.001);
    auto adiff = countDiffs(ang1, ang2, 0.01 * kPI / 180.0);  // 0.01 degrees
    if ((vdiff > 0) || (adiff > 0)) {
        printBusResultDeviations(volts1, volts2, ang1, ang2);
    }
    EXPECT_EQ(vdiff, 0U);
    EXPECT_EQ(adiff, 0U);

    // check that the reset works correctly
    gds->reset(ResetLevels::VOLTAGE_ANGLE);
    gds->getAngle(ang1);
    for (double angleValue : ang1) {
        EXPECT_NEAR(angleValue, 0.0, 1e-6);
    }
    gds->pFlowInitialize();
    gds->powerflow();

    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    gds->getVoltage(volts1);
    gds->getAngle(ang1);
    auto vdiff2 = countDiffs(volts1, volts2, 0.0005);
    auto adiff2 = countDiffs(volts1, volts2, 0.0009);

    EXPECT_EQ(vdiff2, 0U);
    EXPECT_EQ(adiff2, 0U);
}

// test case for power flow automatic adjustment
TEST_F(PowerflowSystemTests, TestPFlowPadjust)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b_Padjust.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    requireState(GridDynSimulation::GridState::STARTUP);

    std::vector<double> generation1;
    std::vector<double> generation2;

    gds->pFlowInitialize();
    gds->updateLocalCache();
    gds->getBusGenerationReal(generation1);
    gds->powerflow();
    auto warnCount = gds->getInt("warncount");
    EXPECT_EQ(warnCount, 0);
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    gds->getBusGenerationReal(generation2);
    // there should be 2 generators+ the swing bus that had their real power levels adjusted instead
    // of just the swing bus
    auto cnt = countDiffs(generation2, generation1, 0.0002);
    EXPECT_EQ(cnt, 3U);
}

/** test case for dc power flow*/
TEST_F(PowerflowSystemTests, PflowTestDcflow)
{
    gds = std::make_unique<GridDynSimulation>();
    std::string fileName = std::string(ieee_test_directory) + "ieee30_no_limit.cdf";

    loadCdf(gds.get(), fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    int count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 30);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 41);
    std::vector<double> volts1;
    std::vector<double> ang1;
    std::vector<double> volts2;
    std::vector<double> ang2;

    gds->getVoltage(volts1);
    gds->getAngle(ang1);

    auto newSolver = makeSolver("kinsol");
    newSolver->setName("dcflow");
    newSolver->set("mode", "dc, algebraic");
    gds->add(std::shared_ptr<SolverInterface>(std::move(newSolver)));

    auto smode = gds->getSolverMode("dcflow");
    gds->set("defpowerflow", "dcflow");
    gds->pFlowInitialize(0.0);
    requireState(GridDynSimulation::GridState::INITIALIZED);

    runJacobianCheck(gds, smode);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

// iterated power flow test case
TEST_F(PowerflowSystemTests, TestIteratedPflow)
{
    std::string fileName = std::string(pFlow_test_directory) + "iterated_test_case.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    requireState(GridDynSimulation::GridState::STARTUP);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    gds->set("recorddirectory", pFlow_test_directory);
    gds->run();
    ASSERT_GT(gds->getSimulationTime(), 575.0);
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointRestoresSolutionAcrossIterations)
{
    const TemporaryStateFile checkpoint(" powerflow.json");
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    auto* load = bus->getLoad();
    ASSERT_NE(load, nullptr);
    const double voltage = bus->getVoltage();
    const double angle = bus->getAngle();
    ASSERT_EQ(gds->execute("save powerflow-checkpoint " + checkpoint.quotedPath()),
              FUNCTION_EXECUTION_SUCCESS);

    load->set("p", 0.8);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_GT(std::abs(bus->getVoltage() - voltage), 1e-4);
    loadPowerFlowCheckpoint(gds.get(), checkpoint.path.string());
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(bus->getAngle(), angle);
    EXPECT_DOUBLE_EQ(load->get("p"), 0.8);

    auto second = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    second->add("load powerflow-checkpoint " + checkpoint.quotedPath());
    ASSERT_EQ(second->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(second->getBus(1)->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(second->getBus(1)->getAngle(), angle);
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointRejectsInvalidFileBeforeChangingState)
{
    const auto checkpoint =
        std::filesystem::temp_directory_path() / "griddyn-invalid-pflow-checkpoint.json";
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->getBus(1);
    const double voltage = bus->getVoltage();
    {
        std::ofstream output(checkpoint);
        output
            << R"({"format":"griddyn-powerflow-checkpoint","version":1,"scope":"solution","state":[],"buses":[]})";
    }
    EXPECT_THROW(loadPowerFlowCheckpoint(gds.get(), checkpoint.string()), FileOperationError);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
    std::filesystem::remove(checkpoint);
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointInIteratedPowerFlow)
{
    const auto checkpoint =
        std::filesystem::temp_directory_path() / "griddyn-iterated-pflow-checkpoint.json";
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "iterated_test_case.xml");
    ASSERT_NE(gds, nullptr);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->eventDrivenPowerflow(10.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->findByUserID("bus", 25);
    ASSERT_NE(bus, nullptr);
    auto* gridBus = dynamic_cast<GridBus*>(bus);
    ASSERT_NE(gridBus, nullptr);
    const double voltage = gridBus->getVoltage();
    const double angle = gridBus->getAngle();
    savePowerFlowCheckpoint(gds.get(), checkpoint.string());
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    loadPowerFlowCheckpoint(gds.get(), checkpoint.string());
    EXPECT_DOUBLE_EQ(gridBus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(gridBus->getAngle(), angle);
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(20.0));
    std::filesystem::remove(checkpoint);
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointAllowsEditedBusValues)
{
    const auto checkpoint =
        std::filesystem::temp_directory_path() / "griddyn-edited-pflow-checkpoint.json";
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    savePowerFlowCheckpoint(gds.get(), checkpoint.string());
    nlohmann::json document;
    {
        std::ifstream input(checkpoint);
        input >> document;
    }
    document["buses"][1]["voltage_pu"] = 0.91;
    document["buses"][1]["angle_rad"] = 0.04;
    {
        std::ofstream output(checkpoint);
        output << document.dump(2);
    }
    loadPowerFlowCheckpoint(gds.get(), checkpoint.string());
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getVoltage(), 0.91);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getAngle(), 0.04);
    std::filesystem::remove(checkpoint);
}

TEST_F(PowerflowSystemTests, BinaryStatePowerFlowRoundTrip)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getSolverMode("powerflow");
    auto solver = gds->getSolverInterface(mode);
    ASSERT_GT(solver->size(), 0U);
    auto* bus = gds->getBus(1);
    auto* link = gds->getLink(0);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(link, nullptr);
    const auto voltage = bus->getVoltage();
    const auto angle = bus->getAngle();
    const auto linkPower = link->getRealPower();
    const std::vector<double> expected(solver->stateData(), solver->stateData() + solver->size());
    gds->set("statefile", file.path.string());
    saveState(gds.get(), "", cEmptySolverMode, false);
    exportMatlabStateFixture(file.path, "powerflow.h5");
    TemporaryStateFile diagnostic;
    writeVector(CoreTime(0.25),
                STATE_INFORMATION,
                7,
                mode,
                solver->size(),
                expected.data(),
                diagnostic.path.string(),
                false);
    exportMatlabStateFixture(diagnostic.path, "solver-diagnostic.h5");
    EXPECT_THROW(readStateBinary(diagnostic.path.string()), FileOperationError);
    TemporaryStateFile jacState;
    captureJacState(gds.get(), jacState.path.string(), mode);
    exportMatlabStateFixture(jacState.path, "jac-state.h5");
    EXPECT_THROW(readStateBinary(jacState.path.string()), FileOperationError);
    TemporaryStateFile jacobian;
    saveJacobian(gds.get(), jacobian.path.string(), mode);
    exportMatlabStateFixture(jacobian.path, "jacobian.h5");
    writeVector(CoreTime(0.25),
                RESIDUAL_INFORMATION,
                7,
                mode,
                solver->size(),
                expected.data(),
                file.path.string(),
                true);
    MatrixDataSparse<double> matrix;
    matrix.assign(0, 1, 0.75);
    writeArray(CoreTime(0.25), JACOBIAN_DATA, 7, mode, matrix, file.path.string(), true);
    exportMatlabStateFixture(file.path, "mixed.h5");
    EXPECT_EQ(readStateBinary(file.path.string()).state, expected);
    const auto saved = readStateBinary(file.path.string());
    EXPECT_EQ(saved.mode, mode);
    EXPECT_EQ(saved.time, gds->getSimulationTime());
    EXPECT_EQ(saved.state, expected);
    EXPECT_TRUE(saved.derivatives.empty());

    bus->setVoltageAngle(voltage - 0.1, angle - 0.05);
    gds->updateLocalCache();
    ASSERT_GT(std::abs(link->getRealPower() - linkPower), 1e-3);
    std::fill(solver->stateData(), solver->stateData() + solver->size(), -42.0);
    loadStateBinary(gds.get(), "", cEmptySolverMode);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), solver->stateData()));
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage - 0.1);

    gds->checkpointRestore(saved, CheckpointRestoreLevel::MINIMAL, mode);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(bus->getAngle(), angle);
    EXPECT_NEAR(link->getRealPower(), linkPower, 1e-12);
}

TEST_F(PowerflowSystemTests, BinaryStateDynamicRoundTripAndAppend)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    ASSERT_EQ(gds->dynInitialize(CoreTime(0.123456789)), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getCurrentMode();
    auto solver = gds->getSolverInterface(mode);
    ASSERT_TRUE(hasDifferential(mode));
    ASSERT_GT(solver->size(), 0U);
    std::vector<double> expectedState(solver->size());
    std::vector<double> expectedDerivative(solver->size());
    for (count_t index = 0; index < solver->size(); ++index) {
        solver->stateData()[index] = 0.125 + index;
        solver->derivData()[index] = -0.25 - index;
        expectedState[index] = solver->stateData()[index];
        expectedDerivative[index] = solver->derivData()[index];
    }
    saveStateBinary(gds.get(), file.path.string(), mode, false);
    auto saved = readStateBinary(file.path.string());
    EXPECT_EQ(saved.time, CoreTime(0.123456789));
    EXPECT_EQ(saved.state, expectedState);
    EXPECT_EQ(saved.derivatives, expectedDerivative);
    {
        utilities::Hdf5DataFile input(file.path.string(),
                                      utilities::Hdf5DataFile::OpenMode::READ_ONLY);
        const auto records = input.readRecords();
        ASSERT_EQ(records.size(), 2U);
        EXPECT_EQ(records[0].time_ns, 123456789);
        EXPECT_EQ(records[0].frame, records[1].frame);
        EXPECT_EQ(input.readValues(records[0]), expectedState);
        EXPECT_EQ(input.readValues(records[1]), expectedDerivative);
    }

    solver->stateData()[0] = 123.5;
    solver->derivData()[0] = -456.5;
    expectedState[0] = 123.5;
    expectedDerivative[0] = -456.5;
    saveStateBinary(gds.get(), file.path.string(), mode, true);
    exportMatlabStateFixture(file.path, "dynamic.h5");
    saved = readStateBinary(file.path.string());
    EXPECT_EQ(saved.state, expectedState);
    EXPECT_EQ(saved.derivatives, expectedDerivative);

    std::fill(solver->stateData(), solver->stateData() + solver->size(), 1.0);
    std::fill(solver->derivData(), solver->derivData() + solver->size(), 2.0);
    loadStateBinary(gds.get(), file.path.string());
    EXPECT_TRUE(std::equal(expectedState.begin(), expectedState.end(), solver->stateData()));
    EXPECT_TRUE(
        std::equal(expectedDerivative.begin(), expectedDerivative.end(), solver->derivData()));
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(0.123456789));
    std::filesystem::resize_file(file.path, std::filesystem::file_size(file.path) / 2);
    EXPECT_THROW(loadStateBinary(gds.get(), file.path.string()), FileOperationError);
    EXPECT_TRUE(std::equal(expectedState.begin(), expectedState.end(), solver->stateData()));
    EXPECT_TRUE(
        std::equal(expectedDerivative.begin(), expectedDerivative.end(), solver->derivData()));
}

TEST_F(PowerflowSystemTests, BinaryStateCheckpointRestoreLevels)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);

    const CoreTime captureTime(0.123456789);
    ASSERT_EQ(gds->dynInitialize(captureTime), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getCurrentMode();
    auto solver = gds->getSolverInterface(mode);
    ASSERT_NE(solver, nullptr);
    ASSERT_TRUE(hasDifferential(mode));
    ASSERT_GT(solver->size(), 0U);

    ASSERT_EQ(gds->execute("save state " + file.quotedPath()), FUNCTION_EXECUTION_SUCCESS);
    const auto saved = readStateBinary(file.path.string());
    ASSERT_EQ(saved.time, captureTime);
    const auto expectedState = saved.state;
    const auto expectedDerivative = saved.derivatives;

    ASSERT_EQ(gds->run(CoreTime(0.173456789)), FUNCTION_EXECUTION_SUCCESS);
    const auto laterSimulationTime = gds->getSimulationTime();
    const auto laterSolverTime = solver->getSolverTime();
    ASSERT_GT(laterSimulationTime, captureTime);
    ASSERT_GT(laterSolverTime, captureTime);

    std::fill(solver->stateData(), solver->stateData() + solver->size(), -11.0);
    std::fill(solver->derivData(), solver->derivData() + solver->size(), -12.0);
    ASSERT_EQ(gds->execute("load state " + file.quotedPath()), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(std::vector<double>(solver->stateData(), solver->stateData() + solver->size()),
              expectedState);
    EXPECT_EQ(std::vector<double>(solver->derivData(), solver->derivData() + solver->size()),
              expectedDerivative);
    EXPECT_EQ(gds->getSimulationTime(), laterSimulationTime);
    EXPECT_EQ(solver->getSolverTime(), laterSolverTime);

    auto incompatible = saved;
    incompatible.state.pop_back();
    const std::vector<double> beforeRejectedRestore(solver->stateData(),
                                                    solver->stateData() + solver->size());
    EXPECT_THROW(gds->checkpointRestore(incompatible, CheckpointRestoreLevel::MINIMAL, mode),
                 FileOperationError);
    EXPECT_EQ(std::vector<double>(solver->stateData(), solver->stateData() + solver->size()),
              beforeRejectedRestore);
    EXPECT_EQ(gds->getSimulationTime(), laterSimulationTime);
    EXPECT_EQ(solver->getSolverTime(), laterSolverTime);

    std::fill(solver->stateData(), solver->stateData() + solver->size(), -21.0);
    std::fill(solver->derivData(), solver->derivData() + solver->size(), -22.0);
    ASSERT_EQ(gds->execute("rollback " + file.quotedPath() + " minimal"),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(std::vector<double>(solver->stateData(), solver->stateData() + solver->size()),
              expectedState);
    EXPECT_EQ(std::vector<double>(solver->derivData(), solver->derivData() + solver->size()),
              expectedDerivative);
    EXPECT_EQ(gds->getSimulationTime(), captureTime);
    EXPECT_EQ(solver->getSolverTime(), captureTime);
    ASSERT_EQ(gds->run(CoreTime(0.133456789)), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_GT(gds->getSimulationTime(), captureTime);
    EXPECT_GT(solver->getSolverTime(), captureTime);

    std::fill(solver->stateData(), solver->stateData() + solver->size(), -31.0);
    std::fill(solver->derivData(), solver->derivData() + solver->size(), -32.0);
    gds->checkpointRestore(saved, CheckpointRestoreLevel::MINIMAL);
    EXPECT_EQ(std::vector<double>(solver->stateData(), solver->stateData() + solver->size()),
              expectedState);
    EXPECT_EQ(std::vector<double>(solver->derivData(), solver->derivData() + solver->size()),
              expectedDerivative);
    EXPECT_EQ(gds->getSimulationTime(), captureTime);
    EXPECT_EQ(solver->getSolverTime(), captureTime);
}

TEST_F(PowerflowSystemTests, BinaryStateCheckpointRestoreMultiMachineDae)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(GRIDDYN_TEST_DIRECTORY) + "/dyn_tests2/test_2m4bDyn.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    auto* bus = gds->getBus(2);
    ASSERT_NE(bus, nullptr);
    bus->setFlag("compute_frequency", true);

    const CoreTime captureTime(0.0);
    ASSERT_EQ(gds->dynInitialize(captureTime), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getCurrentMode();
    ASSERT_TRUE(mode.differential);
    ASSERT_TRUE(mode.algebraic);
    auto solver = gds->getSolverInterface(mode);
    ASSERT_NE(solver, nullptr);
    ASSERT_GT(solver->size(), 0U);

    saveStateBinary(gds.get(), file.path.string(), mode, false);
    const auto saved = readStateBinary(file.path.string());
    ASSERT_EQ(saved.time, captureTime);
    ASSERT_EQ(saved.state.size(), solver->size());
    ASSERT_EQ(saved.derivatives.size(), solver->size());
    const auto voltage = bus->getVoltage(saved.state.data(), mode);
    const auto angle = bus->getAngle(saved.state.data(), mode);
    const auto frequency =
        bus->getFreq(StateData(saved.time, saved.state.data(), saved.derivatives.data()), mode);

    ASSERT_EQ(gds->run(CoreTime(0.02)), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_GT(gds->getSimulationTime(), saved.time);
    bus->setVoltageAngle(voltage - 0.1, angle - 0.05);
    bus->set("frequency", frequency + 0.05);
    std::fill(solver->stateData(), solver->stateData() + solver->size(), -91.0);
    std::fill(solver->derivData(), solver->derivData() + solver->size(), -92.0);

    gds->checkpointRestore(saved, CheckpointRestoreLevel::MINIMAL, mode);
    EXPECT_EQ(std::vector<double>(solver->stateData(), solver->stateData() + solver->size()),
              saved.state);
    EXPECT_EQ(std::vector<double>(solver->derivData(), solver->derivData() + solver->size()),
              saved.derivatives);
    EXPECT_EQ(gds->getSimulationTime(), saved.time);
    EXPECT_EQ(solver->getSolverTime(), saved.time);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(bus->getAngle(), angle);
    EXPECT_DOUBLE_EQ(bus->getFreq(), frequency);

    ASSERT_EQ(gds->run(CoreTime(0.025)), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_GT(gds->getSimulationTime(), saved.time);
    EXPECT_TRUE(std::all_of(solver->stateData(),
                            solver->stateData() + solver->size(),
                            [](double value) { return std::isfinite(value); }));
}

TEST_F(PowerflowSystemTests, LocalStateCheckpointRestoresNestedModelHistory)
{
    TemporaryStateFile file;
    const auto modelFile = std::string(GRIDDYN_TEST_DIRECTORY) + "/dyn_tests2/test_2m4bDyn.xml";
    gds = readSimXMLFile(modelFile);
    ASSERT_NE(gds, nullptr);
    nameCheckpointGeneratorModels(*gds);
    disableFixtureRecorderFiles(*gds);
    ASSERT_EQ(gds->run(CoreTime(0.05)), FUNCTION_EXECUTION_SUCCESS);

    auto* bus = gds->getBus(0);
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen();
    ASSERT_NE(generator, nullptr);
    CoreObject* exciter = generator->find("exciter");
    ASSERT_NE(exciter, nullptr);

    const auto bias = [&](double change) {
        auto savedFields = ModelFieldSnapshot::capture(*exciter, FieldSnapshotLevel::LOCAL_STATE);
        auto field =
            std::ranges::find(savedFields.mFields, "operating_bias", &ModelFieldEntry::mName);
        if (field == savedFields.mFields.end()) {
            throw FileOperationError("fixture exciter lacks operating bias");
        }
        const auto original = std::get<double>(field->mValue);
        if (change != 0.0) {
            field->mValue = original + change;
            savedFields.restore(*exciter);
        }
        return original;
    };
    const auto savedBias = bias(0.0);
    constexpr std::int64_t exactClock = 9007199254740993LL;
    exciter->updateA(CoreTime(std::chrono::nanoseconds(exactClock)));
    const auto voltage = bus->getVoltage();
    const auto captured = gds->checkpointCapture();
    const auto path = file.path.string();
    saveStateBinary(gds.get(), path, captured.mode, false);
    EXPECT_THROW(gds->checkpointRestore(path, CheckpointRestoreLevel::LOCAL_STATE),
                 FileOperationError);
    gds->checkpointCapture(path, captured.mode, true);
    exportMatlabStateFixture(file.path, "local-state-checkpoint.h5");
    const auto decoded = readStateBinary(path);
    ASSERT_TRUE(decoded.localState.has_value());
    EXPECT_EQ(decoded.state, captured.state);
    EXPECT_EQ(decoded.stateNames, captured.stateNames);

    ASSERT_EQ(gds->run(CoreTime(0.1)), FUNCTION_EXECUTION_SUCCESS);
    const auto reference = gds->getState(captured.mode);
    bias(0.02);
    exciter->updateA(timeOneSecond);
    const auto changedBias = bias(0.0);
    auto incomplete = decoded;
    if (!incomplete.localState) {
        FAIL() << "checkpoint has no local-state payload";
    }
    ASSERT_FALSE(incomplete.localState->empty());
    incomplete.localState->pop_back();
    EXPECT_THROW(gds->checkpointRestore(incomplete, CheckpointRestoreLevel::LOCAL_STATE),
                 FileOperationError);
    auto badField = decoded;
    if (!badField.localState) {
        FAIL() << "checkpoint has no local-state payload";
    }
    ASSERT_FALSE(badField.localState->empty());
    auto& fields = badField.localState->back().fields.mFields;
    ASSERT_FALSE(fields.empty());
    fields.back().mValue = std::string("wrong type");
    EXPECT_THROW(gds->checkpointRestore(badField, CheckpointRestoreLevel::LOCAL_STATE),
                 FileOperationError);
    auto mismatched = decoded;
    ASSERT_FALSE(mismatched.stateNames.empty());
    mismatched.stateNames.back() += " invalid";
    EXPECT_THROW(gds->checkpointRestore(mismatched, CheckpointRestoreLevel::LOCAL_STATE),
                 FileOperationError);
    EXPECT_EQ(gds->getState(captured.mode), reference);
    EXPECT_DOUBLE_EQ(bias(0.0), changedBias);
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(0.1));

    gds->checkpointRestore(path, CheckpointRestoreLevel::LOCAL_STATE);
    EXPECT_EQ(gds->getSimulationTime(), captured.time);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(bias(0.0), savedBias);
    auto exciterFields = ModelFieldSnapshot::capture(*exciter, FieldSnapshotLevel::LOCAL_STATE);
    const auto clock =
        std::ranges::find(exciterFields.mFields, "lastUpdateTime", &ModelFieldEntry::mName);
    ASSERT_NE(clock, exciterFields.mFields.end());
    EXPECT_EQ(std::get<CoreTime>(clock->mValue).to_ns().count(), exactClock);

    ASSERT_EQ(gds->run(CoreTime(0.1)), FUNCTION_EXECUTION_SUCCESS);
    const auto replay = gds->getState(captured.mode);
    ASSERT_EQ(replay.size(), reference.size());
    for (std::size_t ii = 0; ii < reference.size(); ++ii) {
        EXPECT_NEAR(replay[ii], reference[ii], 1e-6);
    }

    bias(0.01);
    gds->checkpointRestore(captured, CheckpointRestoreLevel::LOCAL_STATE);
    EXPECT_DOUBLE_EQ(bias(0.0), savedBias);

    gds2 = readSimXMLFile(modelFile);
    ASSERT_NE(gds2, nullptr);
    nameCheckpointGeneratorModels(*gds2);
    disableFixtureRecorderFiles(*gds2);
    ASSERT_EQ(gds2->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
    gds2->checkpointRestore(path, CheckpointRestoreLevel::LOCAL_STATE);
    EXPECT_EQ(gds2->getState(captured.mode), captured.state);
    EXPECT_EQ(gds2->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    ASSERT_EQ(gds2->run(CoreTime(0.1)), FUNCTION_EXECUTION_SUCCESS);
    const auto restarted = gds2->getState(captured.mode);
    ASSERT_EQ(restarted.size(), reference.size());
    for (std::size_t ii = 0; ii < reference.size(); ++ii) {
        EXPECT_NEAR(restarted[ii], reference[ii], 1e-6);
    }
}

TEST_F(PowerflowSystemTests, FileCheckpointContinuesWithAnActiveExciterLimitAfterReload)
{
    const TemporaryStateFile file(" local checkpoint.h5");
    const auto modelFile = std::string(GRIDDYN_TEST_DIRECTORY) + "/dyn_tests2/test_2m4bDyn.xml";
    const auto prepare = [&] {
        gds = readSimXMLFile(modelFile);
        ASSERT_NE(gds, nullptr);
        nameCheckpointGeneratorModels(*gds);
        auto* generator = gds->getBus(0)->getGen();
        ASSERT_NE(generator, nullptr);
        auto* exciter = new exciters::ExciterSEXS("exciter");
        exciter->set("ta", 0.0);
        exciter->set("tb", 0.20);
        exciter->set("te", 0.15);
        exciter->set("vrmax", 3.0);
        exciter->set("vrmin", -2.0);
        generator->add(exciter);
    };
    const auto limiterFlag = [](const CoreObject& exciter, std::string_view name) {
        return std::get<bool>(exciter.getModelDescription().find(name)->value(exciter));
    };
    const CoreTime captureTime(0.65);
    const std::array sampleTimes{CoreTime(0.75), CoreTime(0.85), CoreTime(0.95)};
    const std::array runCommands{"run 0.75", "run 0.85", "run 0.95"};
    std::vector<std::vector<double>> reference;
    double steppedReference = 0.0;
    ASSERT_NO_FATAL_FAILURE(prepare());
    {
        auto* exciter = gds->getBus(0)->getGen()->find("exciter");
        ASSERT_EQ(gds->run(CoreTime(0.05)), FUNCTION_EXECUTION_SUCCESS);
        ASSERT_FALSE(limiterFlag(*exciter, "limiter_active"));
        steppedReference = exciter->get("vref") + 0.25;
        exciter->set("vref", steppedReference);
        ASSERT_EQ(gds->run(captureTime), FUNCTION_EXECUTION_SUCCESS);
        ASSERT_TRUE(limiterFlag(*exciter, "limiter_active"));
        ASSERT_TRUE(limiterFlag(*exciter, "limiter_high"));
        const auto expected = gds->checkpointCapture();
        ASSERT_EQ(gds->execute("checkpoint " + file.quotedPath() + " dae"),
                  FUNCTION_EXECUTION_SUCCESS);
        const auto written = readStateBinary(file.path.string());
        EXPECT_EQ(written.time, expected.time);
        EXPECT_EQ(written.state, expected.state);
        EXPECT_EQ(written.derivatives, expected.derivatives);
        EXPECT_EQ(written.stateNames, expected.stateNames);

        for (const auto time : sampleTimes) {
            ASSERT_EQ(gds->run(time), FUNCTION_EXECUTION_SUCCESS);
            reference.push_back(gds->getState(gds->getCurrentMode()));
        }
    }
    gds = nullptr;

    ASSERT_NO_FATAL_FAILURE(prepare());
    ASSERT_EQ(gds->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
    auto* exciter = gds->getBus(0)->getGen()->find("exciter");
    ASSERT_FALSE(limiterFlag(*exciter, "limiter_active"));
    // The file restores local history. Reapply the same reference step as configuration.
    exciter->set("vref", steppedReference);
    gds->add("rollback " + file.quotedPath());
    ASSERT_EQ(gds->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(gds->getSimulationTime(), captureTime);
    EXPECT_TRUE(limiterFlag(*exciter, "limiter_active"));
    EXPECT_TRUE(limiterFlag(*exciter, "limiter_high"));

    for (std::size_t sample = 0; sample < sampleTimes.size(); ++sample) {
        SCOPED_TRACE(sample);
        gds->add(runCommands[sample]);
        ASSERT_EQ(gds->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_EQ(gds->getSimulationTime(), sampleTimes[sample]);
        const auto restarted = gds->getState(gds->getCurrentMode());
        ASSERT_EQ(restarted.size(), reference[sample].size());
        for (std::size_t ii = 0; ii < restarted.size(); ++ii) {
            EXPECT_NEAR(restarted[ii], reference[sample][ii], 1e-5) << "state " << ii;
        }
        EXPECT_TRUE(limiterFlag(*exciter, "limiter_active"));
    }
}

TEST_F(PowerflowSystemTests, ScriptedRestoreRejectsInvalidFilesAndIncompatibleModels)
{
    const TemporaryStateFile checkpoint;
    const TemporaryStateFile malformed;
    {
        std::ofstream output(malformed.path);
        ASSERT_TRUE(output.is_open());
        output << "not a checkpoint";
    }
    const auto model = std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml";
    gds = readSimXMLFile(model);
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    ASSERT_EQ(gds->run(CoreTime(0.05)), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->execute("checkpoint " + checkpoint.quotedPath()), FUNCTION_EXECUTION_SUCCESS);

    gds2 = readSimXMLFile(model);
    ASSERT_NE(gds2, nullptr);
    disableFixtureRecorderFiles(*gds2);
    ASSERT_EQ(gds2->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const auto rejectRestore = [&](const std::string& command, std::string_view reason) {
        gds2->set("name", "waiting");
        const auto beforeState = gds2->getState(gds2->getCurrentMode());
        const auto beforeTime = gds2->getSimulationTime();
        gds2->addActions(command + "; set name resumed", "restore.xml");
        try {
            gds2->run();
            ADD_FAILURE() << "expected a checkpoint restore failure";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), ActionError::Kind::EXECUTION);
            const std::string_view diagnostic(error.what());
            EXPECT_TRUE(diagnostic.starts_with("restore.xml: action '" + command + "' failed: "))
                << diagnostic;
            EXPECT_TRUE(diagnostic.contains(reason)) << diagnostic;
        }
        EXPECT_EQ(gds2->getName(), "waiting");
        EXPECT_EQ(gds2->getSimulationTime(), beforeTime);
        EXPECT_EQ(gds2->getState(gds2->getCurrentMode()), beforeState);
        ASSERT_EQ(gds2->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_EQ(gds2->getName(), "resumed");
    };
    ASSERT_NO_FATAL_FAILURE(
        rejectRestore("load powerflow-checkpoint " + malformed.quotedPath(), "parse error"));
    ASSERT_NO_FATAL_FAILURE(rejectRestore("rollback " + malformed.quotedPath(), "HDF5"));
    ASSERT_NO_FATAL_FAILURE(
        rejectRestore("rollback " + checkpoint.quotedPath(), "no initialized solver matches"));

    ASSERT_EQ(gds2->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
    gds2->getBus(0)->setName("renamed bus");
    ASSERT_NO_FATAL_FAILURE(rejectRestore("rollback " + checkpoint.quotedPath(),
                                          "missing or duplicate checkpoint component"));
}

TEST_F(PowerflowSystemTests, LocalStateCheckpointRestoresBothPartitionedBlocks)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    gds->set("dynamicsolvermethod", "partitioned");
    ASSERT_EQ(gds->run(CoreTime(0.05)), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getSolverMode("differential");
    const auto path = file.path.string();
    auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    bus->updateA(CoreTime(0.02));
    ASSERT_EQ(gds->execute("checkpoint " + file.quotedPath() + " dyndiff"),
              FUNCTION_EXECUTION_SUCCESS);
    const auto saved = readStateBinary(path);
    ASSERT_EQ(saved.paired.size(), 1U);
    ASSERT_EQ(saved.state.size(), saved.stateNames.size());
    ASSERT_EQ(saved.paired.front().state.size(), saved.paired.front().stateNames.size());
    ASSERT_EQ(gds->run(CoreTime(0.1)), FUNCTION_EXECUTION_SUCCESS);
    bus->updateA(CoreTime(0.09));

    gds->checkpointRestore(path, CheckpointRestoreLevel::MINIMAL, mode);
    EXPECT_DOUBLE_EQ(bus->get("lastupdate"), 0.09);
    ASSERT_EQ(gds->execute("rollback " + file.quotedPath() + " local"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(bus->get("lastupdate"), 0.02);
    EXPECT_EQ(gds->getState(mode), saved.state);
    EXPECT_EQ(gds->getState(saved.paired.front().mode), saved.paired.front().state);
    EXPECT_EQ(gds->getSimulationTime(), saved.time);
}

TEST_F(PowerflowSystemTests, BinaryStatePeriodicDynamicOutput)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    gds->set("statefile", file.path.string());
    gds->set("staterecordperiod", 0.05);
    ASSERT_EQ(gds->run(CoreTime(0.15)), FUNCTION_EXECUTION_SUCCESS);
    exportMatlabStateFixture(file.path, "periodic.h5");
    const auto saved = readStateBinary(file.path.string());
    EXPECT_GE(saved.time, CoreTime(0.10));
    EXPECT_LE(saved.time, CoreTime(0.15));
    EXPECT_FALSE(saved.state.empty());
    EXPECT_EQ(saved.derivatives.size(), saved.state.size());
    utilities::Hdf5DataFile input(file.path.string(), utilities::Hdf5DataFile::OpenMode::READ_ONLY);
    EXPECT_GE(input.recordCount(), 4U);
}

TEST_F(PowerflowSystemTests, BinaryStateSolverDiagnosticOutput)
{
    TemporaryStateFile stateFile;
    TemporaryStateFile jacobianFile;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->pFlowInitialize(), FUNCTION_EXECUTION_SUCCESS);
    auto solver = gds->getSolverInterface(gds->getSolverMode("powerflow"));
    solver->set("statefile", stateFile.path.string());
    solver->set("jacfile", jacobianFile.path.string());
    solver->setFlag("filecapture", true);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    solver->setFlag("filecapture", false);
    ASSERT_TRUE(std::filesystem::exists(stateFile.path));
    ASSERT_TRUE(std::filesystem::exists(jacobianFile.path));
    EXPECT_THROW(readStateBinary(stateFile.path.string()), FileOperationError);
    EXPECT_THROW(readStateBinary(jacobianFile.path.string()), FileOperationError);
    exportMatlabStateFixture(stateFile.path, "solver-run.h5");
    exportMatlabStateFixture(jacobianFile.path, "solver-jacobian.h5");
}

TEST_F(PowerflowSystemTests, BinaryStateDynamicSolverDiagnosticOutput)
{
    TemporaryStateFile stateFile;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    ASSERT_EQ(gds->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
    auto solver = gds->getSolverInterface(gds->getCurrentMode());
    solver->set("statefile", stateFile.path.string());
    solver->setFlag("filecapture", true);
    ASSERT_EQ(gds->run(CoreTime(0.15)), FUNCTION_EXECUTION_SUCCESS);
    solver->setFlag("filecapture", false);
    ASSERT_TRUE(std::filesystem::exists(stateFile.path));
    EXPECT_THROW(readStateBinary(stateFile.path.string()), FileOperationError);
    exportMatlabStateFixture(stateFile.path, "dae-solver-run.h5");
}

TEST_F(PowerflowSystemTests, BinaryStatePartitionedSolverDiagnosticOutput)
{
    std::vector<std::string> solvers;
#ifdef GRIDDYN_ENABLE_CVODE
    solvers.emplace_back("cvode");
#endif
#ifdef GRIDDYN_ENABLE_ARKODE
    solvers.emplace_back("arkode");
#endif
    for (const auto& solverName : solvers) {
        SCOPED_TRACE(solverName);
        TemporaryStateFile stateFile;
        gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
        ASSERT_NE(gds, nullptr);
        disableFixtureRecorderFiles(*gds);
        gds->set("dynamicsolvermethod", "partitioned");
        gds->set("defdyndiff", solverName);
        ASSERT_EQ(gds->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
        auto solver = gds->getSolverInterface(gds->getSolverMode("differential"));
        solver->set("statefile", stateFile.path.string());
        solver->setFlag("filecapture", true);
        ASSERT_EQ(gds->run(CoreTime(0.05)), FUNCTION_EXECUTION_SUCCESS);
        solver->setFlag("filecapture", false);
        ASSERT_TRUE(std::filesystem::exists(stateFile.path));
        EXPECT_THROW(readStateBinary(stateFile.path.string()), FileOperationError);
        const auto fixtureName = solverName + "-solver-run.h5";
        exportMatlabStateFixture(stateFile.path, fixtureName.c_str());
    }
}

TEST_F(PowerflowSystemTests, BinaryStateRejectsTruncatedOrIncompatibleData)
{
    TemporaryStateFile file;
    TemporaryStateFile corrupt;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getSolverMode("powerflow");
    auto solver = gds->getSolverInterface(mode);
    saveStateBinary(gds.get(), file.path.string(), mode, false);
    std::ifstream input(file.path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    const std::vector<double> original(solver->stateData(), solver->stateData() + solver->size());
    for (const auto length : {std::size_t{0}, std::size_t{7}, bytes.size() / 2}) {
        {
            std::ofstream output(corrupt.path, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), static_cast<std::streamsize>(length));
        }
        EXPECT_THROW(loadStateBinary(gds.get(), corrupt.path.string(), mode), FileOperationError);
        EXPECT_TRUE(std::equal(original.begin(), original.end(), solver->stateData()));
        EXPECT_THROW(saveStateBinary(gds.get(), corrupt.path.string(), mode, true),
                     FileOperationError);
    }
    // An old custom-format header is deliberately unsupported.
    {
        std::ofstream output(corrupt.path, std::ios::binary | std::ios::trunc);
        output << "GDSTATE1";
    }
    EXPECT_THROW(readStateBinary(corrupt.path.string()), FileOperationError);
    EXPECT_THROW(loadStateBinary(gds.get(), file.path.string(), cDaeSolverMode),
                 FileOperationError);
    EXPECT_TRUE(std::equal(original.begin(), original.end(), solver->stateData()));
}

TEST_F(PowerflowSystemTests, Hdf5TypedRecordsAndChunkedMatrices)
{
    using namespace utilities;
    TemporaryStateFile file;
    const std::vector<double> values{1.0,
                                     -0.0,
                                     std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::quiet_NaN()};
    const std::vector<std::string> names{"voltage", "", "bus_\xCE\xB1", std::string(400, 'x')};
    {
        Hdf5DataFile output(file.path.string(), Hdf5DataFile::OpenMode::TRUNCATE, 1);
        Hdf5RecordView vector;
        vector.info.source = 1;
        vector.info.time_ns = 9007199254740993LL;
        vector.info.index = 4294967297ULL;
        vector.values = values;
        Hdf5RecordView labels;
        labels.info.type = Hdf5RecordType::NAMES;
        labels.names = names;
        Hdf5RecordView matrix;
        matrix.info.type = Hdf5RecordType::MATRIX;
        matrix.info.rows = 3;
        matrix.info.columns = 5;
        matrix.info.count = 9000;
        matrix.info.cj = 12.5;
        std::uint64_t next = 0;
        matrix.matrix = [&next](std::span<double> data,
                                std::span<std::uint64_t> row,
                                std::span<std::uint64_t> col) {
            for (std::size_t i = 0; i < data.size(); ++i, ++next) {
                data[i] = static_cast<double>(next);
                row[i] = next % 3;
                col[i] = next % 5;
            }
        };
        const std::array records{vector, matrix, labels};
        output.append(records);
    }
    {
        Hdf5DataFile output(file.path.string(), Hdf5DataFile::OpenMode::APPEND);
        Hdf5RecordView empty;
        empty.info.type = Hdf5RecordType::MATRIX;
        empty.info.rows = 4;
        empty.info.columns = 7;
        output.append(std::span(&empty, 1));
    }
    Hdf5DataFile input(file.path.string(), Hdf5DataFile::OpenMode::READ_ONLY);
    const auto records = input.readRecords();
    ASSERT_EQ(records.size(), 4U);
    EXPECT_EQ(records[0].time_ns, 9007199254740993LL);
    EXPECT_EQ(records[0].index, 4294967297ULL);
    const auto loaded = input.readValues(records[0]);
    ASSERT_EQ(loaded.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        EXPECT_EQ(std::bit_cast<std::uint64_t>(loaded[i]), std::bit_cast<std::uint64_t>(values[i]));
    }
    EXPECT_EQ(input.readNames(records[2]), names);
    const auto matrix = input.readMatrix(records[1]);
    ASSERT_EQ(matrix.values.size(), 9000U);
    EXPECT_EQ(matrix.values[8192], 8192);
    EXPECT_EQ(matrix.row.back(), 8999U % 3);
    EXPECT_EQ(matrix.column.back(), 8999U % 5);
    EXPECT_EQ(input.readValues(records[1], 8191, 3), (std::vector<double>{8191, 8192, 8193}));
    EXPECT_EQ(records[3].rows, 4U);
    EXPECT_EQ(records[3].columns, 7U);
    EXPECT_TRUE(input.readMatrix(records[3]).values.empty());
    EXPECT_EQ(records[0].frame, records[2].frame);
    EXPECT_NE(records[0].frame, records[3].frame);
    EXPECT_THROW(input.readValues(records[0], 3, 2), std::runtime_error);
    exportMatlabStateFixture(file.path, "typed-records.h5");
}

TEST_F(PowerflowSystemTests, Hdf5SharedOutputOwnsOneAppendPosition)
{
    using namespace utilities;
    TemporaryStateFile file;
    Hdf5DataFile first(file.path.string(), Hdf5DataFile::OpenMode::TRUNCATE);
    Hdf5RecordView record;
    const std::array<double, 2> values{4.0, 5.0};
    record.values = values;
    first.append(std::span(&record, 1));
    const auto alias = file.path.parent_path() / "." / file.path.filename();
    Hdf5DataFile second(alias.string(), Hdf5DataFile::OpenMode::APPEND);
    second.append(std::span(&record, 1));
    EXPECT_EQ(first.recordCount(), 2U);
    EXPECT_EQ(second.recordCount(), 2U);
    const auto records = first.readRecords();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_NE(records[0].frame, records[1].frame);
    EXPECT_EQ(first.readValues(records[1]), (std::vector<double>{4.0, 5.0}));
    EXPECT_THROW(Hdf5DataFile(file.path.string(), Hdf5DataFile::OpenMode::TRUNCATE),
                 std::runtime_error);
    Hdf5DataFile reader(file.path.string(), Hdf5DataFile::OpenMode::READ_ONLY);
    EXPECT_THROW(reader.append(std::span(&record, 1)), std::runtime_error);
}

TEST_F(PowerflowSystemTests, Hdf5FailedFrameIsNotPublished)
{
    using namespace utilities;
    TemporaryStateFile file;
    {
        Hdf5DataFile output(file.path.string(), Hdf5DataFile::OpenMode::TRUNCATE);
        Hdf5RecordView value;
        const std::array<double, 1> data{1.25};
        value.values = data;
        output.append(std::span(&value, 1));
        Hdf5RecordView matrix;
        matrix.info.type = Hdf5RecordType::MATRIX;
        matrix.info.rows = 1;
        matrix.info.columns = 1;
        matrix.info.count = 9000;
        unsigned chunks = 0;
        matrix.matrix = [&chunks](std::span<double> values,
                                  std::span<std::uint64_t> row,
                                  std::span<std::uint64_t> col) {
            if (++chunks == 2) {
                throw std::runtime_error("injected producer failure");
            }
            std::fill(values.begin(), values.end(), 0.0);
            std::fill(row.begin(), row.end(), 0);
            std::fill(col.begin(), col.end(), 0);
        };
        EXPECT_THROW(output.append(std::span(&matrix, 1)), std::runtime_error);
        EXPECT_EQ(output.recordCount(), 1U);
        EXPECT_THROW(output.append(std::span(&value, 1)), std::runtime_error);
    }
    Hdf5DataFile input(file.path.string(), Hdf5DataFile::OpenMode::READ_ONLY);
    ASSERT_EQ(input.recordCount(), 1U);
    EXPECT_EQ(input.readValues(input.readRecords().front()), (std::vector<double>{1.25}));
}

TEST_F(PowerflowSystemTests, Hdf5PartitionedStateRoundTrip)
{
    std::vector<std::string> solvers{"basicode"};
#ifdef GRIDDYN_ENABLE_CVODE
    solvers.emplace_back("cvode");
#endif
#ifdef GRIDDYN_ENABLE_ARKODE
    solvers.emplace_back("arkode");
#endif
    for (const auto& name : solvers) {
        SCOPED_TRACE(name);
        TemporaryStateFile file;
        gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_dynamic_example.xml");
        ASSERT_NE(gds, nullptr);
        disableFixtureRecorderFiles(*gds);
        gds->set("dynamicsolvermethod", "partitioned");
        gds->set("defdyndiff", name);
        gds->set("statefile", file.path.string());
        gds->set("staterecordperiod", 0.025);
        ASSERT_EQ(gds->run(CoreTime(0.05)), FUNCTION_EXECUTION_SUCCESS);
        auto saved = readStateBinary(file.path.string());
        ASSERT_EQ(saved.paired.size(), 1U);
        auto differential = gds->getSolverInterface(gds->getSolverMode("differential"));
        auto algebraic = gds->getSolverInterface(gds->getSolverMode("algebraic"));
        EXPECT_EQ(saved.mode.pairedOffsetIndex, algebraic->getSolverMode().offsetIndex);
        EXPECT_EQ(saved.paired.front().mode.pairedOffsetIndex,
                  differential->getSolverMode().offsetIndex);
        ASSERT_EQ(saved.state.size(), differential->size());
        ASSERT_EQ(saved.paired.front().state.size(), algebraic->size());
        const auto& diffMode = differential->getSolverMode();
        const auto& algMode = algebraic->getSolverMode();
        auto* loadBus = gds->getBus(1);
        auto* generatorBus = gds->getBus(0);
        ASSERT_NE(loadBus, nullptr);
        ASSERT_NE(generatorBus, nullptr);
        auto* generator = generatorBus->getGen();
        ASSERT_NE(generator, nullptr);
        index_t rotorAngleIndex = kNullLocation;
        generator->getAngle(emptyStateData, diffMode, &rotorAngleIndex);
        ASSERT_TRUE(isValidIndex(rotorAngleIndex, saved.state));
        const auto rotorAngle = saved.state[rotorAngleIndex];
        const auto voltage = loadBus->getVoltage(saved.paired.front().state.data(), algMode);
        const auto angle = loadBus->getAngle(saved.paired.front().state.data(), algMode);
        std::fill(differential->stateData(),
                  differential->stateData() + differential->size(),
                  -999.0);
        std::fill(algebraic->stateData(), algebraic->stateData() + algebraic->size(), -888.0);
        loadStateBinary(gds.get(), file.path.string(), differential->getSolverMode());
        EXPECT_TRUE(std::equal(saved.state.begin(), saved.state.end(), differential->stateData()));
        EXPECT_TRUE(std::equal(saved.paired.front().state.begin(),
                               saved.paired.front().state.end(),
                               algebraic->stateData()));

        ASSERT_EQ(gds->run(CoreTime(0.10)), FUNCTION_EXECUTION_SUCCESS);
        ASSERT_GT(gds->getSimulationTime(), saved.time);
        loadBus->setVoltageAngle(voltage - 0.1, angle - 0.05);
        differential->stateData()[rotorAngleIndex] = rotorAngle + 0.1;
        gds->setState(gds->getSimulationTime(),
                      differential->stateData(),
                      differential->derivData(),
                      diffMode);
        ASSERT_DOUBLE_EQ(generator->getAngle(emptyStateData, cLocalSolverMode), rotorAngle + 0.1);
        std::fill(differential->stateData(),
                  differential->stateData() + differential->size(),
                  -777.0);
        std::fill(differential->derivData(),
                  differential->derivData() + differential->size(),
                  -778.0);
        std::fill(algebraic->stateData(), algebraic->stateData() + algebraic->size(), -779.0);
        gds->checkpointRestore(saved,
                               CheckpointRestoreLevel::MINIMAL,
                               differential->getSolverMode());
        EXPECT_TRUE(std::equal(saved.state.begin(), saved.state.end(), differential->stateData()));
        EXPECT_TRUE(std::equal(saved.derivatives.begin(),
                               saved.derivatives.end(),
                               differential->derivData()));
        EXPECT_TRUE(std::equal(saved.paired.front().state.begin(),
                               saved.paired.front().state.end(),
                               algebraic->stateData()));
        EXPECT_EQ(gds->getSimulationTime(), saved.time);
        EXPECT_EQ(differential->getSolverTime(), saved.time);
        EXPECT_EQ(algebraic->getSolverTime(), saved.time);
        EXPECT_DOUBLE_EQ(loadBus->getVoltage(), voltage);
        EXPECT_DOUBLE_EQ(loadBus->getAngle(), angle);
        EXPECT_DOUBLE_EQ(generator->getAngle(emptyStateData, cLocalSolverMode), rotorAngle);

        const auto fixture = name + "-paired-state.h5";
        exportMatlabStateFixture(file.path, fixture.c_str());

        auto& paired = saved.paired.front();
        std::swap(saved.mode, paired.mode);
        std::swap(saved.state, paired.state);
        std::swap(saved.derivatives, paired.derivatives);
        loadBus->setVoltageAngle(voltage - 0.05, angle - 0.1);
        differential->stateData()[rotorAngleIndex] = rotorAngle - 0.1;
        gds->setState(saved.time, differential->stateData(), differential->derivData(), diffMode);

        gds->checkpointRestore(saved, CheckpointRestoreLevel::MINIMAL, algMode);
        EXPECT_DOUBLE_EQ(loadBus->getVoltage(), voltage);
        EXPECT_DOUBLE_EQ(loadBus->getAngle(), angle);
        EXPECT_DOUBLE_EQ(generator->getAngle(emptyStateData, cLocalSolverMode), rotorAngle);
    }
}

TEST_F(PowerflowSystemTests, Hdf5DiagnosticFailureDoesNotEscapeSolverCallback)
{
    TemporaryStateFile missingDirectory;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->pFlowInitialize(), FUNCTION_EXECUTION_SUCCESS);
    auto solver = gds->getSolverInterface(gds->getSolverMode("powerflow"));
    solver->set("statefile", (missingDirectory.path / "state.h5").string());
    solver->setFlag("filecapture", true);
    EXPECT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_FALSE(solver->getFlag("filecapture"));
}

TEST_F(PowerflowSystemTests, PFlowOffsetsRestoredAfterDynamicInitialization)
{
    const std::string fileName = std::string(pFlow_test_directory) + "iterated_test_case.xml";
    gds = readSimXMLFile(fileName);
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->pFlowInitialize(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);

    auto* bus30 = dynamic_cast<GridBus*>(gds->findByUserID("bus", 30));
    ASSERT_NE(bus30, nullptr);
    const auto originalAngleOffset = bus30->getOffsets(cPflowSolverMode).aOffset;
    ASSERT_NE(originalAngleOffset, kNullLocation);

    ASSERT_EQ(gds->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_FALSE(gds->getOffsets(cPflowSolverMode).offsetsLoaded);

    // Loading counts after the generator model is added must not leave the
    // next power flow with cleared bus offsets when its vector size is unchanged.
    gds->stateSize(cPflowSolverMode);
    ASSERT_EQ(bus30->getOffsets(cPflowSolverMode).aOffset, kNullLocation);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(gds->getOffsets(cPflowSolverMode).offsetsLoaded);
    EXPECT_EQ(bus30->getOffsets(cPflowSolverMode).aOffset, originalAngleOffset);
}

TEST_F(PowerflowSystemTests, BinaryStateCheckpointRestoreRebuildsPowerFlowOffsets)
{
    TemporaryStateFile file;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "iterated_test_case.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const auto mode = gds->getSolverMode("powerflow");
    auto* bus = gds->findByUserID("bus", 30);
    auto* bus30 = dynamic_cast<GridBus*>(bus);
    ASSERT_NE(bus30, nullptr);
    const auto voltage = bus30->getVoltage();
    const auto angle = bus30->getAngle();
    saveStateBinary(gds.get(), file.path.string(), mode, false);

    ASSERT_EQ(gds->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_FALSE(gds->getOffsets(mode).offsetsLoaded);
    bus30->setVoltageAngle(voltage - 0.1, angle - 0.05);

    gds->checkpointRestore(file.path.string(), CheckpointRestoreLevel::MINIMAL, mode);
    EXPECT_TRUE(gds->getOffsets(mode).offsetsLoaded);
    EXPECT_DOUBLE_EQ(bus30->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(bus30->getAngle(), angle);
}

/** test case for a floating bus ie a bus off a line with no load*/
TEST_F(PowerflowSystemTests, PFlowTestFloatingBus)
{
    std::string fileName = std::string(pFlow_test_directory) + "test_powerflow3m9b_float.xml";
    gds = readSimXMLFile(fileName);

    gds->pFlowInitialize();
    requireState(GridDynSimulation::GridState::INITIALIZED);

    runJacobianCheck(gds, cPflowSolverMode);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

/** test case for a single line breaker trip*/
TEST_F(PowerflowSystemTests, PflowTestSingleBreaker)
{
    std::string fileName = std::string(pFlow_test_directory) + "line_single_breaker_trip.xml";
    gds = readSimXMLFile(fileName);

    gds->run(5.0);
    EXPECT_GE(static_cast<double>(gds->getSimulationTime()), 5.0);
    // gds->timestep(2.05,noInputs,cPflowSolverMode);
    // runJacobianCheck(gds, cPflowSolverMode);
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}
static constexpr std::array<std::string_view, 9> approxModes{
    "normal",
    "simple",
    "decoupled",
    "fast_decoupled",
    "simplified_decoupled",
    "small_angle",
    "small_angle_decoupled",
    "small_angle_simplified",
    "linear",
};

TEST_F(PowerflowSystemTests, PflowTestLineModes)
{
    for (const auto& approx : approxModes) {
        SCOPED_TRACE(approx);
        gds = std::make_unique<GridDynSimulation>();
        std::string fileName = std::string(ieee_test_directory) + "ieee30_no_limit.cdf";

        loadCdf(gds.get(), fileName);

        requireState(GridDynSimulation::GridState::STARTUP);

        int count = gds->getInt("totalbuscount");
        EXPECT_EQ(count, 30);
        count = gds->getInt("totallinkcount");
        EXPECT_EQ(count, 41);
        std::vector<double> volts1;
        std::vector<double> ang1;
        std::vector<double> volts2;
        std::vector<double> ang2;

        gds->getVoltage(volts1);
        gds->getAngle(ang1);

        auto newSolver = makeSolver("kinsol");
        newSolver->setName(std::string{approx});
        try {
            newSolver->set("approx", std::string{approx});
        }
        catch (const InvalidParameterValue&) {
            ADD_FAILURE() << "unrecognized approx mode " << approx;
            continue;
        }

        gds->add(std::shared_ptr<SolverInterface>(std::move(newSolver)));
        auto smode = gds->getSolverMode(std::string{approx});
        gds->set("defpowerflow", std::string{approx});
        gds->pFlowInitialize(0.0);
        requireState(GridDynSimulation::GridState::INITIALIZED);
        int errors;
        if (approx == "small_angle_decoupled") {
            errors = runJacobianCheck(gds, smode, 0.05, false);
        } else {
            errors = runJacobianCheck(gds, smode, false);
        }
        ASSERT_EQ(errors, 0) << "Errors in " << approx << " mode";

        gds->powerflow();
        requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    }
}
