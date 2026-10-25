/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/Exciter.h"
#include "griddyn/Governor.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModelClassical.h"
#include "griddyn/measurement/Collector.h"
#include "griddyn/measurement/GrabberSet.h"
#include "griddyn/measurement/GridGrabbers.h"
#include "griddyn/measurement/Recorder.h"
#include "griddyn/measurement/StateGrabber.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/stabilizers/StabilizerStab3.h"
#include "utilities/MatrixDataSparse.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace griddyn;

std::shared_ptr<GridGrabber> scalarGrabber(double value)
{
    auto grabber = std::make_shared<CustomGrabber>();
    grabber->setGrabberFunction("sample", [value](CoreObject*) { return value; });
    return grabber;
}

std::shared_ptr<GridGrabber> vectorGrabber(std::vector<double> values)
{
    auto grabber = std::make_shared<CustomGrabber>();
    grabber->setDescription("samples");
    grabber->setGrabberFunction(
        [values = std::move(values)](CoreObject*, std::vector<double>& output) {
            output = values;
        });
    return grabber;
}

class MeasurementFiles: public ::testing::Test {
  protected:
    std::filesystem::path directory;
    void SetUp() override
    {
        const auto instance = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
            ("griddyn_measurement_tests_" + std::to_string(instance));
        std::filesystem::create_directories(directory);
    }
    void TearDown() override
    {
        // Remove only files created by these tests, leaving unrelated files alone.
        for (const auto& name : {"samples.dat", "samples.csv", "snapshot.dat"}) {
            std::filesystem::remove(directory / name);
        }
        if (std::filesystem::is_empty(directory)) {
            std::filesystem::remove(directory);
        }
    }
};

struct ExpressionCase {
    const char* expression;
    double expected;
};

class GrabberExpressions: public ::testing::TestWithParam<ExpressionCase> {};

TEST_P(GrabberExpressions, EvaluatesArithmetic)
{
    AcBus bus("source");
    bus.set("voltage", 2.0);
    bus.set("angle", 0.5);
    const auto& expression = GetParam();
    SCOPED_TRACE(expression.expression);
    auto grabbers = makeGrabbers(expression.expression, &bus);
    ASSERT_EQ(grabbers.size(), 1U);
    ASSERT_TRUE(grabbers.front()->loaded);
    EXPECT_NEAR(grabbers.front()->grabData(), expression.expected, 1e-10);
}

TEST_P(GrabberExpressions, StateGrabbersUseTheSameArithmetic)
{
    AcBus bus("source");
    bus.set("voltage", 2.0);
    bus.set("angle", 0.5);
    const auto& expression = GetParam();
    SCOPED_TRACE(expression.expression);
    auto grabbers = makeStateGrabbers(expression.expression, &bus);
    ASSERT_EQ(grabbers.size(), 1U);
    EXPECT_NEAR(grabbers.front()->grabData(emptyStateData, cLocalSolverMode),
                expression.expected,
                1e-10);
}

INSTANTIATE_TEST_SUITE_P(
    Arithmetic,
    GrabberExpressions,
    ::testing::Values(ExpressionCase{"voltage+2*3", 8.0},
                      ExpressionCase{"voltage-0.5-0.25", 1.25},
                      ExpressionCase{"voltage/2/2", 0.5},
                      ExpressionCase{"2*(voltage+1)", 6.0},
                      ExpressionCase{"voltage*2*3", 12.0},
                      ExpressionCase{"(voltage+1)*2", 6.0},
                      ExpressionCase{"10-(voltage-1)", 9.0},
                      ExpressionCase{"-(voltage+1)", -3.0},
                      ExpressionCase{"1e-3*voltage", 0.002},
                      ExpressionCase{"sin(angle)+cos(angle)", std::sin(0.5) + std::cos(0.5)},
                      ExpressionCase{"((voltage))", 2.0},
                      ExpressionCase{"2^3^2", 512.0},
                      ExpressionCase{"-voltage^2", -4.0},
                      ExpressionCase{"voltage^-2", 0.25},
                      ExpressionCase{"hypot(voltage,angle)", std::hypot(2.0, 0.5)},
                      ExpressionCase{"pow(2,voltage)", 4.0},
                      ExpressionCase{"3.5", 3.5},
                      ExpressionCase{"voltage%1.5", 0.5},
                      ExpressionCase{"source?voltage%2", 1.0},
                      ExpressionCase{" voltage + 1 as shifted ", 3.0}));

TEST(GrabberTests, UnitsDescriptionsAndRetargeting)
{
    AcBus first("first");
    AcBus second("second");
    first.set("angle", 0.5);
    second.set("angle", 1.0);
    auto grabbers = makeGrabbers("angle(deg) as phase", &first);
    ASSERT_EQ(grabbers.size(), 1U);
    auto& grabber = grabbers.front();
    EXPECT_NEAR(grabber->grabData(), 0.5 * 180.0 / kPI, 1e-6);
    auto copy = grabber->clone();
    copy->updateObject(&second);
    EXPECT_EQ(copy->getDesc(), "phase");
    EXPECT_EQ(grabber->getObject(), &first);
    EXPECT_NEAR(copy->grabData(), 180.0 / kPI, 1e-6);
}

TEST(GrabberTests, SwitchingFieldsInvalidatesTheOldFunction)
{
    GridDynSimulation simulation;
    auto grabber = createGrabber("voltage", &simulation);
    ASSERT_NE(grabber, nullptr);
    ASSERT_TRUE(grabber->vectorGrab);
    grabber->updateField("basepower");
    EXPECT_FALSE(grabber->vectorGrab);
    EXPECT_DOUBLE_EQ(grabber->grabData(), simulation.get("basepower"));
    grabber->updateField("no_such_measurement");
    EXPECT_FALSE(grabber->loaded);
    EXPECT_DOUBLE_EQ(grabber->grabData(), kNullVal);
}

TEST(GrabberTests, VectorScalingAndDescriptions)
{
    auto grabber = vectorGrabber({1.0, 2.0, 3.0});
    grabber->gain = 2.0;
    grabber->bias = -1.0;
    std::vector<double> values;
    grabber->grabVectorData(values);
    EXPECT_EQ(values, (std::vector<double>{1.0, 3.0, 5.0}));
    std::vector<std::string> descriptions;
    grabber->getDesc(descriptions);
    EXPECT_EQ(descriptions, (std::vector<std::string>{"samples[0]", "samples[1]", "samples[2]"}));
}

