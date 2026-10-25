/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/GridArea.h"
#include "griddyn/measurement/Collector.h"
#include "griddyn/measurement/GrabberSet.h"
#include "griddyn/measurement/GridGrabbers.h"
#include "griddyn/measurement/MeasurementExpression.hpp"
#include "griddyn/measurement/StateGrabber.h"
#include "griddyn/primary/AcBus.h"
#include "utilities/GridRandom.h"
#include "utilities/MatrixDataSparse.hpp"
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {
using namespace griddyn;
struct FunctionCase {
    const char* expression;
    double expected;
    FunctionCase(const char* text, double value): expression(text), expected(value) {}
};

class NewGrabberFunctions: public ::testing::TestWithParam<FunctionCase> {};

TEST_P(NewGrabberFunctions, ObjectAndStateValuesAgree)
{
    AcBus bus("source");
    bus.set("voltage", 2.0);
    bus.set("angle", 0.5);
    const auto& example = GetParam();
    SCOPED_TRACE(example.expression);
    auto object = makeGrabbers(example.expression, &bus);
    auto state = makeStateGrabbers(example.expression, &bus);
    ASSERT_EQ(object.size(), 1U);
    ASSERT_EQ(state.size(), 1U);
    EXPECT_NEAR(object[0]->grabData(), example.expected, 1e-12);
    EXPECT_NEAR(state[0]->grabData(emptyStateData, cLocalSolverMode), example.expected, 1e-12);
}

INSTANTIATE_TEST_SUITE_P(Calls,
                         NewGrabberFunctions,
                         ::testing::Values(FunctionCase{"pi()", std::numbers::pi},
                                           FunctionCase{"sin(pi()/2)", 1.0},
                                           FunctionCase{"2*pi()", 2.0 * std::numbers::pi},
                                           FunctionCase{"clamp(voltage,0,1)", 1.0},
                                           FunctionCase{"clamp(voltage,angle,3)", 2.0},
                                           FunctionCase{"clamp(2,0,1)", 1.0},
                                           FunctionCase{"clamp(pi(),0,3)", 3.0},
                                           FunctionCase{"min(voltage,angle,1)", 0.5},
                                           FunctionCase{"max(voltage,angle,1)", 2.0},
                                           FunctionCase{"median(voltage,angle,1)", 1.0},
                                           FunctionCase{"sum(voltage,angle,1)", 3.5},
                                           FunctionCase{"product(voltage,angle,1)", 1.0},
                                           FunctionCase{"hypot(voltage,angle,3)",
                                                        std::hypot(2.0, 0.5, 3.0)},
                                           FunctionCase{"HYPOT(3,4,12)", 13.0},
                                           FunctionCase{"avg(voltage)", 2.0},
                                           FunctionCase{"median(voltage)", 2.0},
                                           FunctionCase{"sum(2,3,4)", 9.0}));

class StateReductionTests: public ::testing::Test {
  protected:
    GridDynSimulation simulation;
    AcBus* first = nullptr;
    AcBus* second = nullptr;
    std::array<double, 4> values{1.1, 0.2, 0.9, -0.1};
    StateData data{0.0, values.data()};

    void SetUp() override
    {
        first = new AcBus("first");
        second = new AcBus("second");
        simulation.add(first);
        simulation.add(second);
        for (auto* bus : {first, second}) {
            bus->pFlowInitializeA(0.0, 0);
            ASSERT_EQ(bus->stateSize(cPflowSolverMode), 2U);
        }
        SolverOffsets offsets;
        offsets.vOffset = 0;
        offsets.aOffset = 1;
        first->setOffsets(offsets, cPflowSolverMode);
        offsets.vOffset = 2;
        offsets.aOffset = 3;
        second->setOffsets(offsets, cPflowSolverMode);
        first->set("voltage", values[0]);
        first->set("angle", values[1]);
        second->set("voltage", values[2]);
        second->set("angle", values[3]);
        ASSERT_EQ(first->getOutputLoc(cPflowSolverMode, VOLTAGE_IN_LOCATION), 0);
        ASSERT_EQ(second->getOutputLoc(cPflowSolverMode, VOLTAGE_IN_LOCATION), 2);
    }

    void checkDerivative(const std::string& expression)
    {
        SCOPED_TRACE(expression);
        auto grabbers = makeStateGrabbers(expression, &simulation);
        ASSERT_EQ(grabbers.size(), 1U);
        ASSERT_NE(grabbers[0]->getJacobianMode(), JacobianMode::NONE);
        MatrixDataSparse<double> jacobian;
        grabbers[0]->outputPartialDerivatives(data, jacobian, cPflowSolverMode);
        jacobian.compact();
        constexpr double step = 1e-5;
        for (size_t column = 0; column < values.size(); ++column) {
            const double previous = values[column];
            values[column] = previous + step;
            const double high = grabbers[0]->grabData(data, cPflowSolverMode);
            values[column] = previous - step;
            const double low = grabbers[0]->grabData(data, cPflowSolverMode);
            values[column] = previous;
            EXPECT_NEAR(jacobian.at(0, static_cast<index_t>(column)),
                        (high - low) / (2.0 * step),
                        1e-6);
        }
    }
};

TEST_F(StateReductionTests, ReductionsReadTheSuppliedStateAndMatchObjectValues)
{
    for (const auto& example : {FunctionCase{"sum(voltage)", 2.0},
                                FunctionCase{"avg(voltage)", 1.0},
                                FunctionCase{"min(voltage)", 0.9},
                                FunctionCase{"max(voltage)", 1.1},
                                FunctionCase{"product(voltage)", 0.99},
                                FunctionCase{"median(voltage)", 1.0},
                                FunctionCase{"stdev(voltage)", 0.1},
                                FunctionCase{"absmin(angle)", 0.1},
                                FunctionCase{"absmax(angle)", 0.2},
                                FunctionCase{"vecprod(voltage,voltage)", 2.02}}) {
        SCOPED_TRACE(example.expression);
        auto object = makeGrabbers(example.expression, &simulation);
        auto state = makeStateGrabbers(example.expression, &simulation);
        ASSERT_EQ(object.size(), 1U);
        ASSERT_EQ(state.size(), 1U);
        EXPECT_NEAR(object[0]->grabData(), example.expected, 1e-12);
        EXPECT_NEAR(state[0]->grabData(data, cPflowSolverMode), example.expected, 1e-12);
    }
    auto sum = makeStateGrabbers("sum(voltage)", &simulation);
    values[0] = 2.0;
    EXPECT_DOUBLE_EQ(sum[0]->grabData(data, cPflowSolverMode), 2.9);
    EXPECT_DOUBLE_EQ(first->get("voltage"), 1.1);
}

TEST_F(StateReductionTests, ReductionChainRulesMatchIndependentDifferences)
{
    for (const auto* expression : {"sum(voltage)",
                                   "avg(voltage)",
                                   "min(voltage)",
                                   "max(voltage)",
                                   "product(voltage)",
                                   "median(voltage)",
                                   "stdev(voltage)",
                                   "absmin(angle)",
                                   "absmax(angle)",
                                   "vecprod(voltage,voltage)",
                                   "sin(sum(voltage))",
                                   "sum(sin(voltage))",
                                   "sum(voltage*voltage)",
                                   "sum(2/voltage)",
                                   "sum(clamp(voltage,1,2))",
                                   "sum(clamp(1,angle,voltage))",
                                   "sum(hypot(voltage,angle,1))",
                                   "vecprod(voltage-1,angle)",
                                   "sum(angle(deg))",
                                   "sum(log10(voltage))",
                                   "sum(log2(voltage))",
                                   "sum(cbrt(angle))",
                                   "sum(voltage*avg(2))",
                                   "2*sum(angle(deg))+1"}) {
        checkDerivative(expression);
    }
}

TEST_F(StateReductionTests, TernaryPartialsIncludeEveryVariableArgument)
{
    for (const auto* expression : {"hypot(first:voltage,first:angle,second:voltage)",
                                   "sum(first:voltage,first:angle,second:voltage)",
                                   "product(first:voltage,first:angle,second:voltage)",
                                   "min(first:voltage,first:angle,second:voltage)",
                                   "max(first:voltage,first:angle,second:voltage)",
                                   "median(first:voltage,first:angle,second:voltage)",
                                   "clamp(first:voltage,first:angle,second:voltage)",
                                   "clamp(first:angle,second:angle,second:voltage)",
                                   "clamp(second:angle,first:angle,second:voltage)"}) {
        checkDerivative(expression);
    }
}

TEST_F(StateReductionTests, ClampAndExtremaDefineBoundaryAndTieRules)
{
    values[0] = 1.0;
    values[2] = 1.0;
    for (const auto* expression : {"min(voltage)", "max(voltage)"}) {
        auto grabbers = makeStateGrabbers(expression, &simulation);
        MatrixDataSparse<double> jacobian;
        grabbers[0]->outputPartialDerivatives(data, jacobian, cPflowSolverMode);
        EXPECT_DOUBLE_EQ(jacobian.at(0, 0), 1.0);
        EXPECT_DOUBLE_EQ(jacobian.at(0, 2), 0.0);
    }
    for (const auto* expression : {"clamp(first:voltage,1,2)", "clamp(first:voltage,0,1)"}) {
        auto grabbers = makeStateGrabbers(expression, &simulation);
        MatrixDataSparse<double> jacobian;
        grabbers[0]->outputPartialDerivatives(data, jacobian, cPflowSolverMode);
        EXPECT_DOUBLE_EQ(jacobian.at(0, 0), 1.0);
    }
    auto fixed = makeStateGrabbers("clamp(first:voltage,1,1)", &simulation);
    MatrixDataSparse<double> jacobian;
    fixed[0]->outputPartialDerivatives(data, jacobian, cPflowSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), 0.0);
    values[0] = 0.0;
    checkDerivative("product(voltage)");
    values[2] = 0.0;
    checkDerivative("product(voltage)");
}

