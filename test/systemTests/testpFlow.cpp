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
#include "griddyn/Load.h"
#include "griddyn/events/Event.h"
#include "griddyn/events/EventAdapters.h"
#include "griddyn/loads/RampLoad.h"
#include "griddyn/measurement/Recorder.h"
#include "griddyn/simulation/GridDynSimulationFileOps.h"
#include "griddyn/solvers/SolverInterface.h"
#include "utilities/Hdf5DataFile.h"
#include "utilities/MatrixDataSparse.hpp"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;
using gmlc::utilities::countDiffs;

static const char pFlow_test_directory[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/";

class PowerflowSystemTests: public GridDynSimulationTestFixture, public ::testing::Test {};

namespace {
class TemporaryStateFile {
  public:
    TemporaryStateFile()
    {
        static std::atomic<unsigned int> sequence{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("griddyn-state-" + std::to_string(timestamp) + "-" + std::to_string(sequence++) +
             ".h5");
    }
    ~TemporaryStateFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    std::filesystem::path path;
};

class TemporaryCheckpoint {
  public:
    TemporaryCheckpoint()
    {
        TemporaryStateFile unique;
        mDirectory = unique.path;
        mDirectory += ".checkpoint";
        if (!std::filesystem::create_directory(mDirectory)) {
            throw std::runtime_error("unable to create checkpoint test directory");
        }
        mPath = mDirectory / "checkpoint.json";
    }
    ~TemporaryCheckpoint()
    {
        std::error_code error;
        std::filesystem::remove_all(mDirectory, error);
    }
    std::filesystem::path mDirectory;
    std::filesystem::path mPath;
};

nlohmann::json readCheckpointManifest(const std::filesystem::path& path)
{
    std::ifstream input(path);
    nlohmann::json result;
    input >> result;
    return result;
}

void writeCheckpointManifest(const std::filesystem::path& path, const nlohmann::json& document)
{
    std::ofstream output(path);
    output << document.dump(2);
}

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
}  // namespace

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
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->getBus(1);
    ASSERT_NE(bus, nullptr);
    auto* load = bus->getLoad();
    ASSERT_NE(load, nullptr);
    const double voltage = bus->getVoltage();
    const double angle = bus->getAngle();
    const double power = load->get("p");
    savePowerFlowCheckpoint(gds.get(), checkpoint.mPath.string());

    load->set("p", 0.8);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_GT(std::abs(bus->getVoltage() - voltage), 1e-4);
    loadPowerFlowCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(bus->getAngle(), angle);
    EXPECT_DOUBLE_EQ(load->get("p"), power);

    auto second = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    loadPowerFlowCheckpoint(second.get(), checkpoint.mPath.string());
    EXPECT_DOUBLE_EQ(second->getBus(1)->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(second->getBus(1)->getAngle(), angle);
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointRejectsInvalidFileBeforeChangingState)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->getBus(1);
    const double voltage = bus->getVoltage();
    {
        std::ofstream output(checkpoint.mPath);
        output << R"({"format":"griddyn-checkpoint","version":99})";
    }
    EXPECT_THROW(loadPowerFlowCheckpoint(gds.get(), checkpoint.mPath.string()), FileOperationError);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), voltage);
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointInIteratedPowerFlow)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "iterated_test_case.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->eventDrivenPowerflow(10.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->findByUserID("bus", 25);
    ASSERT_NE(bus, nullptr);
    auto* gridBus = dynamic_cast<GridBus*>(bus);
    ASSERT_NE(gridBus, nullptr);
    const double voltage = gridBus->getVoltage();
    const double angle = gridBus->getAngle();
    savePowerFlowCheckpoint(gds.get(), checkpoint.mPath.string());
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    loadPowerFlowCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_DOUBLE_EQ(gridBus->getVoltage(), voltage);
    EXPECT_DOUBLE_EQ(gridBus->getAngle(), angle);
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(10.0));
}

TEST_F(PowerflowSystemTests, PowerFlowCheckpointAllowsEditedParametersWithoutSolving)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const double voltage = gds->getBus(1)->getVoltage();
    savePowerFlowCheckpoint(gds.get(), checkpoint.mPath.string());
    auto document = readCheckpointManifest(checkpoint.mPath);
    auto* load = gds->getBus(1)->getLoad();
    for (auto& object : document.at("objects")) {
        if (object.at("name") == load->getName()) {
            object["parameters"]["GridLoad"]["P"] = 0.55;
        }
    }
    writeCheckpointManifest(checkpoint.mPath, document);
    loadPowerFlowCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_DOUBLE_EQ(load->get("p"), 0.55);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getVoltage(), voltage);
}