TEST(GrabberTests, VectorFunctionsReductionsAndBroadcasting)
{
    auto values = vectorGrabber({1.0, 4.0, 9.0});
    FunctionGrabber root(values, "sqrt");
    root.gain = 2.0;
    root.bias = 1.0;
    std::vector<double> result;
    root.grabVectorData(result);
    EXPECT_EQ(result, (std::vector<double>{3.0, 5.0, 7.0}));
    FunctionGrabber total(values, "sum");
    EXPECT_DOUBLE_EQ(total.grabData(), 14.0);
    OpGrabber shifted(values, scalarGrabber(2.0), "+");
    ASSERT_TRUE(shifted.vectorGrab);
    shifted.grabVectorData(result);
    EXPECT_EQ(result, (std::vector<double>{3.0, 6.0, 11.0}));
    OpGrabber reversed(scalarGrabber(12.0), values, "-");
    ASSERT_TRUE(reversed.vectorGrab);
    reversed.grabVectorData(result);
    EXPECT_EQ(result, (std::vector<double>{11.0, 8.0, 3.0}));
    OpGrabber product(values, vectorGrabber({2.0, 3.0, 4.0}), "vecprod");
    EXPECT_DOUBLE_EQ(product.grabData(), 50.0);
}

TEST(GrabberTests, VectorOperationsRejectDifferentLengths)
{
    OpGrabber operation(vectorGrabber({1.0, 2.0}), vectorGrabber({3.0}), "+");
    std::vector<double> values;
    EXPECT_THROW(operation.grabVectorData(values), InvalidParameterValue);
}

TEST(GrabberTests, CustomVectorCallbacksStartWithAnEmptyOutput)
{
    CustomGrabber grabber;
    grabber.setGrabberFunction(
        [](CoreObject*, std::vector<double>& values) { values.push_back(1.0); });
    std::vector<double> values{9.0};
    grabber.grabVectorData(values);
    grabber.grabVectorData(values);
    EXPECT_EQ(values, (std::vector<double>{1.0}));
    OpGrabber incomplete(nullptr, vectorGrabber({1.0}), "+");
    std::vector<std::string> descriptions{"stale"};
    incomplete.getDesc(descriptions);
    EXPECT_TRUE(descriptions.empty());
}

TEST(GrabberTests, InvalidOperationsAreSafeToRead)
{
    FunctionGrabber function(scalarGrabber(2.0), "no_such_function");
    EXPECT_FALSE(function.loaded);
    EXPECT_DOUBLE_EQ(function.grabData(), kNullVal);
    OpGrabber operation(scalarGrabber(2.0), scalarGrabber(3.0), "no_such_operation");
    EXPECT_FALSE(operation.loaded);
    EXPECT_DOUBLE_EQ(operation.grabData(), kNullVal);
}

TEST(CollectorTests, FrequencyAndResolutionUseReciprocalSeconds)
{
    Collector collector;
    collector.set("frequency", 4.0);
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), 0.25);
    collector.set("period_resolution", 0.1);
    collector.trigger(0.25);
    EXPECT_NEAR(collector.nextTriggerTime(), 0.55, 1e-12);
}

TEST(CollectorTests, InvalidPeriodsAreRejected)
{
    EXPECT_THROW(Collector(0.0, 0.0), InvalidParameterValue);
    Collector collector;
    for (double value : {0.0,
                         -1.0,
                         std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_THROW(collector.set("period", value), InvalidParameterValue);
        EXPECT_THROW(collector.set("frequency", value), InvalidParameterValue);
        EXPECT_THROW(collector.set("period_resolution", value), InvalidParameterValue);
    }
}

TEST(CollectorTests, ExplicitColumnsAndVectorsPreserveGaps)
{
    Collector collector;
    collector.add(vectorGrabber({1.0, 2.0}), 2);
    collector.add(scalarGrabber(7.0));
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.getValue(0), 0.0);
    EXPECT_DOUBLE_EQ(collector.getValue(2), 1.0);
    EXPECT_DOUBLE_EQ(collector.getValue(3), 2.0);
    EXPECT_DOUBLE_EQ(collector.getValue(4), 7.0);
    EXPECT_EQ(collector.getColumnDescriptions().size(), 5U);
    std::array<double, 4> output{-1.0, -1.0, -1.0, -1.0};
    EXPECT_EQ(collector.grabData(output.data(), 3), 3U);
    EXPECT_EQ(output, (std::array<double, 4>{-1.0, -1.0, 1.0, -1.0}));
}

TEST(CollectorTests, ResetReusesColumnsAndCloneKeepsLayout)
{
    Collector collector;
    collector.add(scalarGrabber(2.0), 3);
    collector.trigger(0.0);
    auto copy = collector.clone();
    copy->trigger(1.0);
    EXPECT_DOUBLE_EQ(copy->getValue(3), 2.0);
    EXPECT_EQ(copy->getColumnDescriptions().size(), 4U);
    collector.reset();
    collector.add(scalarGrabber(5.0));
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.getValue(0), 5.0);
    EXPECT_DOUBLE_EQ(collector.getValue(1), kNullVal);
}

TEST(CollectorTests, GrabberInfoTransformsEveryExpandedField)
{
    GridDynSimulation simulation;
    auto* bus = new AcBus("bus");
    simulation.add(bus);
    bus->set("voltage", 2.0);
    griddyn::GridGrabberInfo info;
    info.field = "auto";
    info.gain = 3.0;
    info.bias = 1.0;
    Collector collector;
    collector.add(info, bus);
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.getValue(0), 7.0);
    EXPECT_DOUBLE_EQ(collector.getValue(1), 1.0);
}

TEST_F(MeasurementFiles, RepeatedSavesDoNotDuplicateSamples)
{
    Recorder recorder;
    recorder.set("file", (directory / "samples.dat").string());
    recorder.add(scalarGrabber(4.0));
    recorder.trigger(0.0);
    recorder.flush();
    recorder.trigger(1.0);
    recorder.flush();
    recorder.flush();
    gmlc::utilities::TimeSeriesMulti<> saved((directory / "samples.dat").string());
    ASSERT_EQ(saved.size(), 2U);
    EXPECT_EQ(saved.time(), (std::vector<double>{0.0, 1.0}));
}