TEST_F(StateReductionTests, TernaryBroadcastAndStateVectorIntegration)
{
    for (const auto* expression :
         {"clamp(voltage,0,1)", "clamp(1,angle,voltage)", "hypot(voltage,angle,1)"}) {
        auto object = makeGrabbers(std::string{expression} + " as values", &simulation);
        auto state = makeStateGrabbers(expression, &simulation);
        std::vector<double> expected;
        std::vector<double> actual;
        object[0]->grabVectorData(expected);
        state[0]->grabVectorData(data, cPflowSolverMode, actual);
        EXPECT_EQ(actual, expected);
        stringVec descriptions;
        object[0]->getDesc(descriptions);
        EXPECT_EQ(descriptions, (stringVec{"values[0]", "values[1]"}));
    }
    auto state = createStateGrabber("voltage", &simulation);
    Collector collector;
    collector.add(std::shared_ptr<StateGrabber>(state->clone()));
    collector.trigger(0.0);
    EXPECT_DOUBLE_EQ(collector.getValue(0), 1.1);
    EXPECT_DOUBLE_EQ(collector.getValue(1), 0.9);
    EXPECT_EQ(collector.getColumnDescriptions(), (stringVec{"first:voltage", "second:voltage"}));
    GrabberSet set(nullptr, std::shared_ptr<StateGrabber>(state->clone()));
    std::vector<double> actual;
    values[0] = 2.0;
    set.grabData(data, cPflowSolverMode, actual);
    EXPECT_EQ(actual, (std::vector<double>{2.0, 0.9}));
}