TEST_F(PowerflowSystemTests, CheckpointRestoresClockConsumedEventsAndReplay)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_NE(gds, nullptr);
    auto* bus = gds->getBus(1);
    auto* load = bus->getLoad();
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto event = std::make_shared<Event>(CoreTime(15.0));
    ASSERT_TRUE(event->setTarget(load, "p"));
    event->setValue(0.8);
    gds->add(event);
    ASSERT_EQ(gds->eventDrivenPowerflow(10.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    const double savedPower = load->get("p");
    const double savedVoltage = bus->getVoltage();
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    const auto manifest = readCheckpointManifest(checkpoint.mPath);
    EXPECT_EQ(manifest.at("format"), "griddyn-checkpoint");
    EXPECT_EQ(manifest.at("level"), "parameters");
    EXPECT_TRUE(std::filesystem::is_regular_file(
        checkpoint.mDirectory / manifest.at("state").at("file").get<std::string>()));
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 0.8);
    const double expectedVoltage = bus->getVoltage();
    loadCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(10.0));
    EXPECT_DOUBLE_EQ(load->get("p"), savedPower);
    EXPECT_DOUBLE_EQ(bus->getVoltage(), savedVoltage);
    EXPECT_EQ(gds->getEventTime(), CoreTime(12.0));
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 0.8);
    EXPECT_NEAR(bus->getVoltage(), expectedVoltage, 1e-10);
}

TEST_F(PowerflowSystemTests, CheckpointRestoresRampingLoadHistory)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "iterated_test_case.xml");
    ASSERT_NE(gds, nullptr);
    disableFixtureRecorderFiles(*gds);
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->eventDrivenPowerflow(10.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    auto* bus = dynamic_cast<GridBus*>(gds->findByUserID("bus", 25));
    ASSERT_NE(bus, nullptr);
    const double savedPower = bus->getLoad()->get("p");
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    const double expectedPower = bus->getLoad()->get("p");
    const double expectedVoltage = bus->getVoltage();
    loadCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(10.0));
    EXPECT_DOUBLE_EQ(bus->getLoad()->get("p"), savedPower);
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(bus->getLoad()->get("p"), expectedPower, 1e-12);
    EXPECT_NEAR(bus->getVoltage(), expectedVoltage, 1e-9);
}