TEST_F(MeasurementFiles, AutosaveChunksAndExplicitSnapshots)
{
    Recorder recorder;
    recorder.set("file", (directory / "samples.csv").string());
    recorder.set("autosave", 2.0);
    recorder.add(scalarGrabber(4.0));
    recorder.trigger(0.0);
    recorder.trigger(1.0);
    EXPECT_TRUE(recorder.getTime().empty());
    recorder.trigger(2.0);
    recorder.saveFile((directory / "snapshot.dat").string());
    recorder.flush();
    gmlc::utilities::TimeSeriesMulti<> snapshot((directory / "snapshot.dat").string());
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_DOUBLE_EQ(snapshot.time().front(), 2.0);
    gmlc::utilities::TimeSeriesMulti<> saved;
    saved.loadTextFile((directory / "samples.csv").string());
    ASSERT_EQ(saved.size(), 3U);
    EXPECT_DOUBLE_EQ(saved.data(0, 2), 4.0);
}

TEST_F(MeasurementFiles, ResetStartsANewRecordingAndKeepsSignals)
{
    Recorder recorder;
    recorder.set("file", (directory / "samples.dat").string());
    recorder.add(scalarGrabber(3.0));
    recorder.trigger(0.0);
    recorder.flush();
    recorder.reset();
    EXPECT_TRUE(recorder.getTime().empty());
    EXPECT_EQ(recorder.numberOfPoints(), 1U);
    recorder.trigger(0.0);
    recorder.flush();
    gmlc::utilities::TimeSeriesMulti<> saved((directory / "samples.dat").string());
    ASSERT_EQ(saved.size(), 1U);
    EXPECT_DOUBLE_EQ(saved.time().front(), 0.0);
}

TEST(RecorderOperations, CloneKeepsSignalsWithoutCopyingSamples)
{
    Recorder recorder;
    recorder.add(scalarGrabber(3.0));
    recorder.trigger(0.0);
    auto collectorCopy = recorder.clone();
    auto* copy = dynamic_cast<Recorder*>(collectorCopy.get());
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->getTime().empty());
    copy->trigger(1.0);
    ASSERT_EQ(copy->getData(0).size(), 1U);
    EXPECT_DOUBLE_EQ(copy->getData(0).front(), 3.0);
    EXPECT_EQ(recorder.getTime().size(), 1U);
}

TEST(RecorderOperations, CloneToReplacesAnExistingConfigurationAndBuffer)
{
    Recorder source;
    source.setDescription("selected signal");
    source.add(scalarGrabber(3.0));
    source.trigger(0.0);
    Recorder destination;
    destination.add(scalarGrabber(8.0), 2);
    destination.trigger(0.0);
    source.cloneTo(&destination);
    EXPECT_TRUE(destination.getTime().empty());
    EXPECT_EQ(destination.getDescription(), "selected signal");
    destination.trigger(1.0);
    ASSERT_EQ(destination.getTimeSeries().columns(), 1);
    EXPECT_DOUBLE_EQ(destination.getData(0).front(), 3.0);
    destination.cloneTo(&destination);
    EXPECT_EQ(destination.getTime().size(), 1U);
}

TEST_F(MeasurementFiles, ResetWithoutNewSamplesDoesNotTruncateTheSavedFile)
{
    {
        Recorder recorder;
        recorder.set("file", (directory / "samples.dat").string());
        recorder.add(scalarGrabber(3.0));
        recorder.trigger(0.0);
        recorder.flush();
        recorder.reset();
    }
    gmlc::utilities::TimeSeriesMulti<> saved((directory / "samples.dat").string());
    ASSERT_EQ(saved.size(), 1U);
    EXPECT_DOUBLE_EQ(saved.data(0, 0), 3.0);
}

TEST(GrabberTests, InvalidExpressionsDoNotProduceLoadedGrabbers)
{
    AcBus bus;
    for (const auto* expression : {"voltage+",
                                   "(voltage",
                                   "voltage)",
                                   "sin()",
                                   "sin(voltage,angle,1)",
                                   "no_such(voltage)",
                                   "abs(voltage)trailing",
                                   "sin(voltage,)",
                                   "sin(,voltage)",
                                   "hypot(voltage,,angle)",
                                   "hypot(voltage,)",
                                   "voltage**2"}) {
        SCOPED_TRACE(expression);
        auto grabbers = makeGrabbers(expression, &bus);
        EXPECT_TRUE(grabbers.empty() || !grabbers.front()->loaded);
        EXPECT_TRUE(makeStateGrabbers(expression, &bus).empty());
    }
}

TEST(GrabberTests, AreaVectorsConvertUnitsAndExpandPerObjectFields)
{
    GridDynSimulation simulation;
    auto* first = new AcBus("first");
    auto* second = new AcBus("second");
    simulation.add(first);
    simulation.add(second);
    first->set("voltage", 1.1);
    second->set("voltage", 0.9);
    first->set("angle", 0.5);
    second->set("angle", -0.5);
    auto grabbers = makeGrabbers("angle(deg) as phases", &simulation);
    ASSERT_EQ(grabbers.size(), 1U);
    std::vector<double> values;
    grabbers.front()->grabVectorData(values);
    ASSERT_EQ(values.size(), 2U);
    EXPECT_NEAR(values[0], 0.5 * 180.0 / kPI, 1e-6);
    EXPECT_NEAR(values[1], -0.5 * 180.0 / kPI, 1e-6);
    std::vector<std::string> descriptions;
    grabbers.front()->getDesc(descriptions);
    EXPECT_EQ(descriptions, (std::vector<std::string>{"phases[0]", "phases[1]"}));
    auto expanded = makeGrabbers("per_bus[voltage,angle]", &simulation);
    ASSERT_EQ(expanded.size(), 4U);
    EXPECT_DOUBLE_EQ(expanded[0]->grabData(), 1.1);
    EXPECT_DOUBLE_EQ(expanded[1]->grabData(), 0.5);
    EXPECT_DOUBLE_EQ(expanded[2]->grabData(), 0.9);
    EXPECT_DOUBLE_EQ(expanded[3]->grabData(), -0.5);
}

TEST(GrabberTests, FrequencyConversionsUseTheConfiguredSystemBase)
{
    for (const double frequency : {50.0, 60.0}) {
        GridDynSimulation simulation;
        auto* bus = new AcBus("bus");
        simulation.add(bus);
        simulation.set("basefrequency", frequency);
        bus->set("freq", 1.02);
        auto objectGrabbers = makeGrabbers("freq(Hz)", bus);
        auto stateGrabbers = makeStateGrabbers("freq(Hz)", bus);
        auto vectorGrabbers = makeGrabbers("freq(Hz)", &simulation);
        ASSERT_EQ(objectGrabbers.size(), 1U);
        ASSERT_EQ(stateGrabbers.size(), 1U);
        ASSERT_EQ(vectorGrabbers.size(), 1U);
        EXPECT_NEAR(objectGrabbers[0]->grabData(), 1.02 * frequency, 1e-6);
        EXPECT_NEAR(stateGrabbers[0]->grabData(emptyStateData, cLocalSolverMode),
                    1.02 * frequency,
                    1e-6);
        std::vector<double> values;
        vectorGrabbers[0]->grabVectorData(values);
        ASSERT_EQ(values.size(), 1U);
        EXPECT_NEAR(values.front(), 1.02 * frequency, 1e-6);
    }
}