TEST_F(StateReductionTests, ClonesRetargetWithoutChangingOriginals)
{
    GridDynSimulation other;
    auto* bus = new AcBus("third");
    other.add(bus);
    bus->set("voltage", 0.5);
    auto source = makeStateGrabbers("sum(clamp(voltage,0,1))", &simulation);
    auto copy = source[0]->clone();
    copy->updateObject(&other);
    EXPECT_DOUBLE_EQ(copy->grabData(emptyStateData, cLocalSolverMode), 0.5);
    EXPECT_DOUBLE_EQ(source[0]->grabData(emptyStateData, cLocalSolverMode), 1.9);
    source[0]->cloneTo(copy.get());
    EXPECT_DOUBLE_EQ(copy->grabData(emptyStateData, cLocalSolverMode), 1.9);
    auto vector = createStateGrabber("voltage", &simulation);
    vector->updateField("angle");
    std::vector<double> valuesOut;
    vector->grabVectorData(data, cPflowSolverMode, valuesOut);
    EXPECT_EQ(valuesOut, (std::vector<double>{0.2, -0.1}));
    vector->updateField("avgfreq");
    EXPECT_FALSE(vector->vectorGrab);
    vector->updateField("voltage");
    EXPECT_TRUE(vector->vectorGrab);
    auto object = makeGrabbers("clamp(voltage,0,1) as terminals", &simulation);
    auto objectCopy = object[0]->clone();
    objectCopy->updateObject(&other);
    objectCopy->grabVectorData(valuesOut);
    EXPECT_EQ(valuesOut, (std::vector<double>{0.5}));
    stringVec descriptions;
    objectCopy->getDesc(descriptions);
    EXPECT_EQ(descriptions, (stringVec{"terminals[0]"}));
}