TEST_F(PowerflowSystemTests, CheckpointLoadsIntoFreshModelAndMovedBundle)
{
    TemporaryCheckpoint checkpoint;
    TemporaryCheckpoint moved;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->eventDrivenPowerflow(10.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    for (const auto& entry : std::filesystem::directory_iterator(checkpoint.mDirectory)) {
        std::filesystem::copy_file(entry.path(), moved.mDirectory / entry.path().filename());
    }
    auto second = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_EQ(second->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    second->getBus(1)->getLoad()->set("p", 0.7);
    ASSERT_EQ(second->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    loadCheckpoint(second.get(), moved.mPath.string());
    EXPECT_EQ(second->getSimulationTime(), CoreTime(10.0));
    EXPECT_DOUBLE_EQ(second->getBus(1)->getLoad()->get("p"), gds->getBus(1)->getLoad()->get("p"));
    EXPECT_DOUBLE_EQ(second->getBus(1)->getVoltage(), gds->getBus(1)->getVoltage());
}

TEST_F(PowerflowSystemTests, CheckpointStateAndParameterLevels)
{
    TemporaryCheckpoint state;
    TemporaryCheckpoint parameters;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto* bus = gds->getBus(0);
    const double target = bus->get("vtarget");
    saveCheckpoint(gds.get(), state.mPath.string(), CheckpointLevel::STATE);
    saveCheckpoint(gds.get(), parameters.mPath.string(), CheckpointLevel::PARAMETERS);
    bus->set("vtarget", target + 0.01);
    loadCheckpoint(gds.get(), state.mPath.string());
    EXPECT_DOUBLE_EQ(bus->get("vtarget"), target + 0.01);
    loadCheckpoint(gds.get(), parameters.mPath.string());
    EXPECT_DOUBLE_EQ(bus->get("vtarget"), target);
    auto document = readCheckpointManifest(parameters.mPath);
    for (auto& object : document.at("objects")) {
        if (object.at("name") == bus->getName()) {
            object["parameters"]["AcBus"]["vTarget"] = target + 0.02;
        }
    }
    writeCheckpointManifest(parameters.mPath, document);
    loadCheckpoint(gds.get(), parameters.mPath.string());
    EXPECT_DOUBLE_EQ(bus->get("vtarget"), target + 0.02);
}

TEST_F(PowerflowSystemTests, CheckpointRestoreCanBranchHdf5RecorderOutput)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string{GRIDDYN_TEST_DIRECTORY} + "/recorder_tests/recorder_hdf5.xml");
    ASSERT_NE(gds, nullptr);
    ASSERT_EQ(gds->getCollectors().size(), 1U);
    auto recorder = std::dynamic_pointer_cast<Recorder>(gds->getCollectors().front());
    ASSERT_NE(recorder, nullptr);
    gds->set("recorddirectory", checkpoint.mDirectory.string());
    ASSERT_EQ(gds->eventDrivenPowerflow(2.0, 1.0), FUNCTION_EXECUTION_SUCCESS);
    const double voltage = gds->getBus(1)->getVoltage();
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    gds->getBus(1)->getLoad()->set("p", 0.8);
    ASSERT_EQ(gds->eventDrivenPowerflow(4.0, 1.0), FUNCTION_EXECUTION_SUCCESS);
    recorder->flush();
    const double changedVoltage = gds->getBus(1)->getVoltage();
    ASSERT_GT(std::abs(changedVoltage - voltage), 1e-4);
    recorder->reset();
    recorder->set("file", "branch.h5");
    loadCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(2.0));
    EXPECT_TRUE(recorder->getTime().empty());
    ASSERT_EQ(gds->eventDrivenPowerflow(4.0, 1.0), FUNCTION_EXECUTION_SUCCESS);
    recorder->flush();
    const utilities::Hdf5DataFile original((checkpoint.mDirectory / "samples.h5").string(),
                                           utilities::Hdf5DataFile::OpenMode::READ_ONLY);
    const utilities::Hdf5DataFile branch((checkpoint.mDirectory / "branch.h5").string(),
                                         utilities::Hdf5DataFile::OpenMode::READ_ONLY);
    const auto originalRecords = original.readRecords();
    const auto branchRecords = branch.readRecords();
    ASSERT_EQ(originalRecords.size(), 5U);
    ASSERT_EQ(branchRecords.size(), 3U);
    EXPECT_EQ(branch.readNames(branchRecords[0]), original.readNames(originalRecords[0]));
    EXPECT_EQ(branchRecords.back().index, 0U);
    EXPECT_EQ(branchRecords.back().time_ns, 4000000000LL);
    const auto values = branch.readValues(branchRecords.back());
    ASSERT_EQ(values.size(), 2U);
    EXPECT_NEAR(values[0], voltage, 1e-10);
    EXPECT_NEAR(original.readValues(originalRecords.back())[0], changedVoltage, 1e-10);
}

TEST_F(PowerflowSystemTests, CheckpointRestoresReactiveLimitModeAndRange)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    auto* bus = gds->getBus(1);
    bus->set("type", "pv");
    bus->set("vtarget", 1.10);
    auto* generator = new Generator("pv_generator");
    generator->set("p", 0.4);
    generator->set("qmin", -2.0);
    generator->set("qmax", 2.0);
    bus->add(generator);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(bus->getType(), GridBus::BusType::PV);
    const double voltage = bus->getVoltage();
    const double reactivePower = generator->get("q");
    ASSERT_GT(std::abs(reactivePower), 0.01);
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    bus->set("qmin", 0.0);
    bus->set("qmax", 0.0);
    bus->updateFlags();
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(bus->getType(), GridBus::BusType::PQ);
    loadCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_EQ(bus->getType(), GridBus::BusType::PV);
    EXPECT_DOUBLE_EQ(generator->get("qmax"), 2.0);
    EXPECT_DOUBLE_EQ(generator->get("qmin"), -2.0);
    EXPECT_DOUBLE_EQ(generator->get("q"), reactivePower);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(bus->getType(), GridBus::BusType::PV);
    EXPECT_NEAR(bus->getVoltage(), voltage, 1e-9);
}