TEST(CollectorTests, RetargetingUpdatesDefaultColumnDescriptions)
{
    AcBus first("first");
    AcBus second("second");
    Collector collector;
    collector.add("voltage", &first);
    EXPECT_EQ(collector.getColumnDescriptions(), (std::vector<std::string>{"first:voltage"}));
    collector.updateObject(&second);
    EXPECT_EQ(collector.getColumnDescriptions(), (std::vector<std::string>{"second:voltage"}));
}

TEST(GrabberTests, OffsetZeroAndRetargetedOffsetsReadTheSelectedObject)
{
    genmodels::GenModelClassical first("first");
    genmodels::GenModelClassical second("second");
    IOdata fieldSet(4, 0.0);
    first.dynInitializeA(0.0, 0);
    second.dynInitializeA(0.0, 0);
    first.dynInitializeB({1.0, 0.0, 0.0, 0.0}, {0.8, 0.2}, fieldSet);
    second.dynInitializeB({1.0, 0.1, 0.0, 0.0}, {0.3, 0.1}, fieldSet);
    griddyn::GridGrabberInfo info;
    info.offset = 0;
    info.gain = 2.0;
    info.bias = 1.0;
    Collector collector;
    collector.add(info, &first);
    collector.trigger(0.0);
    EXPECT_NEAR(collector.getValue(0), (2.0 * first.getState(0)) + 1.0, 1e-12);
    auto grabber = createGrabber(0, &first);
    ASSERT_NE(grabber, nullptr);
    ASSERT_TRUE(grabber->loaded);
    auto copy = grabber->clone();
    copy->updateObject(&second);
    EXPECT_EQ(copy->getObject(), &second);
    EXPECT_DOUBLE_EQ(copy->grabData(), second.getState(0));
    StateGrabber state(0, &first);
    ASSERT_TRUE(state.loaded);
    EXPECT_DOUBLE_EQ(state.grabData(emptyStateData, cLocalSolverMode), first.getState(0));
    auto stateCopy = state.clone();
    stateCopy->updateObject(&second);
    EXPECT_DOUBLE_EQ(stateCopy->grabData(emptyStateData, cLocalSolverMode), second.getState(0));
    StateFunctionGrabber function(std::make_shared<StateGrabber>("id", &first), "sin");
    EXPECT_EQ(function.getJacobianMode(), JacobianMode::COMPUTED);
}

TEST(GrabberTests, GeneratorStateSelectorsResolveBeforeInitialization)
{
    DynamicGenerator generator("generator");
    auto* model = new genmodels::GenModelClassical("machine");
    generator.add(model);
    auto selected = makeGrabbers("all_model", &generator);
    ASSERT_EQ(selected.size(), 1U);
    EXPECT_TRUE(makeGrabbers("all_gov", &generator).empty());
    EXPECT_TRUE(makeGrabbers("all_ext", &generator).empty());
    EXPECT_TRUE(makeGrabbers("all_pss", &generator).empty());
    model->dynInitializeA(0.0, 0);
    IOdata fieldSet(4, 0.0);
    model->dynInitializeB({1.0, 0.0, 0.0, 0.0}, {0.8, 0.2}, fieldSet);
    const auto allStates = makeGrabbers("all_state", &generator);
    ASSERT_EQ(allStates.size(), 1U);
    std::vector<double> values;
    std::vector<double> allValues;
    selected.front()->grabVectorData(values);
    allStates.front()->grabVectorData(allValues);
    ASSERT_EQ(values.size(), 4U);
    EXPECT_EQ(values, allValues);
    for (size_t index = 0; index < values.size(); ++index) {
        EXPECT_DOUBLE_EQ(values[index], model->getState(static_cast<index_t>(index)));
    }
    std::vector<std::string> descriptions;
    selected.front()->getDesc(descriptions);
    ASSERT_EQ(descriptions.size(), 4U);
    EXPECT_EQ(descriptions.front(), "machine:id");
}

TEST(GrabberTests, ControllerStateSelectorsIncludeBlocksCreatedDuringInitialization)
{
    DynamicGenerator generator("generator");
    auto* governor = new Governor("governor");
    auto* exciter = new Exciter("exciter");
    auto* stabilizer = new stabilizers::StabilizerStab3("stabilizer");
    generator.add(governor);
    generator.add(exciter);
    generator.add(stabilizer);
    Collector collector;
    collector.add("all_gov;all_ext;all_pss", &generator);
    governor->dynInitializeA(0.0, 0);
    exciter->dynInitializeA(0.0, 0);
    stabilizer->dynInitializeA(0.0, 0);
    IOdata fieldSet(4, 0.0);
    governor->dynInitializeB({1.0, 0.8}, {0.8}, fieldSet);
    fieldSet.resize(4, 0.0);
    exciter->dynInitializeB({1.0}, {1.0}, fieldSet);
    IOdata stabilizerInputs(pssInputCount, 0.0);
    stabilizerInputs[pssOmegaInLocation] = 1.0;
    stabilizerInputs[pssElectricalPowerInLocation] = 0.8;
    stabilizer->dynInitializeB(stabilizerInputs, {}, fieldSet);
    EXPECT_FALSE(collector.getColumnDescriptions().empty());
    collector.trigger(0.0);
    EXPECT_FALSE(collector.getColumnDescriptions().empty());
    for (const auto* selection : {"all_gov", "all_ext", "all_pss"}) {
        auto grabbers = makeGrabbers(selection, &generator);
        ASSERT_EQ(grabbers.size(), 1U);
        std::vector<double> values;
        grabbers.front()->grabVectorData(values);
        ASSERT_FALSE(values.empty());
        for (const auto value : values) {
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_NE(value, kNullVal);
        }
    }
}