TEST_F(StateReductionTests, NestedAreasMatchObjectOrderingAndAbsoluteMinima)
{
    auto* area = new GridArea("nested");
    auto* nested = new AcBus("inside");
    nested->set("voltage", 0.7);
    nested->set("angle", 0.01);
    area->add(nested);
    simulation.add(area);
    auto object = makeGrabbers("voltage", &simulation);
    auto state = makeStateGrabbers("voltage", &simulation);
    std::vector<double> objectValues;
    std::vector<double> stateValues;
    object[0]->grabVectorData(objectValues);
    state[0]->grabVectorData(emptyStateData, cLocalSolverMode, stateValues);
    EXPECT_EQ(objectValues, (std::vector<double>{0.7, 1.1, 0.9}));
    EXPECT_EQ(stateValues, objectValues);
    stringVec objectNames;
    stringVec stateNames;
    object[0]->getDesc(objectNames);
    state[0]->getDesc(stateNames);
    EXPECT_EQ(stateNames, objectNames);
    auto alias = createStateGrabber("v", &simulation);
    alias->getDesc(stateNames);
    object = makeGrabbers("v", &simulation);
    object[0]->getDesc(objectNames);
    EXPECT_EQ(stateNames, objectNames);
    object = makeGrabbers("absmin(angle)", &simulation);
    state = makeStateGrabbers("absmin(angle)", &simulation);
    EXPECT_DOUBLE_EQ(object[0]->grabData(), 0.01);
    EXPECT_DOUBLE_EQ(state[0]->grabData(emptyStateData, cLocalSolverMode), 0.01);
}

TEST_F(StateReductionTests, FieldsWithoutStateDerivativesDoNotClaimJacobians)
{
    auto state = makeStateGrabbers("sum(busloadreal)", &simulation);
    ASSERT_EQ(state.size(), 1U);
    EXPECT_EQ(state[0]->getJacobianMode(), JacobianMode::NONE);
    EXPECT_DOUBLE_EQ(state[0]->grabData(emptyStateData, cLocalSolverMode), 0.0);
}

class CountedStateVector final: public StateGrabber {
    std::shared_ptr<size_t> mReads;

  public:
    explicit CountedStateVector(std::shared_ptr<size_t> reads): mReads(std::move(reads))
    {
        loaded = true;
        vectorGrab = true;
        jacMode = JacobianMode::COMPUTED;
        field = "samples";
    }
    void grabVectorData(const StateData& /*data*/,
                        const SolverMode& /*mode*/,
                        std::vector<double>& values) override
    {
        ++*mReads;
        values.assign(32, 0.5);
    }
    void outputVectorPartialDerivatives(const StateData& /*data*/,
                                        MatrixData<double>& matrix,
                                        const SolverMode& /*mode*/,
                                        size_t element) override
    {
        matrix.assign(0, static_cast<index_t>(element), 1.0);
    }
};

TEST(GrabberFunctionTests, ReductionJacobiansDoNotRereadVectorsForEachElement)
{
    const auto reads = std::make_shared<size_t>(0);
    auto product = std::make_unique<MeasurementExpression<StateGrabber>>(
        std::make_unique<CountedStateVector>(reads),
        std::make_unique<CountedStateVector>(reads),
        "*");
    auto sine = std::make_unique<MeasurementExpression<StateGrabber>>(std::move(product), "sin");
    MeasurementExpression<StateGrabber> sum(std::move(sine), "sum");
    MatrixDataSparse<double> jacobian;
    sum.outputPartialDerivatives(emptyStateData, jacobian, cLocalSolverMode);
    jacobian.compact();
    EXPECT_LE(*reads, 8U);
    for (index_t column = 0; column < 32; ++column) {
        EXPECT_NEAR(jacobian.at(0, column), std::cos(0.25), 1e-12);
    }
}