TEST_F(PowerflowSystemTests, CheckpointRecoversAfterFailedPowerFlow)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    const double voltage = gds->getBus(1)->getVoltage();
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    gds->setFlag("no_powerflow_error_recovery");
    gds->getBus(1)->getLoad()->setFlag("no_pqvoltage_limit");
    gds->getBus(1)->getLoad()->set("p", 5.0);
    ASSERT_NE(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    auto solver = gds->getSolverInterface(gds->getSolverMode("powerflow"));
    solver->setMaskElements({0});
    solver->setFlag("mask");
    loadCheckpoint(gds.get(), checkpoint.mPath.string());
    EXPECT_FALSE(solver->getFlag("mask"));
    EXPECT_EQ(gds->currentProcessState(), GridSimulation::GridState::POWERFLOW_COMPLETE);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getLoad()->get("p"), 0.45);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getVoltage(), voltage);
    EXPECT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
}

TEST_F(PowerflowSystemTests, CheckpointInvalidLastFieldDoesNotChangeModel)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    saveCheckpoint(gds.get(), checkpoint.mPath.string());
    auto document = readCheckpointManifest(checkpoint.mPath);
    document.at("objects").back().erase("runtime");
    writeCheckpointManifest(checkpoint.mPath, document);
    gds->getBus(1)->getLoad()->set("p", 0.7);
    ASSERT_EQ(gds->eventDrivenPowerflow(20.0, 2.0), FUNCTION_EXECUTION_SUCCESS);
    const auto before = gds->getState(gds->getSolverMode("powerflow"));
    const double voltage = gds->getBus(1)->getVoltage();
    EXPECT_THROW(loadCheckpoint(gds.get(), checkpoint.mPath.string()), FileOperationError);
    EXPECT_EQ(gds->getSimulationTime(), CoreTime(20.0));
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getLoad()->get("p"), 0.7);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getVoltage(), voltage);
    EXPECT_EQ(gds->getState(gds->getSolverMode("powerflow")), before);
}

TEST_F(PowerflowSystemTests, CheckpointCommandsAndFailedSaveKeepPreviousGeneration)
{
    TemporaryCheckpoint checkpoint;
    gds = readSimXMLFile(std::string(pFlow_test_directory) + "two_bus_example.xml");
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    GridDynAction action(GridDynAction::GdAction::CHECKPOINT);
    action.string1 = checkpoint.mPath.string();
    ASSERT_EQ(gds->execute(action), FUNCTION_EXECUTION_SUCCESS);
    const auto first = readCheckpointManifest(checkpoint.mPath);
    gds->getBus(1)->getLoad()->set("p", 0.7);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->execute("rollback last"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(gds->getBus(1)->getLoad()->get("p"), 0.45);
    ASSERT_EQ(gds->execute(action), FUNCTION_EXECUTION_SUCCESS);
    const auto original = readCheckpointManifest(checkpoint.mPath);
    EXPECT_NE(original.at("state").at("file"), first.at("state").at("file"));
    EXPECT_TRUE(std::filesystem::is_regular_file(checkpoint.mDirectory /
                                                 first.at("state").at("file").get<std::string>()));
    gds->add(std::make_shared<FunctionEventAdapter>([]() { return ChangeCode::NO_CHANGE; },
                                                    CoreTime(50.0)));
    EXPECT_THROW(saveCheckpoint(gds.get(), checkpoint.mPath.string()), FileOperationError);
    EXPECT_EQ(readCheckpointManifest(checkpoint.mPath), original);
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

    std::fill(solver->stateData(), solver->stateData() + solver->size(), -42.0);
    loadStateBinary(gds.get(), "", cEmptySolverMode);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), solver->stateData()));
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
    std::vector<std::string> solvers;
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
        std::fill(differential->stateData(),
                  differential->stateData() + differential->size(),
                  -999.0);
        std::fill(algebraic->stateData(), algebraic->stateData() + algebraic->size(), -888.0);
        loadStateBinary(gds.get(), file.path.string(), differential->getSolverMode());
        EXPECT_TRUE(std::equal(saved.state.begin(), saved.state.end(), differential->stateData()));
        EXPECT_TRUE(std::equal(saved.paired.front().state.begin(),
                               saved.paired.front().state.end(),
                               algebraic->stateData()));
        const auto fixture = name + "-paired-state.h5";
        exportMatlabStateFixture(file.path, fixture.c_str());
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