TEST(StateGrabberTests, UnitConversionAndFieldUpdatesKeepJacobianMetadataAccurate)
{
    AcBus bus("bus");
    bus.set("angle", 0.5);
    auto grabbers = makeStateGrabbers("angle(deg)", &bus);
    ASSERT_EQ(grabbers.size(), 1U);
    EXPECT_NEAR(grabbers[0]->grabData(emptyStateData, cLocalSolverMode), 0.5 * 180.0 / kPI, 1e-6);
    EXPECT_NE(grabbers[0]->getJacobianMode(), JacobianMode::NONE);
    grabbers[0]->updateField("connected");
    EXPECT_EQ(grabbers[0]->getJacobianMode(), JacobianMode::NONE);
    EXPECT_DOUBLE_EQ(grabbers[0]->grabData(emptyStateData, cLocalSolverMode), 1.0);
    grabbers[0]->updateField("no_such_measurement");
    EXPECT_FALSE(grabbers[0]->loaded);
    EXPECT_DOUBLE_EQ(grabbers[0]->grabData(emptyStateData, cLocalSolverMode), kNullVal);
}

TEST(StateGrabberTests, CompositeExpressionsReportEveryTargetObject)
{
    AcBus first("first");
    AcBus second("second");
    auto difference =
        std::make_shared<StateOpGrabber>(std::make_shared<StateGrabber>("voltage", &first),
                                         std::make_shared<StateGrabber>("voltage", &second),
                                         "-");
    StateFunctionGrabber magnitude(difference, "abs");
    std::vector<CoreObject*> objects;
    magnitude.getObjects(objects);
    EXPECT_EQ(objects, (std::vector<CoreObject*>{&first, &second}));
}

TEST(StateGrabberTests, ChainRuleMatchesAnIndependentFiniteDifference)
{
    auto first = std::make_shared<CustomStateGrabber>();
    auto second = std::make_shared<CustomStateGrabber>();
    first->setGrabberFunction(
        [](GridComponent*, const StateData& data, const SolverMode&) { return data.state[0]; });
    second->setGrabberFunction(
        [](GridComponent*, const StateData& data, const SolverMode&) { return data.state[1]; });
    first->setGrabberJacFunction(
        [](GridComponent*, const StateData&, MatrixData<double>& matrix, const SolverMode&) {
            matrix.assign(0, 0, 1.0);
        });
    second->setGrabberJacFunction(
        [](GridComponent*, const StateData&, MatrixData<double>& matrix, const SolverMode&) {
            matrix.assign(0, 1, 1.0);
        });
    auto product = std::make_shared<StateOpGrabber>(first, second, "*");
    StateFunctionGrabber function(product, "sin");
    function.gain = 3.0;
    function.bias = 2.0;
    std::array<double, 2> values{0.4, 0.7};
    const StateData data(0.0, values.data());
    MatrixDataSparse<double> jacobian;
    function.outputPartialDerivatives(data, jacobian, cDaeSolverMode);
    const auto evaluate = [&function, &data]() { return function.grabData(data, cDaeSolverMode); };
    EXPECT_NEAR(evaluate(), (3.0 * std::sin(0.4 * 0.7)) + 2.0, 1e-12);
    constexpr double step = 1e-5;
    for (size_t column = 0; column < values.size(); ++column) {
        const double original = values[column];
        values[column] = original + step;
        const double high = evaluate();
        values[column] = original - step;
        const double low = evaluate();
        values[column] = original;
        EXPECT_NEAR(jacobian.at(0, static_cast<index_t>(column)),
                    (high - low) / (2.0 * step),
                    1e-6);
    }
    auto constant = std::make_shared<StateGrabber>("constant", nullptr);
    constant->bias = 2.0;
    StateOpGrabber power(first, constant, "^");
    EXPECT_NE(power.getJacobianMode(), JacobianMode::NONE);
    MatrixDataSparse<double> powerJacobian;
    power.outputPartialDerivatives(data, powerJacobian, cDaeSolverMode);
    EXPECT_NEAR(powerJacobian.at(0, 0), 0.8, 1e-6);
}

TEST(StateGrabberTests, UnitAndGainScalesAreIncludedInTheJacobian)
{
    AcBus bus;
    CustomStateGrabber grabber(&bus);
    grabber.setGrabberFunction(
        [](GridComponent*, const StateData& data, const SolverMode&) { return data.state[0]; });
    grabber.setGrabberJacFunction(
        [](GridComponent*, const StateData&, MatrixData<double>& matrix, const SolverMode&) {
            matrix.assign(0, 0, 1.0);
        });
    grabber.inputUnits = units::rad;
    grabber.outputUnits = units::deg;
    grabber.gain = 3.0;
    grabber.bias = 4.0;
    std::array<double, 1> value{0.5};
    const StateData data(0.0, value.data());
    MatrixDataSparse<double> jacobian;
    grabber.outputPartialDerivatives(data, jacobian, cDaeSolverMode);
    value[0] += 1e-5;
    const double high = grabber.grabData(data, cDaeSolverMode);
    value[0] -= 2e-5;
    const double low = grabber.grabData(data, cDaeSolverMode);
    EXPECT_NEAR(jacobian.at(0, 0), (high - low) / 2e-5, 1e-6);
}

TEST(GrabberSetTests, EmptyAndStateOnlySetsAreUsableAndClonable)
{
    GrabberSet empty(std::shared_ptr<GridGrabber>{}, std::shared_ptr<StateGrabber>{});
    EXPECT_DOUBLE_EQ(empty.grabData(), kNullVal);
    EXPECT_EQ(empty.getObject(), nullptr);
    EXPECT_TRUE(empty.getDesc().empty());
    EXPECT_FALSE(empty.stateCapable());
    EXPECT_FALSE(empty.hasJacobian());
    auto emptyCopy = empty.clone();
    EXPECT_DOUBLE_EQ(emptyCopy->grabData(), kNullVal);
    AcBus bus("source");
    bus.set("voltage", 1.1);
    auto state = std::make_shared<StateGrabber>("voltage", &bus);
    GrabberSet stateOnly(nullptr, state);
    stateOnly.setDescription("voltage sensor");
    stateOnly.setGain(2.0);
    EXPECT_DOUBLE_EQ(stateOnly.grabData(), 2.2);
    EXPECT_EQ(stateOnly.getObject(), &bus);
    EXPECT_EQ(stateOnly.getDesc(), "voltage sensor");
    EXPECT_TRUE(stateOnly.stateCapable());
    EXPECT_TRUE(stateOnly.hasJacobian());
    auto copy = stateOnly.clone();
    EXPECT_DOUBLE_EQ(copy->grabData(), 2.2);
    EXPECT_EQ(copy->getDesc(), "voltage sensor");
}