TEST_F(StateReductionTests, ShapeMismatchesAndInvalidBoundsAreRejected)
{
    auto* nested = new GridArea("nested");
    nested->add(new AcBus("nested_bus"));
    simulation.add(nested);
    for (const auto* expression :
         {"clamp(voltage,nested:voltage,2)", "vecprod(voltage,nested:voltage)"}) {
        auto object = makeGrabbers(expression, &simulation);
        auto state = makeStateGrabbers(expression, &simulation);
        ASSERT_EQ(object.size(), 1U);
        ASSERT_EQ(state.size(), 1U);
        std::vector<double> output;
        if (object[0]->vectorGrab) {
            EXPECT_THROW(object[0]->grabVectorData(output), InvalidParameterValue);
        } else {
            EXPECT_THROW(object[0]->grabData(), InvalidParameterValue);
        }
        if (state[0]->vectorGrab) {
            EXPECT_THROW(state[0]->grabVectorData(emptyStateData, cLocalSolverMode, output),
                         InvalidParameterValue);
        } else {
            EXPECT_THROW(state[0]->grabData(emptyStateData, cLocalSolverMode),
                         InvalidParameterValue);
        }
    }
    EXPECT_THROW(makeGrabbers("clamp(1,2,0)", &simulation), InvalidParameterValue);
    EXPECT_THROW(makeStateGrabbers("clamp(first:voltage,2,0)", &simulation), InvalidParameterValue);
    EXPECT_THROW(makeGrabbers("clamp(voltage,nan(),2)", &simulation), InvalidParameterValue);
    auto state =
        makeStateGrabbers("clamp(first:voltage,first:voltage,second:voltage)", &simulation);
    EXPECT_THROW(state[0]->grabData(data, cPflowSolverMode), InvalidParameterValue);
    MatrixDataSparse<double> jacobian;
    EXPECT_THROW(state[0]->outputPartialDerivatives(data, jacobian, cPflowSolverMode),
                 InvalidParameterValue);
}

TEST(GrabberFunctionTests, EmptyVectorsAndInvalidAritiesAreSafe)
{
    GridDynSimulation empty;
    for (const auto* expression : {"sum(voltage)",
                                   "avg(voltage)",
                                   "min(voltage)",
                                   "max(voltage)",
                                   "vecprod(voltage,voltage)"}) {
        auto state = makeStateGrabbers(expression, &empty);
        ASSERT_EQ(state.size(), 1U);
        EXPECT_DOUBLE_EQ(state[0]->grabData(emptyStateData, cLocalSolverMode), kNullVal);
        MatrixDataSparse<double> jacobian;
        state[0]->outputPartialDerivatives(emptyStateData, jacobian, cLocalSolverMode);
        EXPECT_DOUBLE_EQ(jacobian.at(0, 0), 0.0);
    }
    for (const auto* expression : {"clamp()",
                                   "clamp(1,2)",
                                   "clamp(1,,2)",
                                   "clamp(1,2,3,4)",
                                   "pi(1)",
                                   "pi(1,2,3)",
                                   "hypot(1,2,)",
                                   "sin(1,2,3)"}) {
        EXPECT_TRUE(makeGrabbers(expression, &empty).empty());
        EXPECT_TRUE(makeStateGrabbers(expression, &empty).empty());
    }
    auto constant = makeStateGrabbers("pi()", nullptr);
    ASSERT_EQ(constant.size(), 1U);
    EXPECT_DOUBLE_EQ(constant[0]->grabData(emptyStateData, cLocalSolverMode), std::numbers::pi);
}

TEST(GrabberFunctionTests, ZeroArgumentCallsNestAndSeparateCorrectly)
{
    AcBus bus("bus");
    auto object = makeGrabbers("pi(),clamp(pi(),0,3);sum(1,pi(),2)", &bus);
    auto state = makeStateGrabbers("pi(),clamp(pi(),0,3);sum(1,pi(),2)", &bus);
    ASSERT_EQ(object.size(), 3U);
    ASSERT_EQ(state.size(), 3U);
    EXPECT_DOUBLE_EQ(object[0]->grabData(), std::numbers::pi);
    EXPECT_DOUBLE_EQ(object[1]->grabData(), 3.0);
    EXPECT_DOUBLE_EQ(object[2]->grabData(), 3.0 + std::numbers::pi);
    EXPECT_DOUBLE_EQ(state[2]->grabData(emptyStateData, cLocalSolverMode), 3.0 + std::numbers::pi);
}

TEST(GrabberFunctionTests, NamedConstantsHaveTheirDocumentedValues)
{
    auto object = makeGrabbers("nan(),inf(),pi()", nullptr);
    auto state = makeStateGrabbers("nan(),inf(),pi()", nullptr);
    ASSERT_EQ(object.size(), 3U);
    ASSERT_EQ(state.size(), 3U);
    EXPECT_TRUE(std::isnan(object[0]->grabData()));
    EXPECT_TRUE(std::isnan(state[0]->grabData(emptyStateData, cLocalSolverMode)));
    EXPECT_DOUBLE_EQ(object[1]->grabData(), 1e48);
    EXPECT_DOUBLE_EQ(state[1]->grabData(emptyStateData, cLocalSolverMode), 1e48);
    EXPECT_DOUBLE_EQ(object[2]->clone()->grabData(), std::numbers::pi);
    EXPECT_EQ(state[2]->getJacobianMode(), JacobianMode::NONE);
}

