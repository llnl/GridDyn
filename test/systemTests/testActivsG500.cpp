/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/GridBus.h"
#include "griddyn/Load.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/solvers/IdaInterface.h"
#include "griddyn/solvers/KinsolInterface.h"
#ifdef GRIDDYN_ENABLE_CVODE
#    include "griddyn/solvers/CvodeInterface.h"
#endif
#ifdef GRIDDYN_ENABLE_ARKODE
#    include "griddyn/solvers/ArkodeInterface.h"
#endif
#include "gtest/gtest.h"
#include "units/units.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;

namespace {

constexpr char ACTIVSG500_TEST_DIRECTORY[] = GRIDDYN_TEST_DIRECTORY "/texas_am/ACTIVSg500/";

class ActivsG500Tests: public GridDynSimulationTestFixture, public ::testing::Test {};

void checkCaseCounts(const GridDynSimulation* simulation)
{
    ASSERT_NE(simulation, nullptr);
    EXPECT_EQ(simulation->getInt("totalbuscount"), 500);
    EXPECT_EQ(simulation->getInt("totallinkcount"), 597);
    EXPECT_EQ(simulation->getInt("gencount"), 90);
    EXPECT_EQ(simulation->getInt("loadcount"), 221);
}

double maxAbsoluteDifference(const std::vector<double>& initial, const std::vector<double>& final)
{
    if (initial.size() != final.size()) {
        return std::numeric_limits<double>::infinity();
    }

    double maxDifference = 0.0;
    for (size_t index = 0; index < initial.size(); ++index) {
        if (!std::isfinite(final[index])) {
            return std::numeric_limits<double>::infinity();
        }
        maxDifference = (std::max)(maxDifference, std::abs(final[index] - initial[index]));
    }
    return maxDifference;
}

void expectStable(const std::vector<double>& initial,
                  const std::vector<double>& final,
                  double tolerance,
                  std::string_view label)
{
    ASSERT_EQ(initial.size(), final.size()) << label;
    const auto maxDifference = maxAbsoluteDifference(initial, final);
    EXPECT_LE(maxDifference, tolerance) << label << " max difference=" << maxDifference;
}

void expectBusOperatingPointStable(const GridDynSimulation* simulation,
                                   const std::vector<double>& initialVoltage,
                                   const std::vector<double>& initialAngle)
{
    std::vector<double> finalVoltage;
    std::vector<double> finalAngle;
    simulation->getVoltage(finalVoltage);
    simulation->getAngle(finalAngle);
    expectStable(initialVoltage, finalVoltage, 1e-4, "bus voltage");
    expectStable(initialAngle, finalAngle, 1e-4, "bus angle");
}

struct DynamicSample {
    std::vector<double> voltage;
    std::vector<double> angle;
    std::vector<double> differentialState;
    std::vector<double> algebraicState;
    double referenceFrequencyDeviation = 0.0;
};

bool allFinite(const std::vector<double>& values)
{
    return std::all_of(values.begin(), values.end(), [](double value) {
        return std::isfinite(value);
    });
}

double
    windowFrequencyPeakToPeak(const std::vector<DynamicSample>& samples, size_t begin, size_t end)
{
    double peakToPeak = 0.0;
    for (size_t first = begin; first < end; ++first) {
        for (size_t second = first + 1; second < end; ++second) {
            peakToPeak = (std::max)(peakToPeak,
                                    std::abs(samples[first].referenceFrequencyDeviation -
                                             samples[second].referenceFrequencyDeviation));
        }
    }
    return peakToPeak;
}

enum class ActivsG500DynamicSolver : std::uint8_t { IDA, CVODE, ARKODE };

void runActivsG500LoadStepCase(GridDynSimulationTestFixture& fixture,
                               std::string_view fileName,
                               ActivsG500DynamicSolver solverType)
{
    fixture.gds = readSimXMLFile(std::string(ACTIVSG500_TEST_DIRECTORY) + std::string(fileName));
    fixture.gds->consolePrintLevel = PrintLevel::NO_PRINT;
    checkCaseCounts(fixture.gds.get());

    ASSERT_EQ(fixture.gds->powerflow(), 0);
    ASSERT_EQ(fixture.gds->dynInitialize(), 0);
    ASSERT_EQ(fixture.gds->currentProcessState(),
              GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    std::optional<SolverMode> dynamicDifferentialMode;
    std::optional<SolverMode> dynamicAlgebraicMode;
    const SolverMode* differentialMode = &cDaeSolverMode;
    const SolverMode* algebraicMode = nullptr;
    if (solverType != ActivsG500DynamicSolver::IDA) {
        dynamicDifferentialMode = fixture.gds->getSolverMode("differential");
        dynamicAlgebraicMode = fixture.gds->getSolverMode("algebraic");
        differentialMode = &dynamicDifferentialMode.value();
        algebraicMode = &dynamicAlgebraicMode.value();
    }

    const auto differentialSolver = fixture.gds->getSolverInterface(*differentialMode);
    ASSERT_NE(differentialSolver, nullptr);
    if (solverType == ActivsG500DynamicSolver::IDA) {
        EXPECT_NE(dynamic_cast<solvers::IdaInterface*>(differentialSolver.get()), nullptr);
    }
#ifdef GRIDDYN_ENABLE_CVODE
    if (solverType == ActivsG500DynamicSolver::CVODE) {
        EXPECT_NE(dynamic_cast<solvers::CvodeInterface*>(differentialSolver.get()), nullptr);
    }
#endif
#ifdef GRIDDYN_ENABLE_ARKODE
    if (solverType == ActivsG500DynamicSolver::ARKODE) {
        EXPECT_NE(dynamic_cast<solvers::ArkodeInterface*>(differentialSolver.get()), nullptr);
    }
#endif

    std::shared_ptr<SolverInterface> algebraicSolver;
    if (algebraicMode != nullptr) {
        algebraicSolver = fixture.gds->getSolverInterface(*algebraicMode);
        ASSERT_NE(algebraicSolver, nullptr);
        EXPECT_NE(dynamic_cast<solvers::KinsolInterface*>(algebraicSolver.get()), nullptr);
    }

    auto* load = fixture.gds->find("BUS$4::LOAD#0");
    ASSERT_NE(load, nullptr);
    const double initialLoadMW = load->get("p", units::MW);
    ASSERT_TRUE(std::isfinite(initialLoadMW));

    std::vector<double> initialVoltage;
    std::vector<double> initialAngle;
    fixture.gds->getVoltage(initialVoltage);
    fixture.gds->getAngle(initialAngle);
    ASSERT_TRUE(allFinite(initialVoltage));
    ASSERT_TRUE(allFinite(initialAngle));

    ASSERT_EQ(fixture.gds->run(0.9), 0);
    EXPECT_NEAR(static_cast<double>(fixture.gds->getSimulationTime()), 0.9, 1e-8);
    EXPECT_NEAR(load->get("p", units::MW), initialLoadMW, 1e-8);

    std::vector<double> preEventAngle;
    fixture.gds->getAngle(preEventAngle);
    ASSERT_FALSE(preEventAngle.empty());
    ASSERT_TRUE(allFinite(preEventAngle));
    const double baselineReferenceFrequency = (preEventAngle.front() - initialAngle.front()) / 0.9;

    // The XML event is scheduled at t=1.0.  Stop at the event so the load
    // change can be checked before the transient is advanced further.
    const double firstPostEventTime = 1.0;
    ASSERT_EQ(fixture.gds->run(firstPostEventTime), 0);
    EXPECT_NEAR(static_cast<double>(fixture.gds->getSimulationTime()), 1.0, 1e-8);
    EXPECT_NEAR(load->get("p", units::MW), initialLoadMW + 10.0, 1e-8);

    std::vector<double> previousAngle;
    fixture.gds->getAngle(previousAngle);
    ASSERT_FALSE(previousAngle.empty());
    ASSERT_TRUE(allFinite(previousAngle));
    double previousReferenceAngle = previousAngle.front();
    double previousSampleTime = firstPostEventTime;

    std::vector<DynamicSample> samples;
    // A 0.5 second observation interval measures the derivative of the
    // reference angle while keeping the test independent of recorder files.
    constexpr double firstSampleTime = 1.5;
    constexpr double samplePeriod = 0.5;
    constexpr int sampleCount = 58;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const double sampleTime =
            firstSampleTime + (samplePeriod * static_cast<double>(sampleIndex));
        ASSERT_EQ(fixture.gds->run(sampleTime), 0);
        DynamicSample sample;
        fixture.gds->getVoltage(sample.voltage);
        fixture.gds->getAngle(sample.angle);
        sample.differentialState = fixture.gds->getState(*differentialMode);
        if (algebraicMode != nullptr) {
            sample.algebraicState = fixture.gds->getState(*algebraicMode);
        }
        ASSERT_TRUE(allFinite(sample.voltage));
        ASSERT_TRUE(allFinite(sample.angle));
        ASSERT_TRUE(allFinite(sample.differentialState));
        ASSERT_TRUE(allFinite(sample.algebraicState));
        sample.referenceFrequencyDeviation =
            ((sample.angle.front() - previousReferenceAngle) / (sampleTime - previousSampleTime)) -
            baselineReferenceFrequency;
        ASSERT_TRUE(std::isfinite(sample.referenceFrequencyDeviation));
        previousReferenceAngle = sample.angle.front();
        previousSampleTime = sampleTime;
        samples.push_back(std::move(sample));
    }

    ASSERT_FALSE(samples.empty());
    ASSERT_EQ(fixture.gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    EXPECT_NEAR(static_cast<double>(fixture.gds->getSimulationTime()), 30.0, 1e-8);
    EXPECT_NEAR(load->get("p", units::MW), initialLoadMW + 10.0, 1e-8);

    double maximumVoltageExcursion = 0.0;
    double maximumFrequencyDeviation = 0.0;
    for (const auto& sample : samples) {
        maximumVoltageExcursion = (std::max)(maximumVoltageExcursion,
                                             maxAbsoluteDifference(initialVoltage, sample.voltage));
        maximumFrequencyDeviation =
            (std::max)(maximumFrequencyDeviation, std::abs(sample.referenceFrequencyDeviation));
    }
    // The absolute reference angle may drift.  Stability is judged by the
    // reference-angle derivative (frequency), with voltage retained as a
    // network sanity bound.
    EXPECT_LT(maximumVoltageExcursion, 0.25);
    EXPECT_LT(maximumFrequencyDeviation, 1.0);

    // Compare frequency peak-to-peak excursions in consecutive five-second
    // windows.  A stable response may oscillate, but its frequency envelope
    // should not grow as the transient settles.
    constexpr size_t samplesPerWindow = 10;
    std::vector<double> envelopes;
    for (size_t begin = 0; begin < samples.size(); begin += samplesPerWindow) {
        const auto end = (std::min)(begin + samplesPerWindow, samples.size());
        if ((end - begin) >= 4) {
            envelopes.push_back(windowFrequencyPeakToPeak(samples, begin, end));
        }
    }
    ASSERT_GE(envelopes.size(), 2U);
    for (size_t index = 1; index < envelopes.size(); ++index) {
        EXPECT_LE(envelopes[index], (envelopes[index - 1] * 1.15) + 1e-5)
            << "transient envelope grew between windows " << (index - 1) << " and " << index;
    }
    EXPECT_LT(envelopes.back(), (envelopes.front() * 0.90) + 1e-5)
        << "transient envelope did not decrease over the 30-second run";
}

void runActivsG500DaeStabilityCase(GridDynSimulationTestFixture& fixture,
                                   std::string_view fileName,
                                   double stabilityTolerance = 1e-4)
{
    fixture.gds = readSimXMLFile(std::string(ACTIVSG500_TEST_DIRECTORY) + std::string(fileName));
    fixture.gds->consolePrintLevel = PrintLevel::NO_PRINT;
    checkCaseCounts(fixture.gds.get());

    ASSERT_EQ(fixture.gds->powerflow(), 0);
    ASSERT_EQ(fixture.gds->dynInitialize(), 0);
    ASSERT_EQ(fixture.gds->currentProcessState(),
              GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    auto daeSolver = fixture.gds->getSolverInterface(cDaeSolverMode);
    ASSERT_NE(daeSolver, nullptr);
    EXPECT_NE(dynamic_cast<solvers::IdaInterface*>(daeSolver.get()), nullptr);

    std::vector<double> initialVoltage;
    std::vector<double> initialAngle;
    fixture.gds->getVoltage(initialVoltage);
    fixture.gds->getAngle(initialAngle);
    const auto initialState = fixture.gds->getState(cDaeSolverMode);

    ASSERT_EQ(fixture.gds->run(), 0);
    ASSERT_EQ(fixture.gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> finalVoltage;
    std::vector<double> finalAngle;
    fixture.gds->getVoltage(finalVoltage);
    fixture.gds->getAngle(finalAngle);
    expectStable(initialVoltage, finalVoltage, stabilityTolerance, "IDA DAE bus voltage");
    expectStable(initialAngle, finalAngle, stabilityTolerance, "IDA DAE bus angle");
    expectStable(initialState,
                 fixture.gds->getState(cDaeSolverMode),
                 stabilityTolerance,
                 "IDA DAE state");
}

}  // namespace

TEST_F(ActivsG500Tests, PowerFlowPreservesSuppliedOperatingPoint)
{
    gds = readSimXMLFile(std::string(ACTIVSG500_TEST_DIRECTORY) + "activsg500_powerflow.xml");
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    checkCaseCounts(gds.get());

    ASSERT_EQ(gds->pFlowInitialize(), 0);
    std::vector<double> initialVoltage;
    std::vector<double> initialAngle;
    gds->getVoltage(initialVoltage);
    gds->getAngle(initialAngle);

    ASSERT_EQ(gds->powerflow(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    expectBusOperatingPointStable(gds.get(), initialVoltage, initialAngle);
}

TEST_F(ActivsG500Tests, EpcDydReaderLoadsDynamicModels)
{
    gds = readSimXMLFile(std::string(ACTIVSG500_TEST_DIRECTORY) + "activsg500_epc_dyd_reader.xml");
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    checkCaseCounts(gds.get());

    ASSERT_EQ(gds->powerflow(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    ASSERT_EQ(gds->dynInitialize(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    auto* bus = dynamic_cast<GridBus*>(gds->findByUserID("bus", 9));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    EXPECT_NE(generator->find("genmodel"), nullptr);
}

TEST_F(ActivsG500Tests, DaeIdaPreservesInitialOperatingPoint)
{
    runActivsG500DaeStabilityCase(*this, "activsg500_dae_stability.xml");
}

TEST_F(ActivsG500Tests, EpcDydDaeIdaPreservesInitialOperatingPoint)
{
    runActivsG500DaeStabilityCase(*this, "activsg500_epc_dyd_dae_stability.xml", 1e-3);
}

#ifdef GRIDDYN_ENABLE_CVODE
TEST_F(ActivsG500Tests, CvodeKinsolPartitionedPreservesInitialOperatingPoint)
{
    gds = readSimXMLFile(std::string(ACTIVSG500_TEST_DIRECTORY) +
                         "activsg500_cvode_kinsol_stability.xml");
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    checkCaseCounts(gds.get());

    ASSERT_EQ(gds->powerflow(), 0);
    ASSERT_EQ(gds->dynInitialize(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    const auto& algebraicMode = gds->getSolverMode("algebraic");
    const auto& differentialMode = gds->getSolverMode("differential");
    auto algebraicSolver = gds->getSolverInterface(algebraicMode);
    auto differentialSolver = gds->getSolverInterface(differentialMode);
    ASSERT_NE(algebraicSolver, nullptr);
    ASSERT_NE(differentialSolver, nullptr);
    EXPECT_NE(dynamic_cast<solvers::KinsolInterface*>(algebraicSolver.get()), nullptr);
    EXPECT_NE(dynamic_cast<solvers::CvodeInterface*>(differentialSolver.get()), nullptr);

    std::vector<double> initialVoltage;
    std::vector<double> initialAngle;
    gds->getVoltage(initialVoltage);
    gds->getAngle(initialAngle);
    const auto initialAlgebraicState = gds->getState(algebraicMode);
    const auto initialDifferentialState = gds->getState(differentialMode);

    ASSERT_EQ(gds->run(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    expectBusOperatingPointStable(gds.get(), initialVoltage, initialAngle);
    expectStable(initialAlgebraicState,
                 gds->getState(algebraicMode),
                 1e-4,
                 "KINSOL algebraic state");
    expectStable(initialDifferentialState,
                 gds->getState(differentialMode),
                 1e-4,
                 "CVODE differential state");
}
#endif

TEST_F(ActivsG500Tests, DaeIdaLoadStepRemainsStable)
{
    runActivsG500LoadStepCase(*this, "activsg500_dae_load_step.xml", ActivsG500DynamicSolver::IDA);
}

TEST_F(ActivsG500Tests, EpcDydDaeIdaLoadStepRemainsStable)
{
    runActivsG500LoadStepCase(*this,
                              "activsg500_epc_dyd_dae_load_step.xml",
                              ActivsG500DynamicSolver::IDA);
}

#ifdef GRIDDYN_ENABLE_CVODE
TEST_F(ActivsG500Tests, CvodeKinsolLoadStepRemainsStable)
{
    runActivsG500LoadStepCase(*this,
                              "activsg500_cvode_kinsol_load_step.xml",
                              ActivsG500DynamicSolver::CVODE);
}

TEST_F(ActivsG500Tests, CvodeKinsolLoadStep5msRemainsStable)
{
    runActivsG500LoadStepCase(*this,
                              "activsg500_cvode_kinsol_load_step_5ms.xml",
                              ActivsG500DynamicSolver::CVODE);
}
#endif

#ifdef GRIDDYN_ENABLE_ARKODE
TEST_F(ActivsG500Tests, ArkodeKinsolLoadStepRemainsStable)
{
    runActivsG500LoadStepCase(*this,
                              "activsg500_arkode_kinsol_load_step.xml",
                              ActivsG500DynamicSolver::ARKODE);
}

TEST_F(ActivsG500Tests, ArkodeKinsolLoadStep5msRemainsStable)
{
    runActivsG500LoadStepCase(*this,
                              "activsg500_arkode_kinsol_load_step_5ms.xml",
                              ActivsG500DynamicSolver::ARKODE);
}
#endif

#ifdef GRIDDYN_ENABLE_ARKODE
TEST_F(ActivsG500Tests, ArkodeKinsolPartitionedPreservesInitialOperatingPoint)
{
    gds = readSimXMLFile(std::string(ACTIVSG500_TEST_DIRECTORY) +
                         "activsg500_arkode_kinsol_stability.xml");
    gds->consolePrintLevel = PrintLevel::NO_PRINT;
    checkCaseCounts(gds.get());

    ASSERT_EQ(gds->powerflow(), 0);
    ASSERT_EQ(gds->dynInitialize(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    const auto& algebraicMode = gds->getSolverMode("algebraic");
    const auto& differentialMode = gds->getSolverMode("differential");
    auto algebraicSolver = gds->getSolverInterface(algebraicMode);
    auto differentialSolver = gds->getSolverInterface(differentialMode);
    ASSERT_NE(algebraicSolver, nullptr);
    ASSERT_NE(differentialSolver, nullptr);
    EXPECT_NE(dynamic_cast<solvers::KinsolInterface*>(algebraicSolver.get()), nullptr);
    EXPECT_NE(dynamic_cast<solvers::ArkodeInterface*>(differentialSolver.get()), nullptr);

    std::vector<double> initialVoltage;
    std::vector<double> initialAngle;
    gds->getVoltage(initialVoltage);
    gds->getAngle(initialAngle);
    const auto initialAlgebraicState = gds->getState(algebraicMode);
    const auto initialDifferentialState = gds->getState(differentialMode);

    ASSERT_EQ(gds->run(), 0);
    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    expectBusOperatingPointStable(gds.get(), initialVoltage, initialAngle);
    expectStable(initialAlgebraicState,
                 gds->getState(algebraicMode),
                 1e-4,
                 "KINSOL algebraic state");
    expectStable(initialDifferentialState,
                 gds->getState(differentialMode),
                 1e-4,
                 "ARKODE differential state");
}
#endif