TEST(CollectorTests, StateOnlyPointsHaveDescriptionsAndValues)
{
    AcBus bus("bus");
    bus.set("voltage", 1.1);
    Collector collector;
    collector.add(std::make_shared<StateGrabber>("voltage", &bus));
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.getValue(0), 1.1);
    EXPECT_EQ(collector.getColumnDescriptions(), (std::vector<std::string>{"voltage"}));
    EXPECT_EQ(collector.getObject(), &bus);
    collector.clearWarnings();
    EXPECT_TRUE(collector.getWarnings().empty());
    EXPECT_EQ(collector.getWarningCount(), 0U);
}

TEST(CollectorTests, SkippedTriggersKeepTheOriginalSamplingPhase)
{
    Collector collector(0.0, 0.25);
    collector.trigger(1.1);
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), 1.25);
    collector.set("stop", 1.5);
    collector.trigger(1.5);
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), maxTime);
}

TEST(CollectorTests, ChangedVectorWidthRequiresAnExplicitRecheck)
{
    auto values = std::make_shared<std::vector<double>>(2, 1.0);
    auto grabber = std::make_shared<CustomGrabber>();
    grabber->setGrabberFunction(
        [values](CoreObject*, std::vector<double>& data) { data = *values; });
    Collector collector;
    collector.add(grabber);
    collector.trigger(0.0);
    values->push_back(2.0);
    EXPECT_THROW(collector.trigger(1.0), InvalidParameterValue);
    collector.recheckColumns();
    collector.trigger(1.0);
    EXPECT_DOUBLE_EQ(collector.getValue(2), 2.0);
}

TEST_F(MeasurementFiles, FailedFlushCanBeHandledWithoutATargetObject)
{
    Recorder recorder;
    recorder.set("file", directory.string());
    recorder.add(scalarGrabber(1.0));
    recorder.trigger(0.0);
    EXPECT_THROW(recorder.flush(), gmlc::utilities::openFileError);
    recorder.set("file", "");
}

TEST(RecorderOperations, TimeWindowAndInvalidColumnAccess)
{
    Recorder recorder;
    recorder.add(scalarGrabber(3.0));
    recorder.set("start", 1.0);
    recorder.set("stop", 2.0);
    recorder.trigger(0.0);
    recorder.trigger(1.0);
    recorder.trigger(2.0);
    recorder.trigger(3.0);
    EXPECT_EQ(recorder.getTime(), (std::vector<CoreTime>{1.0, 2.0}));
    EXPECT_THROW(recorder.getData(1), std::out_of_range);
    EXPECT_THROW(recorder.set("autosave", -1.0), InvalidParameterValue);
    EXPECT_THROW(recorder.set("reserve", -1.0), InvalidParameterValue);
    EXPECT_THROW(recorder.setSpace(-1.0), InvalidParameterValue);
    EXPECT_THROW(recorder.addSpace(-1.0), InvalidParameterValue);
    EXPECT_THROW(recorder.setSpace(maxTime), InvalidParameterValue);
    EXPECT_THROW(recorder.addSpace(maxTime), InvalidParameterValue);
    recorder.setSpace(10.0);
    recorder.addSpace(10.0);
    recorder.set("precision", 12.0);
}

TEST(RecorderOperations, ChangingColumnsRequiresReset)
{
    Recorder recorder;
    recorder.add(scalarGrabber(1.0));
    recorder.trigger(0.0);
    recorder.add(scalarGrabber(2.0));
    EXPECT_THROW(recorder.trigger(1.0), InvalidParameterValue);
    Collector& interface = recorder;
    interface.reset();
    recorder.trigger(0.0);
    ASSERT_EQ(recorder.getTimeSeries().columns(), 2);
    EXPECT_DOUBLE_EQ(recorder.getData(1).front(), 2.0);
}

TEST(RecorderOperations, XmlSettingsMatchDirectMeasurementSemantics)
{
    auto simulation = readSimXMLFile(std::string{GRIDDYN_TEST_DIRECTORY} +
                                     "/recorder_tests/recorder_contract.xml");
    ASSERT_NE(simulation, nullptr);
    const auto& collectors = simulation->getCollectors();
    ASSERT_EQ(collectors.size(), 3U);
    EXPECT_EQ(readerConfig::warnCount, 0);
    EXPECT_EQ(collectors[2]->numberOfPoints(), 3U);
    collectors[0]->trigger(0.0);
    EXPECT_NEAR(collectors[0]->getValue(0), (2.0 * 0.5 * 180.0 / kPI) + 1.0, 1e-6);
    EXPECT_DOUBLE_EQ(collectors[0]->nextTriggerTime(), 0.25);
    EXPECT_EQ(collectors[0]->getColumnDescriptions(), (std::vector<std::string>{"phase"}));
    collectors[1]->trigger(0.0);
    EXPECT_DOUBLE_EQ(collectors[1]->getValue(0), 0.0);
    EXPECT_DOUBLE_EQ(collectors[1]->getValue(2), 5.0);
    EXPECT_EQ(collectors[1]->getColumnDescriptions().size(), 3U);
}

TEST(GrabberTests, EmptyVectorArithmeticHasNoColumns)
{
    for (const auto& operands : {std::pair{vectorGrabber({}), vectorGrabber({})},
                                 std::pair{scalarGrabber(1.0), vectorGrabber({})},
                                 std::pair{vectorGrabber({}), scalarGrabber(1.0)}}) {
        auto operation = std::make_shared<OpGrabber>(operands.first, operands.second, "+");
        std::vector<double> values{99.0};
        operation->grabVectorData(values);
        EXPECT_TRUE(values.empty());
        std::vector<std::string> descriptions{"old"};
        EXPECT_NO_THROW(operation->getDesc(descriptions));
        EXPECT_TRUE(descriptions.empty());
        Collector collector;
        collector.add(operation);
        EXPECT_TRUE(collector.getColumnDescriptions().empty());
    }
}

TEST(GrabberTests, ScalarReadsOfVectorsReturnTheNullValue)
{
    auto source = vectorGrabber({1.0, 2.0});
    EXPECT_DOUBLE_EQ(source->grabData(), kNullVal);
    GrabberSet set(source, nullptr);
    EXPECT_DOUBLE_EQ(set.grabData(), kNullVal);
    EXPECT_DOUBLE_EQ(set.grabData(emptyStateData, cLocalSolverMode), kNullVal);
}

TEST(CollectorTests, SamplingIntervalsMustFitTheSimulationClock)
{
    Collector collector(0.0, 0.25);
    EXPECT_THROW(collector.set("period", 1e-20), InvalidParameterValue);
    EXPECT_THROW(collector.set("frequency", 1e20), InvalidParameterValue);
    EXPECT_THROW(collector.set("period_resolution", 1e-20), InvalidParameterValue);
    EXPECT_THROW(collector.set("period", 2.0 * static_cast<double>(maxTime)),
                 InvalidParameterValue);
    EXPECT_THROW(Collector(0.0, maxTime), InvalidParameterValue);
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), 0.25);
}