TEST(GrabberFunctionTests, RecorderFunctionsLoadFromXml)
{
    auto simulation = readSimXMLFile(std::string{GRIDDYN_TEST_DIRECTORY} +
                                     "/recorder_tests/grabber_functions.xml");
    ASSERT_NE(simulation, nullptr);
    const auto& collectors = simulation->getCollectors();
    ASSERT_EQ(collectors.size(), 2U);
    collectors[0]->trigger(0.0);
    collectors[1]->trigger(0.0);
    EXPECT_EQ(collectors[0]->getColumnDescriptions(), (stringVec{"limited", "magnitude"}));
    EXPECT_DOUBLE_EQ(collectors[0]->getValue(0), 1.0);
    EXPECT_NEAR(collectors[0]->getValue(1), std::hypot(2.0, 0.5, std::numbers::pi), 1e-12);
    EXPECT_DOUBLE_EQ(collectors[1]->getValue(0), 2.0);
    EXPECT_DOUBLE_EQ(collectors[1]->getValue(1), 4.0);
    EXPECT_EQ(readerConfig::warnCount, 0);
}

TEST_F(StateReductionTests, OddMediansAndConstantDeviationHaveDefinedDerivatives)
{
    auto* third = new AcBus("third");
    simulation.add(third);
    third->pFlowInitializeA(0.0, 0);
    ASSERT_EQ(third->stateSize(cPflowSolverMode), 2U);
    SolverOffsets offsets;
    offsets.vOffset = 4;
    offsets.aOffset = 5;
    third->setOffsets(offsets, cPflowSolverMode);
    std::array<double, 6> extended{1.1, 0.2, 0.9, -0.1, 1.0, 0.0};
    const StateData extendedData(0.0, extended.data());
    auto median = makeStateGrabbers("median(voltage)", &simulation);
    MatrixDataSparse<double> jacobian;
    median[0]->outputPartialDerivatives(extendedData, jacobian, cPflowSolverMode);
    jacobian.compact();
    EXPECT_DOUBLE_EQ(jacobian.at(0, 4), 1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), 0.0);
    extended[0] = 1.0;
    extended[2] = 1.0;
    jacobian.clear();
    median[0]->outputPartialDerivatives(extendedData, jacobian, cPflowSolverMode);
    jacobian.compact();
    EXPECT_DOUBLE_EQ(jacobian.at(0, 2), 1.0);
    auto deviation = makeStateGrabbers("stdev(voltage)", &simulation);
    jacobian.clear();
    deviation[0]->outputPartialDerivatives(extendedData, jacobian, cPflowSolverMode);
    EXPECT_DOUBLE_EQ(deviation[0]->grabData(extendedData, cPflowSolverMode), 0.0);
    EXPECT_EQ(jacobian.size(), 0U);
}

TEST(GrabberFunctionTests, RandomCallsAreNotFoldedOrAcceptedByStateExpressions)
{
    const auto previousSeed = utilities::GridRandom::getSeed();
    utilities::GridRandom::setSeed(1234);
    auto random = makeGrabbers("rand(0,1)", nullptr);
    ASSERT_EQ(random.size(), 1U);
    const auto first = random[0]->grabData();
    const auto second = random[0]->grabData();
    utilities::GridRandom::setSeed(1234);
    EXPECT_DOUBLE_EQ(first, evalFunction("rand", 0.0, 1.0));
    EXPECT_DOUBLE_EQ(second, evalFunction("rand", 0.0, 1.0));
    for (const auto* expression : {"rand()", "rand(0,1)", "randn(0,1)", "sin(rand(0,1))"}) {
        EXPECT_TRUE(makeStateGrabbers(expression, nullptr).empty());
    }
    utilities::GridRandom::setSeed(previousSeed);
}

TEST(GrabberFunctionTests, UnloadedExpressionNodesAreSafeToCloneAndRetarget)
{
    MeasurementExpression<StateGrabber> invalid(std::unique_ptr<StateGrabber>{}, "sin");
    auto copy = invalid.clone();
    copy->updateObject(nullptr);
    EXPECT_FALSE(copy->loaded);
    EXPECT_DOUBLE_EQ(copy->grabData(emptyStateData, cLocalSolverMode), kNullVal);
    std::vector<CoreObject*> objects;
    copy->getObjects(objects);
    EXPECT_TRUE(objects.empty());
}
}  // namespace