TEST(CollectorTests, ResetRestoresConfiguredStartAndExternalScheduling)
{
    Collector collector(5.0, 0.25);
    collector.trigger(5.7);
    collector.reset();
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), 5.0);
    collector.set("start", 7.0);
    collector.trigger(8.0);
    collector.reset();
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), 7.0);
    collector.set("time", 9.0);
    collector.trigger(10.0);
    collector.reset();
    EXPECT_DOUBLE_EQ(collector.nextTriggerTime(), 9.0);
    Recorder recorder(5.0, 0.25);
    recorder.add(scalarGrabber(3.0));
    recorder.trigger(5.0);
    auto copy = recorder.clone();
    EXPECT_DOUBLE_EQ(copy->nextTriggerTime(), 5.0);
    recorder.reset();
    EXPECT_DOUBLE_EQ(recorder.nextTriggerTime(), 5.0);
    Collector external(maxTime, maxTime);
    external.trigger(1.0);
    external.reset();
    EXPECT_EQ(external.nextTriggerTime(), maxTime);
    EXPECT_EQ(external.clone()->nextTriggerTime(), maxTime);
}

TEST(StateGrabberTests, InvalidOperationsStayInvalidAfterRetargeting)
{
    AcBus first("first");
    AcBus second("second");
    auto source = std::make_shared<StateGrabber>("voltage", &first);
    StateFunctionGrabber function(source, "sin");
    StateOpGrabber operation(source, std::make_shared<StateGrabber>("angle", &first), "+");
    function.updateField("invalid_function");
    operation.updateField("invalid_operation");
    function.updateObject(&second);
    operation.updateObject(&second);
    EXPECT_FALSE(function.loaded);
    EXPECT_FALSE(operation.loaded);
    EXPECT_EQ(function.getJacobianMode(), JacobianMode::NONE);
    EXPECT_EQ(operation.getJacobianMode(), JacobianMode::NONE);
    EXPECT_DOUBLE_EQ(function.grabData(emptyStateData, cLocalSolverMode), kNullVal);
    EXPECT_DOUBLE_EQ(operation.grabData(emptyStateData, cLocalSolverMode), kNullVal);
}

TEST(GrabberTests, CompositeShapeFollowsUpdatedOperands)
{
    auto source = std::make_shared<CustomGrabber>();
    source->setGrabberFunction("sample", [](CoreObject*) { return -2.0; });
    FunctionGrabber function(source, "abs");
    OpGrabber operation(source, scalarGrabber(1.0), "+");
    source->setGrabberFunction([](CoreObject*, std::vector<double>& values) { values = {-2.0}; });
    function.updateObject(nullptr);
    operation.updateObject(nullptr);
    EXPECT_TRUE(function.vectorGrab);
    EXPECT_TRUE(operation.vectorGrab);
    std::vector<double> values;
    function.grabVectorData(values);
    EXPECT_EQ(values, (std::vector<double>{2.0}));
    operation.grabVectorData(values);
    EXPECT_EQ(values, (std::vector<double>{-1.0}));
}

TEST(StateGrabberTests, CloneToReplacesChildrenWithTheirActualTypes)
{
    AcBus bus("bus");
    bus.set("voltage", 0.5);
    auto source = makeStateGrabbers("sin(voltage*voltage)", &bus);
    auto destination = makeStateGrabbers("cos(voltage)", &bus);
    ASSERT_EQ(source.size(), 1U);
    ASSERT_EQ(destination.size(), 1U);
    source.front()->cloneTo(destination.front().get());
    EXPECT_NEAR(destination.front()->grabData(emptyStateData, cLocalSolverMode),
                std::sin(0.25),
                1e-12);
    source = makeStateGrabbers("sin(voltage)*cos(voltage)", &bus);
    destination = makeStateGrabbers("voltage*voltage", &bus);
    source.front()->cloneTo(destination.front().get());
    const auto expected = std::sin(0.5) * std::cos(0.5);
    EXPECT_NEAR(destination.front()->grabData(emptyStateData, cLocalSolverMode), expected, 1e-12);
    destination.front()->cloneTo(destination.front().get());
    EXPECT_NEAR(destination.front()->grabData(emptyStateData, cLocalSolverMode), expected, 1e-12);
}

TEST(GrabberTests, CopyingEmptyCompositesClearsOldTargets)
{
    AcBus bus("bus");
    OpGrabber empty;
    OpGrabber destination(createGrabber("voltage", &bus), createGrabber("angle", &bus), "+");
    empty.cloneTo(&destination);
    EXPECT_FALSE(destination.loaded);
    EXPECT_EQ(destination.getObject(), nullptr);
    StateFunctionGrabber emptyFunction;
    StateFunctionGrabber stateFunction(std::make_shared<StateGrabber>("voltage", &bus), "sin");
    emptyFunction.cloneTo(&stateFunction);
    EXPECT_EQ(stateFunction.getObject(), nullptr);
    StateOpGrabber emptyOperation;
    StateOpGrabber stateOperation(std::make_shared<StateGrabber>("voltage", &bus),
                                  std::make_shared<StateGrabber>("angle", &bus),
                                  "+");
    emptyOperation.cloneTo(&stateOperation);
    EXPECT_EQ(stateOperation.getObject(), nullptr);
}

TEST(StateGrabberTests, ConstantSubexpressionsKeepTheVariableJacobian)
{
    genmodels::GenModelClassical model("machine");
    model.dynInitializeA(0.0, 0);
    model.setOffset(0, cDaeSolverMode);
    const std::array<double, 4> values{0.25, 0.5, 0.0, 1.0};
    const StateData data(0.0, values.data(), nullptr);
    for (const auto& expression :
         {ExpressionCase{.expression = "id*(2^3)", .expected = 8.0},
          ExpressionCase{.expression = "id*sin(0.5)", .expected = std::sin(0.5)}}) {
        SCOPED_TRACE(expression.expression);
        auto grabbers = makeStateGrabbers(expression.expression, &model);
        ASSERT_EQ(grabbers.size(), 1U);
        EXPECT_NE(grabbers.front()->getJacobianMode(), JacobianMode::NONE);
        MatrixDataSparse<double> jacobian;
        grabbers.front()->outputPartialDerivatives(data, jacobian, cDaeSolverMode);
        EXPECT_NEAR(jacobian.at(0, 0), expression.expected, 1e-6);
    }
}

TEST(StateGrabberTests, CompositeMetadataFollowsFieldAndTargetChanges)
{
    AcBus bus("bus");
    auto source = std::make_shared<StateGrabber>("voltage", &bus);
    StateFunctionGrabber function(source, "sin");
    StateOpGrabber operation(source, std::make_shared<StateGrabber>("constant", &bus), "+");
    source->updateField("enabled");
    function.updateField("cos");
    operation.updateField("*");
    EXPECT_EQ(function.getJacobianMode(), JacobianMode::NONE);
    EXPECT_EQ(operation.getJacobianMode(), JacobianMode::NONE);
    source->updateField("voltage");
    function.updateObject(&bus);
    operation.updateObject(&bus);
    EXPECT_EQ(function.getJacobianMode(), JacobianMode::COMPUTED);
    EXPECT_EQ(operation.getJacobianMode(), JacobianMode::COMPUTED);
}

TEST_F(MeasurementFiles, ExplicitSaveToTheSinkPreservesEarlierAutosaveChunks)
{
    Recorder recorder;
    recorder.set("file", (directory / "samples.dat").string());
    recorder.set("autosave", 2.0);
    recorder.add(scalarGrabber(4.0));
    recorder.trigger(0.0);
    recorder.trigger(1.0);
    recorder.trigger(2.0);
    recorder.saveFile((directory / "." / "samples.dat").string());
    recorder.flush();
    gmlc::utilities::TimeSeriesMulti<> saved((directory / "samples.dat").string());
    EXPECT_EQ(saved.time(), (std::vector<double>{0.0, 1.0, 2.0}));
}

TEST_F(MeasurementFiles, FailedAutosaveRetainsSamplesForRetry)
{
    Recorder recorder;
    recorder.set("file", directory.string());
    recorder.set("autosave", 2.0);
    recorder.add(scalarGrabber(4.0));
    recorder.trigger(0.0);
    EXPECT_THROW(recorder.trigger(1.0), gmlc::utilities::openFileError);
    EXPECT_EQ(recorder.getTime().size(), 2U);
    recorder.set("file", (directory / "samples.dat").string());
    recorder.flush();
    recorder.trigger(2.0);
    recorder.flush();
    gmlc::utilities::TimeSeriesMulti<> saved((directory / "samples.dat").string());
    EXPECT_EQ(saved.time(), (std::vector<double>{0.0, 1.0, 2.0}));
}

TEST(RecorderOperations, SameWidthSignalChangesRequireReset)
{
    AcBus first("first");
    AcBus second("second");
    Recorder recorder;
    recorder.add("voltage", &first);
    recorder.trigger(0.0);
    recorder.add(scalarGrabber(3.0), 0);
    EXPECT_THROW(recorder.trigger(1.0), InvalidParameterValue);
    EXPECT_EQ(recorder.getTime().size(), 1U);
    recorder.reset();
    recorder.trigger(0.0);
    EXPECT_DOUBLE_EQ(recorder.getData(0).front(), 3.0);
    recorder.updateObject(&second);
    EXPECT_THROW(recorder.trigger(1.0), InvalidParameterValue);
    recorder.reset();
    recorder.trigger(0.0);
    EXPECT_EQ(recorder.getTimeSeries().getFields(), recorder.getColumnDescriptions());
}

TEST_F(MeasurementFiles, BothFormatsRoundTripMixedFlushAutosaveAndDestructorOutput)
{
    for (const auto* filename : {"samples.dat", "samples.csv"}) {
        SCOPED_TRACE(filename);
        double value = 0.0;
        {
            Recorder recorder("roundtrip");
            recorder.set("directory", directory.string());
            recorder.set("file", filename);
            recorder.set("autosave", 2.0);
            auto active = std::make_shared<CustomGrabber>();
            active->setDescription("active");
            active->setGrabberFunction("sample", [&value](CoreObject*) { return value; });
            auto reactive = scalarGrabber(-2.0);
            reactive->setDescription("reactive");
            recorder.add(active);
            recorder.add(reactive);
            for (int index = 0; index < 5; ++index) {
                value = index + 0.125;
                recorder.trigger(index * 0.25);
                if (index == 0 || index == 2) {
                    recorder.flush();
                }
            }
        }
        gmlc::utilities::TimeSeriesMulti<> saved;
        if (std::string_view{filename}.ends_with(".csv")) {
            saved.loadTextFile((directory / filename).string());
        } else {
            saved.loadBinaryFile((directory / filename).string());
        }
        ASSERT_EQ(saved.size(), 5U);
        EXPECT_EQ(saved.getFields(), (std::vector<std::string>{"active", "reactive"}));
        for (int index = 0; index < 5; ++index) {
            EXPECT_DOUBLE_EQ(saved.time()[index], index * 0.25);
            EXPECT_DOUBLE_EQ(saved.data(0, index), index + 0.125);
            EXPECT_DOUBLE_EQ(saved.data(1, index), -2.0);
        }
    }
}

TEST_F(MeasurementFiles, ChangingSinksStartsAnIndependentOutputSession)
{
    Recorder recorder;
    recorder.set("directory", directory.string());
    recorder.set("file", "samples.dat");
    recorder.add(scalarGrabber(4.0));
    recorder.trigger(0.0);
    recorder.flush();
    recorder.trigger(1.0);
    recorder.set("file", "snapshot.dat");
    recorder.flush();
    recorder.trigger(2.0);
    recorder.flush();
    const gmlc::utilities::TimeSeriesMulti<> first((directory / "samples.dat").string());
    const gmlc::utilities::TimeSeriesMulti<> second((directory / "snapshot.dat").string());
    EXPECT_EQ(first.time(), (std::vector<double>{0.0}));
    EXPECT_EQ(second.time(), (std::vector<double>{0.0, 1.0, 2.0}));
}

TEST_F(MeasurementFiles, ExistingSinkAliasesUseTheConfiguredFileFormat)
{
    Recorder recorder;
    recorder.set("file", (directory / "samples.csv").string());
    recorder.set("autosave", 2.0);
    recorder.add(scalarGrabber(4.0));
    recorder.trigger(0.0);
    recorder.trigger(1.0);
    recorder.trigger(2.0);
    std::filesystem::create_hard_link(directory / "samples.csv", directory / "snapshot.dat");
    recorder.saveFile((directory / "snapshot.dat").string());
    gmlc::utilities::TimeSeriesMulti<> saved;
    saved.loadTextFile((directory / "samples.csv").string());
    EXPECT_EQ(saved.time(), (std::vector<double>{0.0, 1.0, 2.0}));
}

}  // namespace
