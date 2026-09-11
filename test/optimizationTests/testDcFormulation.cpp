/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/links/AcLine.h"
#include "optimization/gridDynOpt.h"
#include "optimization/models/gridBusOpt.h"
#include "optimization/models/gridGenOpt.h"
#include "optimization/models/gridLinkOpt.h"
#include "optimization/nativeDenseSolver.h"
#include "optimization/optHelperClasses.h"
#include "optimization/optimizerInterface.h"
#ifdef GRIDDYN_ENABLE_HIGHS
#    include "optimization/highsOptimizer.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::filesystem::path makePyPowerCasePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / std::string{fileName};
}

std::filesystem::path makeValidationCasePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "validation_tests" /
        std::string{fileName};
}

griddyn::OptimizationMode makeDcMode(griddyn::LinearityMode linearity)
{
    return griddyn::OptimizationMode{.flowMode = griddyn::FlowModel::DC,
                                     .linMode = linearity,
                                     .offsetIndex = 0,
                                     .numPeriods = 1,
                                     .period = 1.0};
}

void expectFinite(const std::vector<double>& values)
{
    for (const auto value : values) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

void expectFinite(const griddyn::NativeSparseMatrix& matrix)
{
    EXPECT_TRUE(matrix.validate());
    for (const auto value : matrix.values) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

griddyn::NativeQpProblem makeNativeTestProblem(std::size_t variableCount,
                                               std::size_t constraintCount)
{
    griddyn::NativeQpProblem problem;
    problem.valid = true;
    problem.classification = griddyn::NativeProblemClass::SUPPORTED_LINEAR;
    problem.variableCount = variableCount;
    problem.constraintCount = constraintCount;
    problem.initialValues.assign(variableCount, 0.0);
    problem.variableLowerBounds.assign(variableCount, -std::numeric_limits<double>::infinity());
    problem.variableUpperBounds.assign(variableCount, std::numeric_limits<double>::infinity());
    problem.linearObjective.assign(variableCount, 0.0);
    problem.quadraticObjective.assign(variableCount, 0.0);
    problem.constraintLowerBounds.assign(constraintCount, -std::numeric_limits<double>::infinity());
    problem.constraintUpperBounds.assign(constraintCount, std::numeric_limits<double>::infinity());
    problem.constraintOffsets.assign(constraintCount, 0.0);
    problem.constraintMatrix.setDimensions(constraintCount, variableCount);
    problem.initialConstraintValues.assign(constraintCount, 0.0);
    problem.initialGradient.assign(variableCount, 0.0);
    problem.variableTypes.assign(variableCount, CONTINUOUS_OBJECTIVE_VARIABLE);
    problem.tolerances.assign(variableCount, 1e-8);
    problem.variableNames.resize(variableCount);
    problem.constraintNames.resize(constraintCount);
    for (std::size_t column = 0; column < variableCount; ++column) {
        problem.variableNames[column] = "x[" + std::to_string(column) + "]";
    }
    for (std::size_t row = 0; row < constraintCount; ++row) {
        problem.constraintNames[row] = "constraint[" + std::to_string(row) + "]";
    }
    return problem;
}

griddyn::NativeQpProblem makeThreeBusNativeProblem(double generator1UpperBound,
                                                   double branch12Limit)
{
    // Variables are [Pg1, Pg2, theta1, theta2, theta3].  The first three
    // rows are nodal balances, row three fixes the angle reference, and the
    // last row is the thermal limit on the 1--2 branch.
    auto problem = makeNativeTestProblem(5, 5);
    problem.classification = griddyn::NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC;
    problem.linearObjective = {1.0, 2.0, 0.0, 0.0, 0.0};
    problem.quadraticObjective = {1.0, 2.0, 0.0, 0.0, 0.0};
    problem.variableLowerBounds = {0.0,
                                   0.0,
                                   -std::numeric_limits<double>::infinity(),
                                   -std::numeric_limits<double>::infinity(),
                                   -std::numeric_limits<double>::infinity()};
    problem.variableUpperBounds = {generator1UpperBound,
                                   1.2,
                                   std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::infinity()};
    problem.constraintMatrix.assignDense({
        1.0, 0.0,  -10.0, 10.0, 0.0, 0.0, 1.0, 10.0, -15.0, 5.0,  0.0,   0.0, 0.0,
        5.0, -5.0, 0.0,   0.0,  1.0, 0.0, 0.0, 0.0,  0.0,   10.0, -10.0, 0.0,
    });
    problem.constraintLowerBounds = {0.0, 0.0, 1.0, 0.0, -branch12Limit};
    problem.constraintUpperBounds = {0.0, 0.0, 1.0, 0.0, branch12Limit};
    return problem;
}

void expectNativeProblemContentsEqual(const griddyn::NativeQpProblem& expected,
                                      const griddyn::NativeQpProblem& actual)
{
    EXPECT_EQ(actual.mode.flowMode, expected.mode.flowMode);
    EXPECT_EQ(actual.mode.linMode, expected.mode.linMode);
    EXPECT_EQ(actual.mode.offsetIndex, expected.mode.offsetIndex);
    EXPECT_EQ(actual.mode.numPeriods, expected.mode.numPeriods);
    EXPECT_DOUBLE_EQ(actual.mode.period, expected.mode.period);
    EXPECT_EQ(actual.mode.allowInteger, expected.mode.allowInteger);
    EXPECT_EQ(actual.mode.allowBinary, expected.mode.allowBinary);
    EXPECT_EQ(actual.classification, expected.classification);
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(actual.validationError, expected.validationError);
    EXPECT_EQ(actual.variableCount, expected.variableCount);
    EXPECT_EQ(actual.constraintCount, expected.constraintCount);
    EXPECT_EQ(actual.initialValues, expected.initialValues);
    EXPECT_EQ(actual.variableLowerBounds, expected.variableLowerBounds);
    EXPECT_EQ(actual.variableUpperBounds, expected.variableUpperBounds);
    EXPECT_EQ(actual.linearObjective, expected.linearObjective);
    EXPECT_EQ(actual.quadraticObjective, expected.quadraticObjective);
    EXPECT_DOUBLE_EQ(actual.objectiveConstant, expected.objectiveConstant);
    EXPECT_EQ(actual.constraintLowerBounds, expected.constraintLowerBounds);
    EXPECT_EQ(actual.constraintUpperBounds, expected.constraintUpperBounds);
    EXPECT_EQ(actual.constraintOffsets, expected.constraintOffsets);
    EXPECT_EQ(actual.constraintMatrix, expected.constraintMatrix);
    EXPECT_EQ(actual.initialConstraintValues, expected.initialConstraintValues);
    EXPECT_EQ(actual.initialGradient, expected.initialGradient);
    EXPECT_EQ(actual.variableTypes, expected.variableTypes);
    EXPECT_EQ(actual.tolerances, expected.tolerances);
    EXPECT_EQ(actual.variableNames, expected.variableNames);
    EXPECT_EQ(actual.constraintNames, expected.constraintNames);
}

void expectNativeProblemDataFinite(const griddyn::NativeQpProblem& problem)
{
    EXPECT_TRUE(problem.valid);
    EXPECT_TRUE(std::isfinite(problem.objectiveConstant));
    expectFinite(problem.initialValues);
    expectFinite(problem.linearObjective);
    expectFinite(problem.quadraticObjective);
    expectFinite(problem.constraintOffsets);
    expectFinite(problem.constraintMatrix);
    expectFinite(problem.initialConstraintValues);
    expectFinite(problem.initialGradient);
    expectFinite(problem.variableTypes);
    expectFinite(problem.tolerances);

    const auto checkBounds = [](const std::vector<double>& lowerBounds,
                                const std::vector<double>& upperBounds) {
        EXPECT_EQ(lowerBounds.size(), upperBounds.size());
        for (std::size_t index = 0; index < lowerBounds.size(); ++index) {
            const double lower = lowerBounds[index];
            const double upper = upperBounds[index];
            EXPECT_FALSE(std::isnan(lower));
            EXPECT_FALSE(std::isnan(upper));
            EXPECT_FALSE(std::isinf(lower) && (lower > 0.0));
            EXPECT_FALSE(std::isinf(upper) && (upper < 0.0));
            EXPECT_LE(lower, upper);
        }
    };
    checkBounds(problem.variableLowerBounds, problem.variableUpperBounds);
    checkBounds(problem.constraintLowerBounds, problem.constraintUpperBounds);
    for (const auto& name : problem.variableNames) {
        EXPECT_FALSE(name.empty());
    }
    for (const auto& name : problem.constraintNames) {
        EXPECT_FALSE(name.empty());
    }
}

}  // namespace

TEST(OptimizationDcFormulationTests, LoadPyPowerGeneratorCost)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* generator = gds->findByUserID("gen", 1);
    ASSERT_NE(generator, nullptr);
    auto* generatorOpt = dynamic_cast<griddyn::GridGenOpt*>(gds->getOptimizationObject(generator));
    ASSERT_NE(generatorOpt, nullptr);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    generatorOpt->loadSizes(mode);
    generatorOpt->setOffset(0, 0, mode);
    // MATPOWER gencost uses MW, whereas GridDyn optimization variables use pu.
    const double dispatch = 0.1;
    const griddyn::OptimizationData optimizationData{0.0, &dispatch, 0};

    // case2.py specifies 0.01 * Pg^2 + Pg.
    EXPECT_DOUBLE_EQ(generatorOpt->objValue(optimizationData, mode), 11.0);
}

TEST(OptimizationDcFormulationTests, OptimizationDataViewsOptimizerOwnedStorage)
{
    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    griddyn::BasicOptimizer optimizer("basic");
    optimizer.setOptimizationData(nullptr, mode);

    ASSERT_EQ(optimizer.allocate(3, 2), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.getSize(), 3);
    ASSERT_EQ(optimizer.constraintSize(), 2);
    EXPECT_EQ(optimizer.size(), 3);
    EXPECT_EQ(optimizer.constraintCount(), 2);
    ASSERT_EQ(optimizer.values.size(), 3U);
    ASSERT_EQ(optimizer.constraintValues.size(), 2U);
    EXPECT_TRUE(optimizer.getFlag("allocated"));
    EXPECT_FALSE(optimizer.getFlag("initialized"));
    optimizer.setFlag("sparse", true);
    EXPECT_TRUE(optimizer.getFlag("sparse"));
    EXPECT_FALSE(optimizer.getFlag("dense"));
    optimizer.setFlag("dense", true);
    EXPECT_TRUE(optimizer.getFlag("dense"));
    EXPECT_FALSE(optimizer.getFlag("sparse"));

    optimizer.values[0] = 1.0;
    auto optimizationData = optimizer.makeOptimizationData(4.0);

    EXPECT_EQ(optimizationData.time, 4.0);
    EXPECT_EQ(optimizationData.valueSize, 3);
    EXPECT_EQ(optimizationData.constraintSize, 2);
    EXPECT_EQ(optimizationData.val, optimizer.val_data());
    EXPECT_EQ(optimizationData.val[0], 1.0);
    EXPECT_NE(optimizationData.scratch1, nullptr);
    EXPECT_NE(optimizationData.scratch2, nullptr);
    EXPECT_TRUE(optimizationData.hasScratch());
    EXPECT_FALSE(optimizationData.empty());

    const double candidateValues[] = {2.0, 3.0, 4.0};
    auto candidateData = optimizer.makeOptimizationData(5.0, candidateValues);
    EXPECT_EQ(candidateData.val, candidateValues);
    EXPECT_EQ(candidateData.val[2], 4.0);
    EXPECT_EQ(candidateData.valueSize, 3);
    EXPECT_EQ(candidateData.constraintSize, 2);
}

TEST(OptimizationDcFormulationTests, EconomicDispatchOptimizerFactorySelection)
{
    auto directOptimizer = griddyn::makeOptimizer("dispatch");
    ASSERT_NE(directOptimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::EconomicDispatchOptimizer*>(directOptimizer.get()), nullptr);

    auto aliasOptimizer = griddyn::makeOptimizer("pricestack");
    ASSERT_NE(aliasOptimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::EconomicDispatchOptimizer*>(aliasOptimizer.get()), nullptr);

    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->set("optimizer", "dispatch");
    gds->initializeOptimizationModel(mode);

    auto optimizer = gds->getOptimizerInterface(mode);
    ASSERT_NE(optimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::EconomicDispatchOptimizer*>(optimizer.get()), nullptr);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerFactorySelectionAndProblemAssembly)
{
    auto directOptimizer = griddyn::makeOptimizer("native");
    ASSERT_NE(directOptimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::NativeOptimizer*>(directOptimizer.get()), nullptr);

    auto aliasOptimizer = griddyn::makeOptimizer("nativeqp");
    ASSERT_NE(aliasOptimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::NativeOptimizer*>(aliasOptimizer.get()), nullptr);

    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->set("optimizer", "native");
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto configuredOptimizer = gds->getOptimizerInterface(mode);
    ASSERT_NE(configuredOptimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::NativeOptimizer*>(configuredOptimizer.get()), nullptr);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer.initialize(0.0);
    ASSERT_TRUE(optimizer.isInitialized());
    EXPECT_TRUE(optimizer.getFlag("dense"));

    EXPECT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer.get("problem_loaded"), 1.0);
    EXPECT_EQ(optimizer.get("native_solver"), 1.0);
    EXPECT_EQ(optimizer.size(), 3);
    EXPECT_EQ(optimizer.constraintCount(), 4);
    EXPECT_EQ(optimizer.linearObjective.points(), 1);
    EXPECT_EQ(optimizer.quadraticObjective.points(), 1);
    EXPECT_EQ(optimizer.constraintLowerBounds.size(), 4U);
    EXPECT_EQ(optimizer.constraintUpperBounds.size(), 4U);
    EXPECT_GT(optimizer.constraintJacobian.size(), 0);
    expectFinite(optimizer.values);
    expectFinite(optimizer.lowerBounds);
    expectFinite(optimizer.upperBounds);
    expectFinite(optimizer.constraintValues);
    expectFinite(optimizer.gradient);

    EXPECT_EQ(optimizer.writeBack(), FUNCTION_EXECUTION_FAILURE);
    double returnTime = -1.0;
    ASSERT_EQ(optimizer.solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.lastSolveResult().message;
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_EQ(optimizer.get("problem_loaded"), 1.0);
    EXPECT_EQ(optimizer.get("solution_valid"), 1.0);
    EXPECT_EQ(optimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    EXPECT_TRUE(std::isfinite(optimizer.get("objective_value")));
    EXPECT_EQ(optimizer.writeBack(), FUNCTION_EXECUTION_SUCCESS);
}

TEST(OptimizationDcFormulationTests, NativeProblemUsesHiGHSBoundedRowNormalization)
{
    griddyn::NativeQpProblem problem;
    problem.variableCount = 1;
    problem.constraintCount = 1;
    problem.constraintMatrix.setDimensions(problem.constraintCount, problem.variableCount);
    problem.initialValues = {0.0};
    problem.variableLowerBounds = {-1.0};
    problem.variableUpperBounds = {2.0};
    problem.linearObjective = {3.0};
    problem.quadraticObjective = {0.5};
    problem.constraintLowerBounds = {-1.0};
    problem.constraintUpperBounds = {3.0};
    problem.constraintOffsets = {0.5};
    problem.constraintMatrix.assignDense({2.0});
    problem.initialConstraintValues = {0.5};
    problem.initialGradient = {3.0};
    problem.variableTypes = {CONTINUOUS_OBJECTIVE_VARIABLE};
    problem.tolerances = {1e-6};
    problem.variableNames = {"x[0]"};
    problem.constraintNames = {"constraint[0]"};

    std::string validationError;
    ASSERT_TRUE(problem.validate(&validationError)) << validationError;
    EXPECT_DOUBLE_EQ(problem.constraintValue(0, problem.initialValues), 0.5);
    EXPECT_DOUBLE_EQ(problem.solverConstraintLowerBound(0), -1.5);
    EXPECT_DOUBLE_EQ(problem.solverConstraintUpperBound(0), 2.5);
    EXPECT_DOUBLE_EQ(problem.objectiveValue(problem.initialValues), 0.0);
    EXPECT_DOUBLE_EQ(problem.objectiveGradient(problem.initialValues)[0], 3.0);
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverSolvesBoundedConvexQp)
{
    auto problem = makeNativeTestProblem(2, 1);
    problem.classification = griddyn::NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC;
    problem.linearObjective = {-2.0, -4.0};
    problem.quadraticObjective = {1.0, 1.0};
    problem.constraintUpperBounds[0] = 2.0;
    problem.constraintMatrix.assignDense({1.0, 1.0});

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    ASSERT_EQ(result.status, griddyn::NativeSolveStatus::OPTIMAL) << result.message;
    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_NEAR(result.values[0], 0.5, 1e-8);
    EXPECT_NEAR(result.values[1], 1.5, 1e-8);
    EXPECT_NEAR(result.objectiveValue, -4.5, 1e-8);
    EXPECT_LE(result.maximumConstraintViolation, 1e-8);
    EXPECT_LE(result.maximumStationarity, 1e-8);
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverActivatesLowerRowLimit)
{
    auto problem = makeNativeTestProblem(1, 1);
    problem.linearObjective = {1.0};
    problem.constraintLowerBounds[0] = 1.0;
    problem.constraintMatrix.assignDense({1.0});

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    ASSERT_EQ(result.status, griddyn::NativeSolveStatus::OPTIMAL) << result.message;
    ASSERT_EQ(result.values.size(), 1U);
    EXPECT_NEAR(result.values[0], 1.0, 1e-8);
    EXPECT_LE(result.maximumConstraintViolation, 1e-8);
    EXPECT_GE(result.activeSetSize, 1U);
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverUsesPhaseOneForEqualityFeasibility)
{
    auto problem = makeNativeTestProblem(2, 1);
    problem.classification = griddyn::NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC;
    problem.initialValues = {0.0, 0.0};
    problem.variableLowerBounds = {0.0, 0.0};
    problem.quadraticObjective = {1.0, 2.0};
    problem.constraintLowerBounds[0] = 3.0;
    problem.constraintUpperBounds[0] = 3.0;
    problem.constraintMatrix.assignDense({1.0, 1.0});

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    ASSERT_EQ(result.status, griddyn::NativeSolveStatus::OPTIMAL) << result.message;
    EXPECT_NEAR(result.values[0], 2.0, 1e-8);
    EXPECT_NEAR(result.values[1], 1.0, 1e-8);
    EXPECT_NEAR(result.values[0] + result.values[1], 3.0, 1e-8);
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverReportsInfeasiblePhaseOne)
{
    auto problem = makeNativeTestProblem(1, 2);
    problem.constraintLowerBounds[0] = 2.0;
    problem.constraintUpperBounds[1] = 1.0;
    problem.constraintMatrix.assignDense({1.0, 1.0});

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    EXPECT_EQ(result.status, griddyn::NativeSolveStatus::INFEASIBLE) << result.message;
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverReportsUnboundedLinearDirection)
{
    auto problem = makeNativeTestProblem(1, 0);
    problem.linearObjective = {-1.0};

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    EXPECT_EQ(result.status, griddyn::NativeSolveStatus::UNBOUNDED) << result.message;
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverPresolvesRedundantEqualities)
{
    auto problem = makeNativeTestProblem(1, 2);
    problem.constraintLowerBounds = {1.0, 1.0};
    problem.constraintUpperBounds = {1.0, 1.0};
    problem.constraintMatrix.assignDense({1.0, 1.0});
    problem.initialValues = {0.0};

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    ASSERT_EQ(result.status, griddyn::NativeSolveStatus::OPTIMAL) << result.message;
    ASSERT_EQ(result.values.size(), 1U);
    EXPECT_NEAR(result.values[0], 1.0, 1e-8);
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverReportsInconsistentEqualities)
{
    auto problem = makeNativeTestProblem(1, 2);
    problem.constraintLowerBounds = {1.0, 2.0};
    problem.constraintUpperBounds = {1.0, 2.0};
    problem.constraintMatrix.assignDense({1.0, 1.0});
    problem.initialValues = {0.0};

    const auto result = griddyn::NativeDenseSolver::solve(problem);

    EXPECT_EQ(result.status, griddyn::NativeSolveStatus::INFEASIBLE) << result.message;
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverSolvesThreeBusDispatchAndLimits)
{
    const auto uncongested = griddyn::NativeDenseSolver::solve(
        makeThreeBusNativeProblem(1.2, std::numeric_limits<double>::infinity()));
    ASSERT_EQ(uncongested.status, griddyn::NativeSolveStatus::OPTIMAL) << uncongested.message;
    ASSERT_EQ(uncongested.values.size(), 5U);
    EXPECT_NEAR(uncongested.values[0], 5.0 / 6.0, 1e-8);
    EXPECT_NEAR(uncongested.values[1], 1.0 / 6.0, 1e-8);
    EXPECT_NEAR(uncongested.values[0] + uncongested.values[1], 1.0, 1e-8);
    EXPECT_NEAR(10.0 * (uncongested.values[2] - uncongested.values[3]), 5.0 / 6.0, 1e-8);
    EXPECT_LE(uncongested.maximumConstraintViolation, 1e-8);

    const auto generatorLimited =
        griddyn::NativeDenseSolver::solve(makeThreeBusNativeProblem(0.6, 1.2));
    ASSERT_EQ(generatorLimited.status, griddyn::NativeSolveStatus::OPTIMAL)
        << generatorLimited.message;
    EXPECT_NEAR(generatorLimited.values[0], 0.6, 1e-8);
    EXPECT_NEAR(generatorLimited.values[1], 0.4, 1e-8);
    EXPECT_LE(generatorLimited.maximumBoundViolation, 1e-8);
    EXPECT_LE(generatorLimited.maximumConstraintViolation, 1e-8);

    const auto congested = griddyn::NativeDenseSolver::solve(makeThreeBusNativeProblem(1.2, 0.5));
    ASSERT_EQ(congested.status, griddyn::NativeSolveStatus::OPTIMAL) << congested.message;
    EXPECT_NEAR(congested.values[0], 0.5, 1e-8);
    EXPECT_NEAR(congested.values[1], 0.5, 1e-8);
    EXPECT_NEAR(10.0 * (congested.values[2] - congested.values[3]), 0.5, 1e-8);
    EXPECT_LE(congested.maximumConstraintViolation, 1e-8);
}

TEST(OptimizationDcFormulationTests, NativeDenseSolverPresolvesFixedVariablesAndScalesRows)
{
    auto fixedVariable = makeNativeTestProblem(2, 1);
    fixedVariable.classification = griddyn::NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC;
    fixedVariable.variableLowerBounds[0] = 2.0;
    fixedVariable.variableUpperBounds[0] = 2.0;
    fixedVariable.variableLowerBounds[1] = 0.0;
    fixedVariable.variableUpperBounds[1] = 10.0;
    fixedVariable.linearObjective = {0.0, -4.0};
    fixedVariable.quadraticObjective = {3.0, 1.0};
    fixedVariable.constraintLowerBounds[0] = 3.0;
    fixedVariable.constraintUpperBounds[0] = 3.0;
    fixedVariable.constraintMatrix.assignDense({1.0, 1.0});

    const auto fixedResult = griddyn::NativeDenseSolver::solve(fixedVariable);
    ASSERT_EQ(fixedResult.status, griddyn::NativeSolveStatus::OPTIMAL) << fixedResult.message;
    ASSERT_EQ(fixedResult.values.size(), 2U);
    EXPECT_NEAR(fixedResult.values[0], 2.0, 1e-10);
    EXPECT_NEAR(fixedResult.values[1], 1.0, 1e-8);
    EXPECT_NEAR(fixedResult.objectiveValue, 9.0, 1e-8);

    auto scaledRows = makeNativeTestProblem(2, 1);
    scaledRows.classification = griddyn::NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC;
    scaledRows.variableLowerBounds = {0.0, 0.0};
    scaledRows.variableUpperBounds = {1.0, 1.0};
    scaledRows.quadraticObjective = {1.0, 1.0};
    scaledRows.constraintLowerBounds[0] = 1.0;
    scaledRows.constraintUpperBounds[0] = 1.0;
    scaledRows.constraintMatrix.assignDense({1.0e6, 1.0});

    const auto scaledResult = griddyn::NativeDenseSolver::solve(scaledRows);
    ASSERT_EQ(scaledResult.status, griddyn::NativeSolveStatus::OPTIMAL) << scaledResult.message;
    ASSERT_EQ(scaledResult.values.size(), 2U);
    EXPECT_GT(scaledResult.values[0], 0.9e-6);
    EXPECT_LT(scaledResult.values[1], 1.0e-6);
    EXPECT_LE(scaledResult.maximumConstraintViolation, 1e-8);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerMaterializesTwoBusAffineQp)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = gds->findByUserID("link", 1);
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->set("tap", 2.0);
    physicalBranch->set("tapangle", 0.1);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& branchOffsets = branch->offsets.getOffsets(mode);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS);

    const auto& problem = optimizer.problem();
    ASSERT_TRUE(problem.valid);
    EXPECT_EQ(problem.classification,
              griddyn::NativeProblemClass::SUPPORTED_CONVEX_DIAGONAL_QUADRATIC);
    EXPECT_EQ(problem.variableCount, 3U);
    EXPECT_EQ(problem.constraintCount, 4U);
    EXPECT_EQ(problem.constraintMatrix.rowCount, problem.constraintCount);
    EXPECT_EQ(problem.constraintMatrix.columnCount, problem.variableCount);
    EXPECT_EQ(problem.variableNames.size(), 3U);
    EXPECT_EQ(problem.constraintNames.size(), 4U);
    EXPECT_EQ(problem.modelVersion, 1U);

    const auto matrixValue = [&problem](index_t row, index_t column) {
        return problem.constraintMatrix.coefficient(static_cast<std::size_t>(row),
                                                    static_cast<std::size_t>(column));
    };
    EXPECT_DOUBLE_EQ(matrixValue(bus1Offsets.constraintOffset, generatorOffsets.gOffset), 1.0);
    EXPECT_DOUBLE_EQ(matrixValue(bus1Offsets.constraintOffset, bus1Offsets.aOffset), -5.0);
    EXPECT_DOUBLE_EQ(matrixValue(bus1Offsets.constraintOffset, bus2Offsets.aOffset), 5.0);
    EXPECT_DOUBLE_EQ(matrixValue(bus2Offsets.constraintOffset, bus1Offsets.aOffset), 5.0);
    EXPECT_DOUBLE_EQ(matrixValue(bus2Offsets.constraintOffset, bus2Offsets.aOffset), -5.0);
    EXPECT_DOUBLE_EQ(matrixValue(branchOffsets.constraintOffset, bus1Offsets.aOffset), 5.0);
    EXPECT_DOUBLE_EQ(matrixValue(branchOffsets.constraintOffset, bus2Offsets.aOffset), -5.0);
    EXPECT_DOUBLE_EQ(problem.constraintOffsets[branchOffsets.constraintOffset], -0.5);
    EXPECT_DOUBLE_EQ(problem.solverConstraintLowerBound(branchOffsets.constraintOffset), -0.5);
    EXPECT_DOUBLE_EQ(problem.solverConstraintUpperBound(branchOffsets.constraintOffset), 1.5);

    std::vector<double> candidate = problem.initialValues;
    candidate[generatorOffsets.gOffset] = 0.1;
    candidate[bus1Offsets.aOffset] = 0.05;
    candidate[bus2Offsets.aOffset] = -0.02;
    EXPECT_NEAR(problem.objectiveValue(candidate),
                optimizer.objectiveFunction(0.0, candidate.data()),
                1e-10);
    std::vector<double> callbackGradient(problem.variableCount, 0.0);
    ASSERT_EQ(optimizer.gradientFunction(0.0, candidate.data(), callbackGradient.data()),
              FUNCTION_EXECUTION_SUCCESS);
    const auto nativeGradient = problem.objectiveGradient(candidate);
    for (std::size_t column = 0; column < problem.variableCount; ++column) {
        EXPECT_NEAR(nativeGradient[column], callbackGradient[column], 1e-10);
    }

    std::vector<double> callbackConstraints(problem.constraintCount, 0.0);
    ASSERT_EQ(optimizer.constraintFunction(0.0, candidate.data(), callbackConstraints.data()),
              FUNCTION_EXECUTION_SUCCESS);
    for (std::size_t row = 0; row < problem.constraintCount; ++row) {
        EXPECT_NEAR(problem.constraintValue(row, candidate), callbackConstraints[row], 1e-10);
    }
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSnapshotMatchesCallbacksAndJacobian)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = gds->findByUserID("link", 1);
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->set("tap", 2.0);
    physicalBranch->set("tapangle", 0.1);
    physicalBranch->set("minangle", -0.05);
    physicalBranch->set("maxangle", 0.15);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    ASSERT_EQ(branch->constraintSize(mode), 2);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& branchOffsets = branch->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.getLastErrorString();

    const auto& problem = optimizer.problem();
    std::vector<double> candidate = problem.initialValues;
    candidate[generatorOffsets.gOffset] = 0.1;
    candidate[bus1Offsets.aOffset] = 0.05;
    candidate[bus2Offsets.aOffset] = -0.02;

    EXPECT_NEAR(problem.objectiveValue(candidate),
                optimizer.objectiveFunction(0.0, candidate.data()),
                1e-10);
    std::vector<double> callbackGradient(problem.variableCount, 0.0);
    ASSERT_EQ(optimizer.gradientFunction(0.0, candidate.data(), callbackGradient.data()),
              FUNCTION_EXECUTION_SUCCESS);
    const auto nativeGradient = problem.objectiveGradient(candidate);
    ASSERT_EQ(nativeGradient.size(), callbackGradient.size());
    for (std::size_t column = 0; column < problem.variableCount; ++column) {
        EXPECT_NEAR(nativeGradient[column], callbackGradient[column], 1e-10);
    }

    std::vector<double> callbackConstraints(problem.constraintCount, 0.0);
    ASSERT_EQ(optimizer.constraintFunction(0.0, candidate.data(), callbackConstraints.data()),
              FUNCTION_EXECUTION_SUCCESS);
    for (std::size_t row = 0; row < problem.constraintCount; ++row) {
        EXPECT_NEAR(problem.constraintValue(row, candidate), callbackConstraints[row], 1e-10);
    }

    MatrixDataSparse<double> callbackJacobian;
    ASSERT_EQ(optimizer.constraintJacobianFunction(0.0, candidate.data(), callbackJacobian),
              FUNCTION_EXECUTION_SUCCESS);
    callbackJacobian.sortIndex();
    callbackJacobian.compact();
    std::vector<double> denseJacobian(problem.constraintCount * problem.variableCount, 0.0);
    for (const auto& element : callbackJacobian) {
        ASSERT_GE(element.row, 0);
        ASSERT_GE(element.col, 0);
        ASSERT_LT(static_cast<std::size_t>(element.row), problem.constraintCount);
        ASSERT_LT(static_cast<std::size_t>(element.col), problem.variableCount);
        denseJacobian[(static_cast<std::size_t>(element.row) * problem.variableCount) +
                      static_cast<std::size_t>(element.col)] += element.data;
    }
    ASSERT_EQ(problem.constraintMatrix.rowCount, problem.constraintCount);
    ASSERT_EQ(problem.constraintMatrix.columnCount, problem.variableCount);
    for (std::size_t row = 0; row < problem.constraintCount; ++row) {
        for (std::size_t column = 0; column < problem.variableCount; ++column) {
            EXPECT_DOUBLE_EQ(problem.constraintMatrix.coefficient(row, column),
                             denseJacobian[(row * problem.variableCount) + column]);
        }
    }

    const auto flowRow = static_cast<std::size_t>(branchOffsets.constraintOffset);
    const auto angleRow = flowRow + 1;
    EXPECT_NEAR(problem.constraintOffsets[flowRow], -0.5, 1e-12);
    EXPECT_NEAR(problem.constraintLowerBounds[flowRow], -1.0, 1e-12);
    EXPECT_NEAR(problem.constraintUpperBounds[flowRow], 1.0, 1e-12);
    EXPECT_NEAR(problem.solverConstraintLowerBound(flowRow), -0.5, 1e-12);
    EXPECT_NEAR(problem.solverConstraintUpperBound(flowRow), 1.5, 1e-12);
    EXPECT_NEAR(problem.constraintOffsets[angleRow], -0.1, 1e-12);
    EXPECT_NEAR(problem.constraintLowerBounds[angleRow], -0.05, 1e-12);
    EXPECT_NEAR(problem.constraintUpperBounds[angleRow], 0.15, 1e-12);
    EXPECT_NEAR(problem.solverConstraintLowerBound(angleRow), 0.05, 1e-12);
    EXPECT_NEAR(problem.solverConstraintUpperBound(angleRow), 0.25, 1e-12);
    EXPECT_NEAR(problem.constraintValue(flowRow, candidate), -0.15, 1e-12);
    EXPECT_NEAR(problem.constraintValue(angleRow, candidate), -0.03, 1e-12);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerPreparationIsRepeatableAndOwnsSnapshot)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = gds->findByUserID("link", 1);
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->set("tap", 2.0);
    physicalBranch->set("tapangle", 0.1);
    physicalBranch->set("minangle", -0.05);
    physicalBranch->set("maxangle", 0.15);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.getLastErrorString();
    const auto firstSnapshot = optimizer.problem();

    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.getLastErrorString();
    const auto secondSnapshot = optimizer.problem();
    EXPECT_EQ(secondSnapshot.modelVersion, firstSnapshot.modelVersion + 1);
    expectNativeProblemContentsEqual(firstSnapshot, secondSnapshot);

    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(branch, nullptr);
    const auto flowRow =
        static_cast<std::size_t>(branch->offsets.getOffsets(mode).constraintOffset);
    const double snapshotFlow =
        secondSnapshot.constraintValue(flowRow, secondSnapshot.initialValues);
    physicalBranch->set("tapangle", 0.2);
    physicalBranch->set("ratinga", 50.0, units::MW);

    std::vector<double> mutatedCallbackConstraints(secondSnapshot.constraintCount, 0.0);
    ASSERT_EQ(optimizer.constraintFunction(0.0,
                                           secondSnapshot.initialValues.data(),
                                           mutatedCallbackConstraints.data()),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NE(mutatedCallbackConstraints[flowRow], snapshotFlow);
    expectNativeProblemContentsEqual(secondSnapshot, optimizer.problem());
}

TEST(OptimizationDcFormulationTests, NativeOptimizerAssemblesFiniteDeterministicCaseSnapshots)
{
    const std::vector<std::string> caseFiles{"case2.py", "case9.m", "case118.m"};
    for (const auto& caseFile : caseFiles) {
        SCOPED_TRACE(caseFile);
        auto gds = std::make_unique<griddyn::GridDynOptimization>();
        const auto filePath = (caseFile == "case2.py") ? makePyPowerCasePath(caseFile) :
                                                         makeValidationCasePath(caseFile);
        ASSERT_TRUE(std::filesystem::exists(filePath));
        griddyn::loadFile(gds.get(), filePath.string());

        const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
        ASSERT_NO_THROW(gds->initializeOptimizationModel(mode));
        auto* root = gds->getOptimizationObject();
        ASSERT_NE(root, nullptr);

        griddyn::NativeOptimizer optimizer(gds.get(), mode);
        ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
                  FUNCTION_EXECUTION_SUCCESS);
        optimizer.initialize(0.0);
        ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
            << optimizer.getLastErrorString();
        const auto firstSnapshot = optimizer.problem();
        expectNativeProblemDataFinite(firstSnapshot);
        EXPECT_EQ(firstSnapshot.variableCount, static_cast<std::size_t>(root->objSize(mode)));
        EXPECT_EQ(firstSnapshot.constraintCount,
                  static_cast<std::size_t>(root->constraintSize(mode)));
        EXPECT_EQ(firstSnapshot.constraintMatrix.rowCount, firstSnapshot.constraintCount);
        EXPECT_EQ(firstSnapshot.constraintMatrix.columnCount, firstSnapshot.variableCount);

        ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
            << optimizer.getLastErrorString();
        const auto secondSnapshot = optimizer.problem();
        EXPECT_EQ(secondSnapshot.modelVersion, firstSnapshot.modelVersion + 1);
        expectNativeProblemContentsEqual(firstSnapshot, secondSnapshot);
    }
}

TEST(OptimizationDcFormulationTests, NativeOptimizerPreservesConstantGeneratorCost)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    auto* physicalGenerator = gds->findByUserID("gen", 1);
    ASSERT_NE(physicalGenerator, nullptr);
    auto* generatorOpt =
        dynamic_cast<griddyn::GridGenOpt*>(gds->getOptimizationObject(physicalGenerator));
    ASSERT_NE(generatorOpt, nullptr);
    generatorOpt->set("constantp", 7.0);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    const auto& generatorOffsets = generatorOpt->offsets.getOffsets(mode);
    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS);

    const auto& problem = optimizer.problem();
    EXPECT_DOUBLE_EQ(problem.objectiveConstant, 7.0);
    std::vector<double> candidate = problem.initialValues;
    candidate[generatorOffsets.gOffset] = 0.1;
    EXPECT_NEAR(problem.objectiveValue(candidate), 18.0, 1e-10);
    EXPECT_NEAR(optimizer.objectiveFunction(0.0, candidate.data()), 18.0, 1e-10);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerMaterializesCase118)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case118.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    EXPECT_NO_THROW(gds->initializeOptimizationModel(mode));
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.getLastErrorString();

    const auto& problem = optimizer.problem();
    EXPECT_TRUE(problem.valid);
    EXPECT_EQ(problem.classification, griddyn::NativeProblemClass::SUPPORTED_LINEAR);
    EXPECT_EQ(problem.variableCount, static_cast<std::size_t>(root->objSize(mode)));
    EXPECT_EQ(problem.constraintCount, static_cast<std::size_t>(root->constraintSize(mode)));
    EXPECT_EQ(problem.constraintMatrix.rowCount, problem.constraintCount);
    EXPECT_EQ(problem.constraintMatrix.columnCount, problem.variableCount);
    expectFinite(problem.initialValues);
    expectFinite(problem.linearObjective);
    expectFinite(problem.constraintMatrix);

    const auto firstVersion = problem.modelVersion;
    const auto firstMatrix = problem.constraintMatrix;
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer.problem().modelVersion, firstVersion + 1);
    EXPECT_EQ(optimizer.problem().constraintMatrix, firstMatrix);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerRejectsPiecewiseLinearCost)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case30pwl.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);

    EXPECT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_FAILURE);
    EXPECT_EQ(optimizer.get("problem_loaded"), 0.0);
    EXPECT_NE(optimizer.getLastErrorString().find("piecewise-linear"), std::string::npos);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSetsUpCase13659WithoutSolve)
{
    const auto filePath = makeValidationCasePath("case13659pegase.m");
    ASSERT_TRUE(std::filesystem::exists(filePath));

    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    const auto variableCount = static_cast<std::size_t>(root->objSize(mode));
    const auto constraintCount = static_cast<std::size_t>(root->constraintSize(mode));
    EXPECT_GT(variableCount, 13000U);
    EXPECT_GT(constraintCount, 13000U);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(static_cast<count_t>(variableCount),
                                 static_cast<count_t>(constraintCount)),
              FUNCTION_EXECUTION_SUCCESS);
    const auto callbackStart = std::chrono::steady_clock::now();
    optimizer.initialize(0.0);
    ASSERT_TRUE(optimizer.isInitialized());
    ASSERT_EQ(optimizer.loadInitialGuess(0.0), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.loadVariableBounds(0.0), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.loadVariableTypes(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.loadTolerances(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.loadLinearObjective(0.0), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.loadLinearConstraints(0.0), FUNCTION_EXECUTION_SUCCESS);
    optimizer.linearConstraints.sortIndex();
    optimizer.linearConstraints.compact();
    optimizer.quadraticObjective.sortIndex();
    optimizer.quadraticObjective.compact();
    ASSERT_GT(optimizer.linearObjective.points(), 0);
    ASSERT_GT(optimizer.quadraticObjective.points(), 0);
    for (std::size_t column = 0; column < variableCount; ++column) {
        EXPECT_TRUE(std::isfinite(optimizer.quadraticObjective.at(static_cast<index_t>(column))));
    }

    const auto objectiveValue = optimizer.objectiveFunction(0.0, optimizer.values.data());
    EXPECT_TRUE(std::isfinite(objectiveValue));
    expectFinite(optimizer.values);
    expectFinite(optimizer.lowerBounds);
    expectFinite(optimizer.upperBounds);
    expectFinite(optimizer.constraintLowerBounds);
    expectFinite(optimizer.constraintUpperBounds);
    expectFinite(optimizer.tolerances);

    ASSERT_EQ(optimizer.gradientFunction(0.0, optimizer.values.data(), optimizer.gradient.data()),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(optimizer.constraintFunction(0.0,
                                           optimizer.values.data(),
                                           optimizer.constraintValues.data()),
              FUNCTION_EXECUTION_SUCCESS);
    expectFinite(optimizer.gradient);
    expectFinite(optimizer.constraintValues);

    auto& jacobian = optimizer.constraintJacobianFunction(0.0, optimizer.values.data());
    jacobian.sortIndex();
    jacobian.compact();
    ASSERT_GT(jacobian.size(), 0);

    bool validJacobian = true;
    index_t previousRow = 0;
    index_t previousColumn = 0;
    bool firstJacobianEntry = true;
    for (const auto& element : jacobian) {
        validJacobian = validJacobian && (element.row >= 0) && (element.col >= 0) &&
            std::cmp_less(element.row, constraintCount) &&
            std::cmp_less(element.col, variableCount) && std::isfinite(element.data);
        if (!firstJacobianEntry) {
            if (element.row == previousRow) {
                validJacobian = validJacobian && (element.col > previousColumn);
            } else {
                validJacobian = validJacobian && (element.row > previousRow);
            }
        }
        previousRow = element.row;
        previousColumn = element.col;
        firstJacobianEntry = false;
    }
    EXPECT_TRUE(validJacobian);

    bool validLinearRows = true;
    for (const auto& element : optimizer.linearConstraints) {
        validLinearRows = validLinearRows && (element.row >= 0) && (element.col >= 0) &&
            std::cmp_less(element.row, constraintCount) &&
            std::cmp_less(element.col, variableCount) && std::isfinite(element.data);
    }
    EXPECT_TRUE(validLinearRows);

    const auto callbackStop = std::chrono::steady_clock::now();
    const auto loadMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(loadStop - loadStart);
    const auto setupMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(setupStop - setupStart);
    const auto callbackMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(callbackStop - callbackStart);
    RecordProperty("case13659_load_ms", static_cast<int>(loadMilliseconds.count()));
    RecordProperty("case13659_setup_ms", static_cast<int>(setupMilliseconds.count()));
    RecordProperty("case13659_callback_ms", static_cast<int>(callbackMilliseconds.count()));
    RecordProperty("case13659_variables", static_cast<int>(variableCount));
    RecordProperty("case13659_constraints", static_cast<int>(constraintCount));
    RecordProperty("case13659_linear_rows", static_cast<int>(optimizer.linearConstraints.size()));
    RecordProperty("case13659_jacobian_nnz", static_cast<int>(jacobian.size()));
    const auto vectorBytes = (optimizer.values.capacity() * sizeof(double)) +
        (optimizer.lowerBounds.capacity() * sizeof(double)) +
        (optimizer.upperBounds.capacity() * sizeof(double)) +
        (optimizer.constraintValues.capacity() * sizeof(double)) +
        (optimizer.constraintLowerBounds.capacity() * sizeof(double)) +
        (optimizer.constraintUpperBounds.capacity() * sizeof(double)) +
        (optimizer.gradient.capacity() * sizeof(double)) +
        (optimizer.variableType.capacity() * sizeof(double)) +
        (optimizer.tolerances.capacity() * sizeof(double));
    const auto sparseBytes = ((optimizer.linearConstraints.capacity() + jacobian.capacity()) *
                              sizeof(MatrixElement<double>));
    RecordProperty("case13659_callback_storage_bytes", std::to_string(vectorBytes + sparseBytes));

    const auto prepareStart = std::chrono::steady_clock::now();
    ASSERT_EQ(optimizer.prepareProblemData(0.0), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.getLastErrorString();
    const auto prepareStop = std::chrono::steady_clock::now();
    const auto& problem = optimizer.problem();
    ASSERT_TRUE(problem.valid);
    EXPECT_EQ(problem.variableCount, variableCount);
    EXPECT_EQ(problem.constraintCount, constraintCount);
    EXPECT_EQ(problem.constraintMatrix.rowCount, constraintCount);
    EXPECT_EQ(problem.constraintMatrix.columnCount, variableCount);
    EXPECT_EQ(problem.constraintMatrix.size(), jacobian.size());
    EXPECT_LT(problem.constraintMatrix.size(), variableCount * constraintCount);
    expectFinite(problem.constraintMatrix);
    RecordProperty("case13659_sparse_prepare_ms",
                   static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        prepareStop - prepareStart)
                                        .count()));
    const auto sparseSnapshotBytes =
        problem.constraintMatrix.rowStarts.capacity() * sizeof(std::size_t) +
        problem.constraintMatrix.columnIndices.capacity() * sizeof(std::size_t) +
        problem.constraintMatrix.values.capacity() * sizeof(double);
    const auto denseEquivalentBytes = variableCount * constraintCount * sizeof(double);
    EXPECT_LT(sparseSnapshotBytes, denseEquivalentBytes / 100U);
    RecordProperty("case13659_sparse_snapshot_bytes", std::to_string(sparseSnapshotBytes));
}

TEST(OptimizationDcFormulationTests, TwoBusDcModelHasBusOwnedBalanceRows)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalGenerator = gds->findByUserID("gen", 1);
    ASSERT_NE(physicalGenerator, nullptr);
    auto* preloadedGeneratorOpt = gds->getOptimizationObject(physicalGenerator);
    ASSERT_NE(preloadedGeneratorOpt, nullptr);

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);
    EXPECT_EQ(bus1->getGen(0), preloadedGeneratorOpt);
    EXPECT_EQ(gds->getOptimizationObject(physicalGenerator), preloadedGeneratorOpt);

    // DC OPF equations are distributed: each bus owns its nodal balance row,
    // fixed-angle buses own their reference-angle row, and active branch
    // limits own their own rows. Branches always contribute flow terms to the
    // connected buses.
    EXPECT_EQ(bus1->constraintSize(mode), 2);
    EXPECT_EQ(bus2->constraintSize(mode), 1);
    EXPECT_EQ(branch->constraintSize(mode), 1);
    EXPECT_EQ(root->constraintSize(mode), 4);
    EXPECT_EQ(root->objSize(mode), 3);
    EXPECT_EQ(root->aSize(mode), 2);
    EXPECT_EQ(root->genSize(mode), 1);

    const auto& rootOffsets = root->offsets.getOffsets(mode);
    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);

    EXPECT_EQ(rootOffsets.aOffset, 0);
    EXPECT_EQ(rootOffsets.gOffset, 2);
    EXPECT_EQ(rootOffsets.vOffset, kNullLocation);
    EXPECT_EQ(rootOffsets.qOffset, kNullLocation);
    EXPECT_EQ(rootOffsets.intOffset, kNullLocation);
    EXPECT_EQ(rootOffsets.constraintOffset, 0);
    EXPECT_EQ(bus1Offsets.aOffset, 0);
    EXPECT_EQ(bus2Offsets.aOffset, 1);
    EXPECT_EQ(generatorOffsets.gOffset, 2);
    EXPECT_EQ(bus1Offsets.constraintOffset, 0);
    EXPECT_EQ(bus2Offsets.constraintOffset, 2);
    EXPECT_EQ(branch->offsets.getOffsets(mode).constraintOffset, 3);

    std::vector<index_t> usedObjectiveOffsets{bus1Offsets.aOffset,
                                              bus2Offsets.aOffset,
                                              generatorOffsets.gOffset};
    std::sort(usedObjectiveOffsets.begin(), usedObjectiveOffsets.end());
    EXPECT_EQ(usedObjectiveOffsets, (std::vector<index_t>{0, 1, 2}));

    std::vector<index_t> usedConstraintOffsets{bus1Offsets.constraintOffset,
                                               bus1Offsets.constraintOffset + 1,
                                               bus2Offsets.constraintOffset,
                                               branch->offsets.getOffsets(mode).constraintOffset};
    std::sort(usedConstraintOffsets.begin(), usedConstraintOffsets.end());
    EXPECT_EQ(usedConstraintOffsets, (std::vector<index_t>{0, 1, 2, 3}));

    gds->initializeOptimizationModel(mode);
    EXPECT_EQ(bus1->getGen(0), preloadedGeneratorOpt);
    EXPECT_EQ(root->constraintSize(mode), 4);
    EXPECT_EQ(root->objSize(mode), 3);
    EXPECT_EQ(root->aSize(mode), 2);
    EXPECT_EQ(root->genSize(mode), 1);
    EXPECT_EQ(bus1Offsets.aOffset, 0);
    EXPECT_EQ(bus2Offsets.aOffset, 1);
    EXPECT_EQ(generatorOffsets.gOffset, 2);
}

TEST(OptimizationDcFormulationTests, TwoBusDcFlatLayoutIsZeroBasedAndContiguous)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode, 0);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);
    ASSERT_LT(bus1Offsets.aOffset, root->objSize(mode));
    ASSERT_LT(bus2Offsets.aOffset, root->objSize(mode));
    ASSERT_LT(generatorOffsets.gOffset, root->objSize(mode));

    // Flat layout follows the simulation object traversal: child objective
    // variables are placed first, then the bus-local network angle variables.
    std::vector<index_t> usedObjectiveOffsets{bus1Offsets.aOffset,
                                              bus2Offsets.aOffset,
                                              generatorOffsets.gOffset};
    std::sort(usedObjectiveOffsets.begin(), usedObjectiveOffsets.end());
    EXPECT_EQ(usedObjectiveOffsets, (std::vector<index_t>{0, 1, 2}));
}

TEST(OptimizationDcFormulationTests, TwoBusDcBranchSourceAndFlow)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = dynamic_cast<griddyn::Link*>(gds->findByUserID("link", 1));
    ASSERT_NE(physicalBranch, nullptr);
    EXPECT_NEAR(physicalBranch->get("x"), 0.1, 1e-12);

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(branch, nullptr);
    ASSERT_NE(generator, nullptr);
    ASSERT_EQ(branch->getBus(1), bus1);
    ASSERT_EQ(branch->getBus(2), bus2);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);
    std::vector<double> values(root->objSize(mode), 0.0);
    ASSERT_LT(bus1Offsets.aOffset, values.size());
    ASSERT_LT(bus2Offsets.aOffset, values.size());
    ASSERT_LT(generatorOffsets.gOffset, values.size());
    ASSERT_NE(bus1Offsets.aOffset, bus2Offsets.aOffset);
    values[generatorOffsets.gOffset] = 0.5;
    values[bus1Offsets.aOffset] = 0.0;
    values[bus2Offsets.aOffset] = -0.05;

    const griddyn::OptimizationData data{0.0, values.data(), 0};
    EXPECT_NEAR(branch->dcPowerFlow(bus1, data, mode), 0.5, 1e-12);
    EXPECT_NEAR(branch->dcPowerFlow(bus2, data, mode), -0.5, 1e-12);
    std::vector<double> residuals(root->constraintSize(mode), 0.0);
    root->constraintValue(data, residuals.data(), mode);
    EXPECT_NEAR(residuals[bus1Offsets.constraintOffset], 0.0, 1e-12);
    EXPECT_NEAR(residuals[bus2Offsets.constraintOffset], 0.0, 1e-12);
    EXPECT_NEAR(residuals[branch->offsets.getOffsets(mode).constraintOffset], 0.5, 1e-12);
}

TEST(OptimizationDcFormulationTests, TwoBusDcLinkUsesTapPhaseShiftAndFlowLimitRow)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = gds->findByUserID("link", 1);
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->set("tap", 2.0);
    physicalBranch->set("tapangle", 0.1);

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);
    ASSERT_TRUE(branch->isDcFlowValid());
    ASSERT_EQ(branch->constraintSize(mode), 1);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& branchOffsets = branch->offsets.getOffsets(mode);
    std::vector<double> values(root->objSize(mode), 0.0);
    values[bus1Offsets.aOffset] = 0.2;
    values[bus2Offsets.aOffset] = 0.0;
    const griddyn::OptimizationData data{0.0, values.data(), 0};

    EXPECT_NEAR(branch->dcPowerFlow(bus1, data, mode), 0.5, 1e-12);
    EXPECT_NEAR(branch->dcPowerFlow(bus2, data, mode), -0.5, 1e-12);

    std::vector<double> residuals(root->constraintSize(mode), 0.0);
    root->constraintValue(data, residuals.data(), mode);
    EXPECT_NEAR(residuals[branchOffsets.constraintOffset], 0.5, 1e-12);

    MatrixDataSparse<double> jacobian;
    root->constraintJacobianElements(data, jacobian, mode);
    EXPECT_NEAR(jacobian.at(branchOffsets.constraintOffset, bus1Offsets.aOffset), 5.0, 1e-12);
    EXPECT_NEAR(jacobian.at(branchOffsets.constraintOffset, bus2Offsets.aOffset), -5.0, 1e-12);

    std::vector<double> upperBounds(root->constraintSize(mode), 0.0);
    std::vector<double> lowerBounds(root->constraintSize(mode), 0.0);
    MatrixDataSparse<double> linearConstraints;
    root->getConstraints(data, linearConstraints, upperBounds.data(), lowerBounds.data(), mode);
    EXPECT_NEAR(lowerBounds[branchOffsets.constraintOffset], -0.5, 1e-12);
    EXPECT_NEAR(upperBounds[branchOffsets.constraintOffset], 1.5, 1e-12);
    EXPECT_NEAR(linearConstraints.at(branchOffsets.constraintOffset, bus1Offsets.aOffset),
                5.0,
                1e-12);
}

TEST(OptimizationDcFormulationTests, TwoBusDcLinkUsesAngleLimitRow)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = gds->findByUserID("link", 1);
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->set("tapangle", 0.1);
    physicalBranch->set("minangle", -0.05);
    physicalBranch->set("maxangle", 0.15);

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);
    ASSERT_TRUE(branch->hasDcAngleLimit());
    ASSERT_EQ(branch->constraintSize(mode), 2);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& branchOffsets = branch->offsets.getOffsets(mode);
    std::vector<double> values(root->objSize(mode), 0.0);
    values[bus1Offsets.aOffset] = 0.2;
    values[bus2Offsets.aOffset] = 0.0;
    const griddyn::OptimizationData data{0.0, values.data(), 0};

    std::vector<double> residuals(root->constraintSize(mode), 0.0);
    root->constraintValue(data, residuals.data(), mode);
    EXPECT_NEAR(residuals[branchOffsets.constraintOffset], 1.0, 1e-12);
    EXPECT_NEAR(residuals[branchOffsets.constraintOffset + 1], 0.1, 1e-12);

    std::vector<double> upperBounds(root->constraintSize(mode), 0.0);
    std::vector<double> lowerBounds(root->constraintSize(mode), 0.0);
    MatrixDataSparse<double> linearConstraints;
    root->getConstraints(data, linearConstraints, upperBounds.data(), lowerBounds.data(), mode);
    EXPECT_NEAR(lowerBounds[branchOffsets.constraintOffset + 1], 0.05, 1e-12);
    EXPECT_NEAR(upperBounds[branchOffsets.constraintOffset + 1], 0.25, 1e-12);
    EXPECT_NEAR(linearConstraints.at(branchOffsets.constraintOffset + 1, bus1Offsets.aOffset),
                1.0,
                1e-12);
    EXPECT_NEAR(linearConstraints.at(branchOffsets.constraintOffset + 1, bus2Offsets.aOffset),
                -1.0,
                1e-12);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerActivatesBranchThermalLimit)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBus2 = dynamic_cast<griddyn::GridBus*>(gds->findByUserID("bus", 2));
    auto* physicalBranch = dynamic_cast<griddyn::Link*>(gds->findByUserID("link", 1));
    ASSERT_NE(physicalBus2, nullptr);
    ASSERT_NE(physicalBranch, nullptr);

    auto* secondGenerator = new griddyn::Generator("gen2");
    secondGenerator->setUserID(2);
    secondGenerator->set("pmin", 0.0);
    secondGenerator->set("pmax", 1.0);
    physicalBus2->add(secondGenerator);
    // case2 has a 100 MW system base, so this creates a 0.25 pu flow limit.
    physicalBranch->set("ratinga", 25.0, units::MW);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    auto* generator2 = dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", 2));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(branch, nullptr);
    ASSERT_NE(generator2, nullptr);
    // Make the added generator more expensive than the original case2 generator.
    generator2->set("p1", 400.0);

    const auto& generator2Offsets = generator2->offsets.getOffsets(mode);
    const auto& branchOffsets = branch->offsets.getOffsets(mode);
    ASSERT_EQ(branch->constraintSize(mode), 1);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    double returnTime = -1.0;
    ASSERT_EQ(optimizer.solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.lastSolveResult().message;

    const griddyn::OptimizationData data{0.0, optimizer.values.data(), 0};
    EXPECT_NEAR(branch->dcPowerFlow(bus1, data, mode), 0.25, 1e-8);
    EXPECT_NEAR(optimizer.values[generator2Offsets.gOffset], 0.25, 1e-8);
    EXPECT_NEAR(optimizer.problem().constraintValue(branchOffsets.constraintOffset,
                                                    optimizer.values),
                optimizer.problem().constraintUpperBounds[branchOffsets.constraintOffset],
                1e-8);
    EXPECT_LE(optimizer.lastSolveResult().maximumConstraintViolation, 1e-8);
    EXPECT_LE(optimizer.lastSolveResult().maximumComplementarity, 1e-8);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerActivatesBranchAngleLimit)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = dynamic_cast<griddyn::Link*>(gds->findByUserID("link", 1));
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->set("tapangle", 0.0);
    physicalBranch->set("minangle", -0.05);
    physicalBranch->set("maxangle", 0.05);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);
    ASSERT_TRUE(branch->hasDcAngleLimit());

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& branchOffsets = branch->offsets.getOffsets(mode);
    ASSERT_EQ(branch->constraintSize(mode), 2);

    griddyn::NativeOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.initialize(0.0);
    double returnTime = -1.0;
    ASSERT_EQ(optimizer.solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.lastSolveResult().message;

    const auto angleRow = branchOffsets.constraintOffset + 1;
    EXPECT_NEAR(optimizer.values[bus1Offsets.aOffset] - optimizer.values[bus2Offsets.aOffset],
                0.05,
                1e-8);
    EXPECT_NEAR(optimizer.problem().constraintValue(angleRow, optimizer.values),
                optimizer.problem().constraintUpperBounds[angleRow],
                1e-8);
    EXPECT_LE(optimizer.lastSolveResult().maximumConstraintViolation, 1e-8);
    EXPECT_LE(optimizer.lastSolveResult().maximumComplementarity, 1e-8);
}

TEST(OptimizationDcFormulationTests, DisconnectedDcLinkContributesNoFlowOrLimitRows)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* physicalBranch = dynamic_cast<griddyn::Link*>(gds->findByUserID("link", 1));
    ASSERT_NE(physicalBranch, nullptr);
    physicalBranch->disconnect();

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    ASSERT_NE(branch, nullptr);
    EXPECT_TRUE(branch->isDcFlowValid());
    EXPECT_EQ(branch->constraintSize(mode), 0);
    EXPECT_EQ(root->constraintSize(mode), 3);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    std::vector<double> values(root->objSize(mode), 0.0);
    values[bus1Offsets.aOffset] = 0.2;
    values[bus2Offsets.aOffset] = -0.3;
    const griddyn::OptimizationData data{0.0, values.data(), 0};
    EXPECT_DOUBLE_EQ(branch->dcPowerFlow(bus1, data, mode), 0.0);

    std::vector<double> residuals(root->constraintSize(mode), 0.0);
    root->constraintValue(data, residuals.data(), mode);
    EXPECT_DOUBLE_EQ(residuals[bus1Offsets.constraintOffset], 0.0);
    // The disconnected branch cannot serve bus 2's 50 MW load.
    EXPECT_DOUBLE_EQ(residuals[bus2Offsets.constraintOffset], -0.5);
}

TEST(OptimizationDcFormulationTests, InvalidDcLinkParametersAreDiagnosed)
{
    auto makeCase = [] {
        auto caseData = std::make_unique<griddyn::GridDynOptimization>();
        const auto filePath = makePyPowerCasePath("case2.py");
        griddyn::loadFile(caseData.get(), filePath.string());
        return caseData;
    };

    {
        auto gds = makeCase();
        auto* physicalBranch = gds->findByUserID("link", 1);
        ASSERT_NE(physicalBranch, nullptr);
        physicalBranch->set("x", 1e-14);

        const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
        gds->initializeOptimizationModel(mode);
        auto* root = gds->getOptimizationObject();
        ASSERT_NE(root, nullptr);
        auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
        ASSERT_NE(branch, nullptr);
        EXPECT_FALSE(branch->isDcFlowValid());
        EXPECT_FALSE(branch->hasDcFlowLimit());
        EXPECT_EQ(branch->constraintSize(mode), 0);
    }

    {
        auto gds = makeCase();
        auto* physicalBranch = gds->findByUserID("link", 1);
        ASSERT_NE(physicalBranch, nullptr);
        physicalBranch->set("tap", 0.0);

        const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
        gds->initializeOptimizationModel(mode);
        auto* root = gds->getOptimizationObject();
        ASSERT_NE(root, nullptr);
        auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
        ASSERT_NE(branch, nullptr);
        EXPECT_FALSE(branch->isDcFlowValid());
        EXPECT_FALSE(branch->hasDcFlowLimit());
        EXPECT_EQ(branch->constraintSize(mode), 0);
    }
}

TEST(OptimizationDcFormulationTests, ParallelDcLinksConserveInternalFlow)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    auto* parallel = new griddyn::AcLine(0.0, 0.2, "parallel_link");
    parallel->setUserID(2);
    parallel->set("ratinga", 0.0);
    parallel->set("minangle", -2.0 * griddyn::kPI);
    parallel->set("maxangle", 2.0 * griddyn::kPI);
    parallel->updateBus(dynamic_cast<griddyn::GridBus*>(gds->findByUserID("bus", 1)), 1);
    parallel->updateBus(dynamic_cast<griddyn::GridBus*>(gds->findByUserID("bus", 2)), 2);
    gds->add(parallel);

    const auto mode = makeDcMode(griddyn::LinearityMode::LINEAR);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* branch = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 1));
    auto* parallelOpt = dynamic_cast<griddyn::GridLinkOpt*>(root->findByUserID("link", 2));
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    ASSERT_NE(branch, nullptr);
    ASSERT_NE(parallelOpt, nullptr);
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    EXPECT_EQ(parallelOpt->constraintSize(mode), 0);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);
    std::vector<double> values(root->objSize(mode), 0.0);
    values[bus1Offsets.aOffset] = 0.1;
    values[bus2Offsets.aOffset] = 0.0;
    values[generatorOffsets.gOffset] = 0.5;
    const griddyn::OptimizationData data{0.0, values.data(), 0};

    const double branchFlow = branch->dcPowerFlow(bus1, data, mode);
    const double parallelFlow = parallelOpt->dcPowerFlow(bus1, data, mode);
    EXPECT_NEAR(branchFlow, 1.0, 1e-12);
    EXPECT_NEAR(parallelFlow, 0.5, 1e-12);
    EXPECT_NEAR(branch->dcPowerFlow(bus2, data, mode), -branchFlow, 1e-12);
    EXPECT_NEAR(parallelOpt->dcPowerFlow(bus2, data, mode), -parallelFlow, 1e-12);

    std::vector<double> residuals(root->constraintSize(mode), 0.0);
    root->constraintValue(data, residuals.data(), mode);
    EXPECT_NEAR(residuals[bus1Offsets.constraintOffset] + residuals[bus2Offsets.constraintOffset],
                0.0,
                1e-12);
}

TEST(OptimizationDcFormulationTests, OptimizerInterfaceDrivesTwoBusDcCallbacks)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);

    griddyn::BasicOptimizer optimizer("basic");
    optimizer.setOptimizationData(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);

    ASSERT_LT(bus1Offsets.aOffset, optimizer.values.size());
    ASSERT_LT(bus2Offsets.aOffset, optimizer.values.size());
    ASSERT_LT(generatorOffsets.gOffset, optimizer.values.size());
    optimizer.values[generatorOffsets.gOffset] = 0.5;
    optimizer.values[bus1Offsets.aOffset] = 0.0;
    optimizer.values[bus2Offsets.aOffset] = -0.05;

    EXPECT_EQ(optimizer.loadVariableBounds(0.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer.loadQuadraticObjective(0.0), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer.constraintFunction(0.0, optimizer.val_data(), optimizer.constraint_data()),
              FUNCTION_EXECUTION_SUCCESS);

    EXPECT_NEAR(optimizer.constraintValues[bus1Offsets.constraintOffset], 0.0, 1e-12);
    EXPECT_NEAR(optimizer.constraintValues[bus1Offsets.constraintOffset + 1], 0.0, 1e-12);
    EXPECT_NEAR(optimizer.constraintValues[bus2Offsets.constraintOffset], 0.0, 1e-12);
    EXPECT_NEAR(optimizer.objectiveFunction(0.0, optimizer.val_data()), 75.0, 1e-12);

    ASSERT_EQ(optimizer.gradientFunction(0.0, optimizer.val_data(), optimizer.gradientData()),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(optimizer.gradient[generatorOffsets.gOffset], 200.0, 1e-12);

    auto& jacobian = optimizer.constraintJacobianFunction(0.0, optimizer.val_data());
    EXPECT_NEAR(jacobian.at(bus1Offsets.constraintOffset, generatorOffsets.gOffset), 1.0, 1e-12);
    EXPECT_NEAR(jacobian.at(bus1Offsets.constraintOffset, bus1Offsets.aOffset), -10.0, 1e-12);
    EXPECT_NEAR(jacobian.at(bus1Offsets.constraintOffset, bus2Offsets.aOffset), 10.0, 1e-12);
    EXPECT_NEAR(jacobian.at(bus1Offsets.constraintOffset + 1, bus1Offsets.aOffset), 1.0, 1e-12);
    EXPECT_NEAR(jacobian.at(bus2Offsets.constraintOffset, bus1Offsets.aOffset), 10.0, 1e-12);
    EXPECT_NEAR(jacobian.at(bus2Offsets.constraintOffset, bus2Offsets.aOffset), -10.0, 1e-12);
}

TEST(OptimizationDcFormulationTests, OptimizerInterfaceInitializationLoadsProblemData)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* physicalBus1 = dynamic_cast<griddyn::GridBus*>(bus1->sourceObject());
    auto* physicalBus2 = dynamic_cast<griddyn::GridBus*>(bus2->sourceObject());
    auto* physicalGenerator = dynamic_cast<griddyn::Generator*>(generator->sourceObject());
    ASSERT_NE(physicalBus1, nullptr);
    ASSERT_NE(physicalBus2, nullptr);
    ASSERT_NE(physicalGenerator, nullptr);

    griddyn::BasicOptimizer optimizer("basic");
    optimizer.setOptimizationData(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.set("rtol", 1e-7);
    optimizer.initialize(0.0);

    EXPECT_TRUE(optimizer.isInitialized());
    EXPECT_EQ(optimizer.variableType.size(), optimizer.values.size());
    EXPECT_EQ(optimizer.tolerances.size(), optimizer.values.size());
    EXPECT_EQ(optimizer.get("integer_variables"), 0.0);
    EXPECT_EQ(optimizer.get("binary_variables"), 0.0);

    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);
    EXPECT_NEAR(optimizer.values[bus1Offsets.aOffset], physicalBus1->getAngle(), 1e-12);
    EXPECT_NEAR(optimizer.values[bus2Offsets.aOffset], physicalBus2->getAngle(), 1e-12);
    EXPECT_NEAR(optimizer.values[generatorOffsets.gOffset],
                -physicalGenerator->getRealPower(),
                1e-12);
    EXPECT_NEAR(optimizer.tolerances[generatorOffsets.gOffset], 1e-7, 1e-15);
    EXPECT_EQ(optimizer.variableType[generatorOffsets.gOffset], CONTINUOUS_OBJECTIVE_VARIABLE);
}

TEST(OptimizationDcFormulationTests, TwoBusIntegratedBasicOptimizerDryRunBeforeSolve)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->objSize(mode), 3);
    ASSERT_EQ(root->constraintSize(mode), 4);

    auto optimizer = griddyn::makeOptimizer(gds.get(), mode);
    ASSERT_NE(optimizer, nullptr);
    optimizer->setName("dcopf-dry-run");
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    ASSERT_TRUE(optimizer->isInitialized());
    EXPECT_EQ(optimizer->size(), 3);
    EXPECT_EQ(optimizer->constraintCount(), 4);
    EXPECT_EQ(optimizer->values.size(), 3U);
    EXPECT_EQ(optimizer->lowerBounds.size(), 3U);
    EXPECT_EQ(optimizer->upperBounds.size(), 3U);
    EXPECT_EQ(optimizer->constraintValues.size(), 4U);
    EXPECT_EQ(optimizer->constraintLowerBounds.size(), 4U);
    EXPECT_EQ(optimizer->constraintUpperBounds.size(), 4U);
    EXPECT_EQ(optimizer->gradient.size(), 3U);
    EXPECT_EQ(optimizer->variableType.size(), 3U);
    EXPECT_EQ(optimizer->tolerances.size(), 3U);
    EXPECT_EQ(optimizer->multipliers.size(), 4U);
    EXPECT_EQ(optimizer->linearObjective.points(), 1);
    EXPECT_EQ(optimizer->quadraticObjective.points(), 1);

    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    auto* bus2 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 2));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* physicalGenerator = dynamic_cast<griddyn::Generator*>(generator->sourceObject());
    ASSERT_NE(physicalGenerator, nullptr);
    const auto& bus1Offsets = bus1->offsets.getOffsets(mode);
    const auto& bus2Offsets = bus2->offsets.getOffsets(mode);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);

    SCOPED_TRACE(::testing::Message{} << "bus1 angle offset=" << bus1Offsets.aOffset
                                      << ", bus2 angle offset=" << bus2Offsets.aOffset
                                      << ", generator offset=" << generatorOffsets.gOffset
                                      << ", physical pmin=" << physicalGenerator->getPmin()
                                      << ", physical pmax=" << physicalGenerator->getPmax());
    ASSERT_LT(generatorOffsets.gOffset, optimizer->lowerBounds.size());

    EXPECT_NEAR(optimizer->lowerBounds[generatorOffsets.gOffset], 0.0, 1e-12);
    EXPECT_NEAR(optimizer->upperBounds[generatorOffsets.gOffset], 1.5, 1e-12);
    EXPECT_EQ(optimizer->variableType[bus1Offsets.aOffset], CONTINUOUS_OBJECTIVE_VARIABLE);
    EXPECT_EQ(optimizer->variableType[bus2Offsets.aOffset], CONTINUOUS_OBJECTIVE_VARIABLE);
    EXPECT_EQ(optimizer->variableType[generatorOffsets.gOffset], CONTINUOUS_OBJECTIVE_VARIABLE);

    expectFinite(optimizer->values);
    expectFinite(optimizer->lowerBounds);
    expectFinite(optimizer->upperBounds);
    expectFinite(optimizer->tolerances);

    EXPECT_EQ(optimizer->constraintFunction(0.0,
                                            optimizer->val_data(),
                                            optimizer->constraint_data()),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer->gradientFunction(0.0, optimizer->val_data(), optimizer->gradientData()),
              FUNCTION_EXECUTION_SUCCESS);
    auto& jacobian = optimizer->constraintJacobianFunction(0.0, optimizer->val_data());
    EXPECT_GT(jacobian.size(), 0);
    expectFinite(optimizer->constraintValues);
    expectFinite(optimizer->gradient);

    // The dry-run stage proves the problem can be fully specified and evaluated.
    // Feasibility and optimality are intentionally left for the actual solver.
    optimizer->values[generatorOffsets.gOffset] = 0.5;
    optimizer->values[bus1Offsets.aOffset] = 0.0;
    optimizer->values[bus2Offsets.aOffset] = -0.05;
    ASSERT_EQ(optimizer->constraintFunction(0.0,
                                            optimizer->val_data(),
                                            optimizer->constraint_data()),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(optimizer->constraintValues[bus1Offsets.constraintOffset], 0.0, 1e-12);
    EXPECT_NEAR(optimizer->constraintValues[bus1Offsets.constraintOffset + 1], 0.0, 1e-12);
    EXPECT_NEAR(optimizer->constraintValues[bus2Offsets.constraintOffset], 0.0, 1e-12);
}

TEST(OptimizationDcFormulationTests, TwoBusEconomicDispatchOptimizerRunsProblemSequence)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makePyPowerCasePath("case2.py");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->set("optimizer", "dispatch");
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    auto optimizer = gds->getOptimizerInterface(mode);
    ASSERT_NE(optimizer, nullptr);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    ASSERT_TRUE(optimizer->isInitialized());
    auto* bus1 = dynamic_cast<griddyn::GridBusOpt*>(root->findByUserID("bus", 1));
    ASSERT_NE(bus1, nullptr);
    auto* generator = dynamic_cast<griddyn::GridGenOpt*>(bus1->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* physicalGenerator = dynamic_cast<griddyn::Generator*>(generator->sourceObject());
    ASSERT_NE(physicalGenerator, nullptr);
    const auto& generatorOffsets = generator->offsets.getOffsets(mode);

    physicalGenerator->set("p", 0.25);
    physicalGenerator->set("pset", 0.25);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_NEAR(optimizer->get("dispatch_imbalance"), 0.0, 1e-12);
    EXPECT_NEAR(optimizer->values[generatorOffsets.gOffset], 0.5, 1e-12);
    EXPECT_NEAR(physicalGenerator->getRealPower(), -0.25, 1e-12);
    EXPECT_NEAR(physicalGenerator->getPset(), 0.25, 1e-12);

    EXPECT_EQ(optimizer->writeBack(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(physicalGenerator->getRealPower(), -0.5, 1e-12);
    EXPECT_NEAR(physicalGenerator->getPset(), 0.5, 1e-12);

    EXPECT_NEAR(optimizer->objectiveFunction(0.0, optimizer->val_data()), 75.0, 1e-12);
    EXPECT_EQ(optimizer->constraintFunction(0.0,
                                            optimizer->val_data(),
                                            optimizer->constraint_data()),
              FUNCTION_EXECUTION_SUCCESS);
    expectFinite(optimizer->constraintValues);
    expectFinite(optimizer->gradient);
}

TEST(OptimizationDcFormulationTests, Case9IntegratedBasicOptimizerDryRunBeforeSolve)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case9.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);

    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->aSize(mode), 9);
    EXPECT_EQ(root->genSize(mode), 3);
    EXPECT_EQ(root->objSize(mode), 12);
    EXPECT_EQ(root->constraintSize(mode), 19);

    auto optimizer = griddyn::makeOptimizer(gds.get(), mode);
    ASSERT_NE(optimizer, nullptr);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    EXPECT_TRUE(optimizer->isInitialized());
    EXPECT_EQ(optimizer->size(), 12);
    EXPECT_EQ(optimizer->constraintCount(), 19);
    EXPECT_EQ(optimizer->values.size(), 12U);
    EXPECT_EQ(optimizer->lowerBounds.size(), 12U);
    EXPECT_EQ(optimizer->upperBounds.size(), 12U);
    EXPECT_EQ(optimizer->constraintValues.size(), 19U);
    EXPECT_EQ(optimizer->constraintLowerBounds.size(), 19U);
    EXPECT_EQ(optimizer->constraintUpperBounds.size(), 19U);
    EXPECT_EQ(optimizer->gradient.size(), 12U);
    EXPECT_EQ(optimizer->variableType.size(), 12U);
    EXPECT_EQ(optimizer->tolerances.size(), 12U);
    EXPECT_EQ(optimizer->multipliers.size(), 19U);
    EXPECT_EQ(optimizer->linearObjective.points(), 3);
    EXPECT_EQ(optimizer->quadraticObjective.points(), 3);
    EXPECT_EQ(optimizer->get("integer_variables"), 0.0);
    EXPECT_EQ(optimizer->get("binary_variables"), 0.0);

    expectFinite(optimizer->values);
    expectFinite(optimizer->lowerBounds);
    expectFinite(optimizer->upperBounds);
    expectFinite(optimizer->tolerances);

    EXPECT_TRUE(std::isfinite(optimizer->objectiveFunction(0.0, optimizer->val_data())));
    EXPECT_EQ(optimizer->constraintFunction(0.0,
                                            optimizer->val_data(),
                                            optimizer->constraint_data()),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer->gradientFunction(0.0, optimizer->val_data(), optimizer->gradientData()),
              FUNCTION_EXECUTION_SUCCESS);
    auto& jacobian = optimizer->constraintJacobianFunction(0.0, optimizer->val_data());
    EXPECT_GT(jacobian.size(), 0);
    EXPECT_TRUE(std::isfinite(optimizer->objectiveFunction(0.0, optimizer->val_data())));
    expectFinite(optimizer->constraintValues);
    expectFinite(optimizer->gradient);
}

#ifdef GRIDDYN_ENABLE_HIGHS
TEST(OptimizationDcFormulationTests, HighsOptimizerScalingPolicy)
{
    griddyn::HighsOptimizer optimizer;

    EXPECT_EQ(optimizer.scalingMode(), griddyn::HighsScalingMode::AUTO);
    EXPECT_EQ(optimizer.scalingVariableThreshold(), 6000);
    EXPECT_EQ(optimizer.get("scaling_mode"), 2.0);
    EXPECT_EQ(optimizer.get("scaling_threshold"), 6000.0);
    EXPECT_EQ(optimizer.get("scaling_requested"), 0.0);

    ASSERT_EQ(optimizer.allocate(5999), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer.get("scaling_requested"), 0.0);
    ASSERT_EQ(optimizer.allocate(6000), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(optimizer.get("scaling_requested"), 1.0);

    optimizer.set("scaling", "no_scaling");
    EXPECT_EQ(optimizer.scalingMode(), griddyn::HighsScalingMode::NO_SCALING);
    EXPECT_EQ(optimizer.get("scaling_requested"), 0.0);

    optimizer.set("scaling_mode", "scaling");
    EXPECT_EQ(optimizer.scalingMode(), griddyn::HighsScalingMode::SCALING);
    EXPECT_EQ(optimizer.get("scaling_requested"), 1.0);

    optimizer.set("scaling", "auto");
    optimizer.set("scaling_threshold", 12000.0);
    EXPECT_EQ(optimizer.scalingMode(), griddyn::HighsScalingMode::AUTO);
    EXPECT_EQ(optimizer.scalingVariableThreshold(), 12000);
    EXPECT_EQ(optimizer.get("scaling_requested"), 0.0);

    optimizer.set("scaling", 1.0);
    EXPECT_EQ(optimizer.scalingMode(), griddyn::HighsScalingMode::SCALING);
    EXPECT_THROW(optimizer.set("scaling", "invalid"), std::invalid_argument);
    EXPECT_THROW(optimizer.set("scaling_threshold", 0.5), std::invalid_argument);
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase9LikeNativeOptimizer)
{
    auto factoryOptimizer = griddyn::makeOptimizer("highs");
    ASSERT_NE(factoryOptimizer, nullptr);
    EXPECT_NE(dynamic_cast<griddyn::HighsOptimizer*>(factoryOptimizer.get()), nullptr);

    auto nativeGrid = std::make_unique<griddyn::GridDynOptimization>();
    auto highsGrid = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case9.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(nativeGrid.get(), filePath.string());
    griddyn::loadFile(highsGrid.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    nativeGrid->initializeOptimizationModel(mode);
    highsGrid->initializeOptimizationModel(mode);
    auto* nativeRoot = nativeGrid->getOptimizationObject();
    auto* highsRoot = highsGrid->getOptimizationObject();
    ASSERT_NE(nativeRoot, nullptr);
    ASSERT_NE(highsRoot, nullptr);

    griddyn::NativeOptimizer nativeOptimizer(nativeGrid.get(), mode);
    griddyn::HighsOptimizer highsOptimizer(highsGrid.get(), mode);
    ASSERT_EQ(nativeOptimizer.allocate(nativeRoot->objSize(mode), nativeRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(highsOptimizer.allocate(highsRoot->objSize(mode), highsRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    nativeOptimizer.setMaxNonZeros(nativeRoot->objSize(mode) * nativeRoot->constraintSize(mode));
    highsOptimizer.setMaxNonZeros(highsRoot->objSize(mode) * highsRoot->constraintSize(mode));
    nativeOptimizer.initialize(0.0);
    highsOptimizer.initialize(0.0);

    double nativeReturnTime = -1.0;
    double highsReturnTime = -1.0;
    ASSERT_EQ(nativeOptimizer.solve(0.0, nativeReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << nativeOptimizer.lastSolveResult().message;
    ASSERT_EQ(highsOptimizer.solve(0.0, highsReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << highsOptimizer.lastSolveResult().message;
    ASSERT_EQ(nativeOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    ASSERT_EQ(highsOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    EXPECT_EQ(highsReturnTime, nativeReturnTime);
    EXPECT_EQ(highsOptimizer.get("highs_solver"), 1.0);
    EXPECT_EQ(highsOptimizer.get("native_solver"), 0.0);
    ASSERT_EQ(highsOptimizer.values.size(), nativeOptimizer.values.size());
    for (std::size_t index = 0; index < nativeOptimizer.values.size(); ++index) {
        EXPECT_NEAR(highsOptimizer.values[index], nativeOptimizer.values[index], 1e-6)
            << "decision variable " << index;
    }
    EXPECT_NEAR(highsOptimizer.lastSolveResult().objectiveValue,
                nativeOptimizer.lastSolveResult().objectiveValue,
                1e-5);
    EXPECT_LE(highsOptimizer.lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(highsOptimizer.lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(highsOptimizer.get("solution_valid"), 1.0);
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase118LikeNativeOptimizer)
{
    auto nativeGrid = std::make_unique<griddyn::GridDynOptimization>();
    auto highsGrid = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case118.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(nativeGrid.get(), filePath.string());
    griddyn::loadFile(highsGrid.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    nativeGrid->initializeOptimizationModel(mode);
    highsGrid->initializeOptimizationModel(mode);
    auto* nativeRoot = nativeGrid->getOptimizationObject();
    auto* highsRoot = highsGrid->getOptimizationObject();
    ASSERT_NE(nativeRoot, nullptr);
    ASSERT_NE(highsRoot, nullptr);

    griddyn::NativeOptimizer nativeOptimizer(nativeGrid.get(), mode);
    griddyn::HighsOptimizer highsOptimizer(highsGrid.get(), mode);
    ASSERT_EQ(nativeOptimizer.allocate(nativeRoot->objSize(mode), nativeRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(highsOptimizer.allocate(highsRoot->objSize(mode), highsRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    nativeOptimizer.setMaxNonZeros(nativeRoot->objSize(mode) * nativeRoot->constraintSize(mode));
    highsOptimizer.setMaxNonZeros(highsRoot->objSize(mode) * highsRoot->constraintSize(mode));
    nativeOptimizer.initialize(0.0);
    highsOptimizer.initialize(0.0);

    double nativeReturnTime = -1.0;
    double highsReturnTime = -1.0;
    const auto nativeStart = std::chrono::steady_clock::now();
    ASSERT_EQ(nativeOptimizer.solve(0.0, nativeReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << nativeOptimizer.lastSolveResult().message;
    const auto nativeStop = std::chrono::steady_clock::now();
    const auto highsStart = std::chrono::steady_clock::now();
    ASSERT_EQ(highsOptimizer.solve(0.0, highsReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << highsOptimizer.lastSolveResult().message;
    const auto highsStop = std::chrono::steady_clock::now();
    ASSERT_EQ(nativeOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    ASSERT_EQ(highsOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    EXPECT_EQ(highsReturnTime, nativeReturnTime);
    ASSERT_EQ(highsOptimizer.values.size(), nativeOptimizer.values.size());
    for (std::size_t index = 0; index < nativeOptimizer.values.size(); ++index) {
        EXPECT_NEAR(highsOptimizer.values[index], nativeOptimizer.values[index], 1e-4)
            << "decision variable " << index;
    }
    EXPECT_NEAR(highsOptimizer.lastSolveResult().objectiveValue,
                nativeOptimizer.lastSolveResult().objectiveValue,
                1e-3);
    EXPECT_LE(highsOptimizer.lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(highsOptimizer.lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(highsOptimizer.get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case118_native_solve_ms",
                   std::to_string(milliseconds(nativeStop - nativeStart)));
    RecordProperty("case118_highs_solve_ms", std::to_string(milliseconds(highsStop - highsStart)));
    RecordProperty("case118_highs_iterations",
                   std::to_string(highsOptimizer.lastSolveResult().iterationCount));
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCaseIllinois200LikeNativeOptimizer)
{
    auto nativeGrid = std::make_unique<griddyn::GridDynOptimization>();
    auto highsGrid = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case_illinois200.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(nativeGrid.get(), filePath.string());
    griddyn::loadFile(highsGrid.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    nativeGrid->initializeOptimizationModel(mode);
    highsGrid->initializeOptimizationModel(mode);
    auto* nativeRoot = nativeGrid->getOptimizationObject();
    auto* highsRoot = highsGrid->getOptimizationObject();
    ASSERT_NE(nativeRoot, nullptr);
    ASSERT_NE(highsRoot, nullptr);

    griddyn::NativeOptimizer nativeOptimizer(nativeGrid.get(), mode);
    griddyn::HighsOptimizer highsOptimizer(highsGrid.get(), mode);
    ASSERT_EQ(nativeOptimizer.allocate(nativeRoot->objSize(mode), nativeRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(highsOptimizer.allocate(highsRoot->objSize(mode), highsRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    nativeOptimizer.setMaxNonZeros(nativeRoot->objSize(mode) * nativeRoot->constraintSize(mode));
    highsOptimizer.setMaxNonZeros(highsRoot->objSize(mode) * highsRoot->constraintSize(mode));
    nativeOptimizer.initialize(0.0);
    highsOptimizer.initialize(0.0);

    double nativeReturnTime = -1.0;
    double highsReturnTime = -1.0;
    const auto nativeStart = std::chrono::steady_clock::now();
    ASSERT_EQ(nativeOptimizer.solve(0.0, nativeReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << nativeOptimizer.lastSolveResult().message;
    const auto nativeStop = std::chrono::steady_clock::now();
    const auto highsStart = std::chrono::steady_clock::now();
    ASSERT_EQ(highsOptimizer.solve(0.0, highsReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << highsOptimizer.lastSolveResult().message;
    const auto highsStop = std::chrono::steady_clock::now();

    ASSERT_EQ(nativeOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    ASSERT_EQ(highsOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    EXPECT_EQ(highsReturnTime, nativeReturnTime);
    ASSERT_EQ(highsOptimizer.values.size(), nativeOptimizer.values.size());
    for (std::size_t index = 0; index < nativeOptimizer.values.size(); ++index) {
        EXPECT_NEAR(highsOptimizer.values[index], nativeOptimizer.values[index], 1e-4)
            << "decision variable " << index;
    }
    EXPECT_NEAR(highsOptimizer.lastSolveResult().objectiveValue,
                nativeOptimizer.lastSolveResult().objectiveValue,
                1e-2);
    EXPECT_LE(highsOptimizer.lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(highsOptimizer.lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(highsOptimizer.get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case_illinois200_native_solve_ms",
                   std::to_string(milliseconds(nativeStop - nativeStart)));
    RecordProperty("case_illinois200_highs_solve_ms",
                   std::to_string(milliseconds(highsStop - highsStart)));
    RecordProperty("case_illinois200_highs_iterations",
                   std::to_string(highsOptimizer.lastSolveResult().iterationCount));
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase300)
{
    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case300.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::HighsOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    const auto solveStart = std::chrono::steady_clock::now();
    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    const auto solveStop = std::chrono::steady_clock::now();

    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_TRUE(optimizer->problem().valid);
    EXPECT_GT(optimizer->problem().variableCount, 300U);
    EXPECT_GT(optimizer->problem().constraintCount, 300U);
    EXPECT_GT(optimizer->problem().constraintMatrix.size(), 0U);
    expectFinite(optimizer->problem().constraintMatrix);
    expectFinite(optimizer->values);
    EXPECT_TRUE(std::isfinite(optimizer->lastSolveResult().objectiveValue));
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case300_load_ms", std::to_string(milliseconds(loadStop - loadStart)));
    RecordProperty("case300_setup_ms", std::to_string(milliseconds(setupStop - setupStart)));
    RecordProperty("case300_solve_ms", std::to_string(milliseconds(solveStop - solveStart)));
    RecordProperty("case300_variables", std::to_string(optimizer->problem().variableCount));
    RecordProperty("case300_constraints", std::to_string(optimizer->problem().constraintCount));
    RecordProperty("case300_jacobian_nnz",
                   std::to_string(optimizer->problem().constraintMatrix.size()));
    const auto sparseMatrixBytes =
        optimizer->problem().constraintMatrix.rowStarts.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.columnIndices.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.values.capacity() * sizeof(double);
    RecordProperty("case300_sparse_matrix_bytes", std::to_string(sparseMatrixBytes));
    RecordProperty("case300_highs_iterations",
                   std::to_string(optimizer->lastSolveResult().iterationCount));
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase300WithAndWithoutScaling)
{
    auto noScalingGrid = std::make_unique<griddyn::GridDynOptimization>();
    auto scalingGrid = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case300.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(noScalingGrid.get(), filePath.string());
    griddyn::loadFile(scalingGrid.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    noScalingGrid->initializeOptimizationModel(mode);
    scalingGrid->initializeOptimizationModel(mode);
    auto* noScalingRoot = noScalingGrid->getOptimizationObject();
    auto* scalingRoot = scalingGrid->getOptimizationObject();
    ASSERT_NE(noScalingRoot, nullptr);
    ASSERT_NE(scalingRoot, nullptr);

    griddyn::HighsOptimizer noScalingOptimizer(noScalingGrid.get(), mode);
    griddyn::HighsOptimizer scalingOptimizer(scalingGrid.get(), mode);
    ASSERT_EQ(noScalingOptimizer.allocate(noScalingRoot->objSize(mode),
                                          noScalingRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(scalingOptimizer.allocate(scalingRoot->objSize(mode),
                                        scalingRoot->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    noScalingOptimizer.setMaxNonZeros(noScalingRoot->objSize(mode) *
                                      noScalingRoot->constraintSize(mode));
    scalingOptimizer.setMaxNonZeros(scalingRoot->objSize(mode) * scalingRoot->constraintSize(mode));
    noScalingOptimizer.set("scaling", "no_scaling");
    scalingOptimizer.set("scaling", "scaling");
    noScalingOptimizer.initialize(0.0);
    scalingOptimizer.initialize(0.0);

    double noScalingReturnTime = -1.0;
    double scalingReturnTime = -1.0;
    ASSERT_EQ(noScalingOptimizer.solve(0.0, noScalingReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << noScalingOptimizer.lastSolveResult().message;
    ASSERT_EQ(scalingOptimizer.solve(0.0, scalingReturnTime), FUNCTION_EXECUTION_SUCCESS)
        << scalingOptimizer.lastSolveResult().message;

    ASSERT_EQ(noScalingOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    ASSERT_EQ(scalingOptimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL);
    EXPECT_EQ(noScalingReturnTime, scalingReturnTime);
    EXPECT_EQ(noScalingOptimizer.get("scaling_applied"), 0.0);
    EXPECT_EQ(scalingOptimizer.get("scaling_applied"), 1.0);
    EXPECT_EQ(noScalingOptimizer.values.size(), scalingOptimizer.values.size());
    double maximumValueDifference = 0.0;
    for (std::size_t index = 0; index < noScalingOptimizer.values.size(); ++index) {
        maximumValueDifference =
            (std::max)(maximumValueDifference,
                       std::abs(noScalingOptimizer.values[index] - scalingOptimizer.values[index]));
    }
    EXPECT_LT(maximumValueDifference, 1e-3);
    EXPECT_NEAR(noScalingOptimizer.lastSolveResult().objectiveValue,
                scalingOptimizer.lastSolveResult().objectiveValue,
                1e-5);
    EXPECT_LE(noScalingOptimizer.lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(scalingOptimizer.lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(noScalingOptimizer.lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_LE(scalingOptimizer.lastSolveResult().maximumBoundViolation, 1e-7);
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase1354Pegase)
{
    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case1354pegase.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::HighsOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    const auto solveStart = std::chrono::steady_clock::now();
    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    const auto solveStop = std::chrono::steady_clock::now();

    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_TRUE(optimizer->problem().valid);
    EXPECT_GT(optimizer->problem().variableCount, 1000U);
    EXPECT_GT(optimizer->problem().constraintCount, 1000U);
    EXPECT_GT(optimizer->problem().constraintMatrix.size(), 0U);
    expectFinite(optimizer->problem().constraintMatrix);
    expectFinite(optimizer->values);
    EXPECT_TRUE(std::isfinite(optimizer->lastSolveResult().objectiveValue));
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case1354pegase_load_ms", std::to_string(milliseconds(loadStop - loadStart)));
    RecordProperty("case1354pegase_setup_ms", std::to_string(milliseconds(setupStop - setupStart)));
    RecordProperty("case1354pegase_solve_ms", std::to_string(milliseconds(solveStop - solveStart)));
    RecordProperty("case1354pegase_variables", std::to_string(optimizer->problem().variableCount));
    RecordProperty("case1354pegase_constraints",
                   std::to_string(optimizer->problem().constraintCount));
    RecordProperty("case1354pegase_jacobian_nnz",
                   std::to_string(optimizer->problem().constraintMatrix.size()));
    const auto sparseMatrixBytes =
        optimizer->problem().constraintMatrix.rowStarts.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.columnIndices.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.values.capacity() * sizeof(double);
    RecordProperty("case1354pegase_sparse_matrix_bytes", std::to_string(sparseMatrixBytes));
    RecordProperty("case1354pegase_highs_iterations",
                   std::to_string(optimizer->lastSolveResult().iterationCount));
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase2383wp)
{
    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case2383wp.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::HighsOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    const auto solveStart = std::chrono::steady_clock::now();
    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    const auto solveStop = std::chrono::steady_clock::now();

    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_TRUE(optimizer->problem().valid);
    EXPECT_GT(optimizer->problem().variableCount, 2000U);
    EXPECT_GT(optimizer->problem().constraintCount, 2000U);
    EXPECT_GT(optimizer->problem().constraintMatrix.size(), 0U);
    expectFinite(optimizer->problem().constraintMatrix);
    expectFinite(optimizer->values);
    EXPECT_TRUE(std::isfinite(optimizer->lastSolveResult().objectiveValue));
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case2383wp_load_ms", std::to_string(milliseconds(loadStop - loadStart)));
    RecordProperty("case2383wp_setup_ms", std::to_string(milliseconds(setupStop - setupStart)));
    RecordProperty("case2383wp_solve_ms", std::to_string(milliseconds(solveStop - solveStart)));
    RecordProperty("case2383wp_variables", std::to_string(optimizer->problem().variableCount));
    RecordProperty("case2383wp_constraints", std::to_string(optimizer->problem().constraintCount));
    RecordProperty("case2383wp_jacobian_nnz",
                   std::to_string(optimizer->problem().constraintMatrix.size()));
    const auto sparseMatrixBytes =
        optimizer->problem().constraintMatrix.rowStarts.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.columnIndices.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.values.capacity() * sizeof(double);
    RecordProperty("case2383wp_sparse_matrix_bytes", std::to_string(sparseMatrixBytes));
    RecordProperty("case2383wp_highs_iterations",
                   std::to_string(optimizer->lastSolveResult().iterationCount));
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase6468rte)
{
    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case6468rte.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::HighsOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    const auto solveStart = std::chrono::steady_clock::now();
    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    const auto solveStop = std::chrono::steady_clock::now();

    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_TRUE(optimizer->problem().valid);
    EXPECT_EQ(optimizer->problem().variableCount, 6867U);
    EXPECT_EQ(optimizer->problem().constraintCount, 8782U);
    EXPECT_EQ(optimizer->problem().constraintMatrix.size(), 27624U);
    expectFinite(optimizer->problem().constraintMatrix);
    expectFinite(optimizer->values);
    EXPECT_TRUE(std::isfinite(optimizer->lastSolveResult().objectiveValue));
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case6468rte_load_ms", std::to_string(milliseconds(loadStop - loadStart)));
    RecordProperty("case6468rte_setup_ms", std::to_string(milliseconds(setupStop - setupStart)));
    RecordProperty("case6468rte_solve_ms", std::to_string(milliseconds(solveStop - solveStart)));
    RecordProperty("case6468rte_variables", std::to_string(optimizer->problem().variableCount));
    RecordProperty("case6468rte_constraints", std::to_string(optimizer->problem().constraintCount));
    RecordProperty("case6468rte_jacobian_nnz",
                   std::to_string(optimizer->problem().constraintMatrix.size()));
    const auto sparseMatrixBytes =
        optimizer->problem().constraintMatrix.rowStarts.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.columnIndices.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.values.capacity() * sizeof(double);
    RecordProperty("case6468rte_sparse_matrix_bytes", std::to_string(sparseMatrixBytes));
    RecordProperty("case6468rte_highs_iterations",
                   std::to_string(optimizer->lastSolveResult().iterationCount));
}

TEST(OptimizationDcFormulationTests, HighsOptimizerSolvesCase13659Pegase)
{
    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case13659pegase.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    griddyn::HighsOptimizer optimizer(gds.get(), mode);
    ASSERT_EQ(optimizer.allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer.set("scaling", "auto");
    EXPECT_EQ(optimizer.get("scaling_requested"), 1.0);
    optimizer.initialize(0.0);

    const auto solveStart = std::chrono::steady_clock::now();
    double returnTime = -1.0;
    ASSERT_EQ(optimizer.solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer.lastSolveResult().message;
    const auto solveStop = std::chrono::steady_clock::now();

    ASSERT_EQ(optimizer.lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer.lastSolveResult().message;
    EXPECT_EQ(returnTime, 0.0);
    EXPECT_EQ(optimizer.get("scaling_applied"), 1.0);
    EXPECT_TRUE(optimizer.problem().valid);
    EXPECT_GT(optimizer.problem().variableCount, 13000U);
    EXPECT_GT(optimizer.problem().constraintCount, 13000U);
    EXPECT_GT(optimizer.problem().constraintMatrix.size(), 0U);
    expectFinite(optimizer.problem().constraintMatrix);
    expectFinite(optimizer.values);
    EXPECT_TRUE(std::isfinite(optimizer.lastSolveResult().objectiveValue));
    EXPECT_LE(optimizer.lastSolveResult().maximumConstraintViolation, 1e-6);
    EXPECT_LE(optimizer.lastSolveResult().maximumBoundViolation, 1e-6);
    EXPECT_EQ(optimizer.get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case13659pegase_load_ms", std::to_string(milliseconds(loadStop - loadStart)));
    RecordProperty("case13659pegase_setup_ms",
                   std::to_string(milliseconds(setupStop - setupStart)));
    RecordProperty("case13659pegase_solve_ms",
                   std::to_string(milliseconds(solveStop - solveStart)));
    RecordProperty("case13659pegase_variables", std::to_string(optimizer.problem().variableCount));
    RecordProperty("case13659pegase_constraints",
                   std::to_string(optimizer.problem().constraintCount));
    RecordProperty("case13659pegase_jacobian_nnz",
                   std::to_string(optimizer.problem().constraintMatrix.size()));
    RecordProperty("case13659pegase_highs_iterations",
                   std::to_string(optimizer.lastSolveResult().iterationCount));
}

#endif

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCase9AgainstPypowerReference)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case9.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    auto* gen1 = dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", 1));
    auto* gen2 = dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", 2));
    auto* gen3 = dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", 3));
    ASSERT_NE(gen1, nullptr);
    ASSERT_NE(gen2, nullptr);
    ASSERT_NE(gen3, nullptr);
    const auto gen1Offset = gen1->offsets.getOffsets(mode).gOffset;
    const auto gen2Offset = gen2->offsets.getOffsets(mode).gOffset;
    const auto gen3Offset = gen3->offsets.getOffsets(mode).gOffset;

    // This is the standard PYPOWER/MATPOWER case9 DC-OPF dispatch.  The
    // expected values are expressed in GridDyn per-unit variables.  All
    // three generators remain inside their limits and have equal marginal
    // cost for the 315 MW system demand.
    const double expectedGen1 = 0.8656449793155322;
    const double expectedGen2 = 1.3437758555848063;
    const double expectedGen3 = 0.9405791650996616;
    EXPECT_NEAR(optimizer->values[gen1Offset], expectedGen1, 1e-7);
    EXPECT_NEAR(optimizer->values[gen2Offset], expectedGen2, 1e-7);
    EXPECT_NEAR(optimizer->values[gen3Offset], expectedGen3, 1e-7);
    EXPECT_NEAR(optimizer->values[gen1Offset] + optimizer->values[gen2Offset] +
                    optimizer->values[gen3Offset],
                3.15,
                1e-8);
    EXPECT_NEAR(optimizer->lastSolveResult().objectiveValue, 5216.026607747273, 1e-5);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCase14AgainstPypowerReference)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case14.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    // Standard PYPOWER/MATPOWER case14 DC-OPF reference, expressed in
    // GridDyn per-unit variables.  The two lower-cost generators supply the
    // 259 MW load; the three generators with a 40 $/MW linear term remain at
    // their zero lower bounds.
    const std::vector<double> expectedDispatch{
        2.209676945643475, 0.3803230543565251, 0.0, 0.0, 0.0};
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        const auto offset = generator->offsets.getOffsets(mode).gOffset;
        EXPECT_NEAR(optimizer->values[offset], expectedDispatch[index], 1e-7);
    }
    EXPECT_NEAR(optimizer->lastSolveResult().objectiveValue, 7642.5917769585, 1e-5);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCase39AgainstPypowerReference)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case39.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    // Standard PYPOWER/MATPOWER case39 DC-OPF reference, expressed in
    // GridDyn per-unit variables.  The identical quadratic costs make the
    // dispatch unique while the network limits place generators 2, 4, 5,
    // 7, and 8 at their upper bounds.
    const std::vector<double> expectedDispatch{6.6084600006,
                                               6.4599999991,
                                               6.6084600005,
                                               6.5199999986,
                                               5.0799999999,
                                               6.6084600002,
                                               5.7999999998,
                                               5.6399999999,
                                               6.6084600006,
                                               6.6084600008};
    double dispatchTotal = 0.0;
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        const auto offset = generator->offsets.getOffsets(mode).gOffset;
        EXPECT_NEAR(optimizer->values[offset], expectedDispatch[index], 1e-6);
        dispatchTotal += optimizer->values[offset];
    }
    EXPECT_NEAR(dispatchTotal, 62.5423000000, 1e-7);
    EXPECT_NEAR(optimizer->lastSolveResult().objectiveValue, 41263.940785927334, 1e-4);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCase57AgainstPypowerReference)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case57.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    // Standard IEEE-57 PYPOWER/MATPOWER DC-OPF reference, expressed in
    // GridDyn per-unit variables.  The repository case uses the same
    // topology and generator data, with its higher-precision cost entries.
    const std::vector<double> expectedDispatch{1.3946094836,
                                               0.8193132918,
                                               0.4327725317,
                                               0.8193132918,
                                               4.8686909866,
                                               0.8193132918,
                                               3.3539871225};
    double dispatchTotal = 0.0;
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        const auto offset = generator->offsets.getOffsets(mode).gOffset;
        EXPECT_NEAR(optimizer->values[offset], expectedDispatch[index], 1e-6);
        dispatchTotal += optimizer->values[offset];
    }
    EXPECT_NEAR(dispatchTotal, 12.5080000000, 1e-7);
    EXPECT_NEAR(optimizer->lastSolveResult().objectiveValue, 41006.73694205556, 1e-4);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);
}

TEST(OptimizationDcFormulationTests, NativeCase57WriteBackThenPowerflow)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case57.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    gds->setFlag("no_powerflow_error_recovery");
    ASSERT_EQ(gds->pFlowInitialize(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    // This is the same dispatch used by the case57 PYPOWER/MATPOWER
    // comparison test, expressed in GridDyn per-unit variables.
    const std::vector<double> expectedDispatch{1.3946094836,
                                               0.8193132918,
                                               0.4327725317,
                                               0.8193132918,
                                               4.8686909866,
                                               0.8193132918,
                                               3.3539871225};
    std::vector<double> originalPset(expectedDispatch.size());
    std::vector<double> originalRealPower(expectedDispatch.size());
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        const auto offset = generator->offsets.getOffsets(mode).gOffset;
        EXPECT_NEAR(optimizer->values[offset], expectedDispatch[index], 1e-6);
        auto* physicalGenerator = dynamic_cast<griddyn::Generator*>(generator->sourceObject());
        ASSERT_NE(physicalGenerator, nullptr);
        originalPset[index] = physicalGenerator->getPset();
        originalRealPower[index] = physicalGenerator->getRealPower();
    }

    // The optimization result is staged until this explicit commit.  Verify
    // that the physical generator values have not changed during solve.
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        auto* physicalGenerator = dynamic_cast<griddyn::Generator*>(generator->sourceObject());
        ASSERT_NE(physicalGenerator, nullptr);
        EXPECT_NEAR(physicalGenerator->getPset(), originalPset[index], 1e-12);
        EXPECT_NEAR(physicalGenerator->getRealPower(), originalRealPower[index], 1e-12);
    }

    ASSERT_EQ(optimizer->writeBack(), FUNCTION_EXECUTION_SUCCESS);
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        auto* physicalGenerator = dynamic_cast<griddyn::Generator*>(generator->sourceObject());
        ASSERT_NE(physicalGenerator, nullptr);
        EXPECT_NEAR(physicalGenerator->getPset(), expectedDispatch[index], 1e-6);
        // Generator::getRealPower() is the signed network injection, so a
        // positive optimizer Pg appears as a negative physical injection.
        EXPECT_NEAR(physicalGenerator->getRealPower(), -expectedDispatch[index], 1e-6);
    }

    // Reinitialize and run the physical power-flow model using the committed
    // generator setpoints.  The AC result need not equal the DC angles or
    // dispatch exactly, but it must be a finite, converged physical state.
    ASSERT_EQ(gds->pFlowInitialize(), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(gds->powerflow(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(gds->currentProcessState(),
              griddyn::GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    std::vector<double> voltages;
    std::vector<double> angles;
    gds->getVoltage(voltages);
    gds->getAngle(angles);
    ASSERT_EQ(voltages.size(), 57U);
    ASSERT_EQ(angles.size(), 57U);
    expectFinite(voltages);
    expectFinite(angles);
    for (const auto voltage : voltages) {
        EXPECT_GT(voltage, 0.8);
        EXPECT_LT(voltage, 1.2);
    }
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCase118AgainstPypowerReference)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case118.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    // IEEE-118 PYPOWER/MATPOWER DC-OPF reference, expressed in GridDyn
    // per-unit variables.  The repository case has the same 54-generator
    // quadratic cost data and no effective thermal branch limits.
    const std::vector<double> expectedDispatch{0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               4.36080779215294,
                                               0.82370813646186,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               2.13195047190153,
                                               3.04287476346594,
                                               0.0,
                                               0.06783478774674,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.18412299565456,
                                               1.97689953220737,
                                               0.46515283144594,
                                               0.0,
                                               0.0,
                                               1.50205236009053,
                                               1.55050943566184,
                                               0.0,
                                               3.78905742899775,
                                               3.79874811463082,
                                               5.00426919389443,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               4.62245625219357,
                                               0.0,
                                               0.03876273589601,
                                               5.88224516435594,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               2.44205236009053,
                                               0.38762735891883,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.0,
                                               0.34886462274832,
                                               0.0,
                                               0.0,
                                               0.0};
    double dispatchTotal = 0.0;
    for (std::size_t index = 0; index < expectedDispatch.size(); ++index) {
        const auto generatorId = static_cast<int>(index + 1);
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        const auto offset = generator->offsets.getOffsets(mode).gOffset;
        EXPECT_NEAR(optimizer->values[offset], expectedDispatch[index], 1e-5);
        dispatchTotal += optimizer->values[offset];
    }
    EXPECT_NEAR(dispatchTotal, 42.42, 1e-7);
    EXPECT_NEAR(optimizer->lastSolveResult().objectiveValue, 125947.88141815213, 1e-3);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCaseIllinois200AsScaleProbe)
{
    const auto loadStart = std::chrono::steady_clock::now();
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case_illinois200.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());
    const auto loadStop = std::chrono::steady_clock::now();

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    const auto setupStart = std::chrono::steady_clock::now();
    gds->initializeOptimizationModel(mode);
    const auto setupStop = std::chrono::steady_clock::now();
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    const auto solveStart = std::chrono::steady_clock::now();
    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    const auto solveStop = std::chrono::steady_clock::now();

    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;
    EXPECT_TRUE(optimizer->problem().valid);
    EXPECT_GT(optimizer->problem().variableCount, 200U);
    EXPECT_GT(optimizer->problem().constraintCount, 200U);
    expectFinite(optimizer->problem().constraintMatrix);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-6);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-6);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);

    const auto milliseconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    RecordProperty("case_illinois200_load_ms", std::to_string(milliseconds(loadStop - loadStart)));
    RecordProperty("case_illinois200_setup_ms",
                   std::to_string(milliseconds(setupStop - setupStart)));
    RecordProperty("case_illinois200_solve_ms",
                   std::to_string(milliseconds(solveStop - solveStart)));
    RecordProperty("case_illinois200_variables",
                   std::to_string(optimizer->problem().variableCount));
    RecordProperty("case_illinois200_constraints",
                   std::to_string(optimizer->problem().constraintCount));
    const auto sparseMatrixBytes =
        optimizer->problem().constraintMatrix.rowStarts.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.columnIndices.capacity() * sizeof(std::size_t) +
        optimizer->problem().constraintMatrix.values.capacity() * sizeof(double);
    RecordProperty("case_illinois200_sparse_matrix_bytes", std::to_string(sparseMatrixBytes));
    RecordProperty("case_illinois200_iterations",
                   std::to_string(optimizer->lastSolveResult().iterationCount));
    RecordProperty("case_illinois200_active_set_size",
                   std::to_string(optimizer->lastSolveResult().activeSetSize));
}

TEST(OptimizationDcFormulationTests, NativeOptimizerSolvesCase89Pegase)
{
    auto gds = std::make_unique<griddyn::GridDynOptimization>();
    const auto filePath = makeValidationCasePath("case89pegase.m");

    ASSERT_TRUE(std::filesystem::exists(filePath));
    griddyn::loadFile(gds.get(), filePath.string());

    const auto mode = makeDcMode(griddyn::LinearityMode::QUADRATIC);
    gds->initializeOptimizationModel(mode);
    auto* root = gds->getOptimizationObject();
    ASSERT_NE(root, nullptr);

    auto optimizer = std::make_unique<griddyn::NativeOptimizer>(gds.get(), mode);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    double returnTime = -1.0;
    ASSERT_EQ(optimizer->solve(0.0, returnTime), FUNCTION_EXECUTION_SUCCESS)
        << optimizer->lastSolveResult().message;
    ASSERT_EQ(optimizer->lastSolveResult().status, griddyn::NativeSolveStatus::OPTIMAL)
        << optimizer->lastSolveResult().message;

    // The PEGASE case intentionally assigns the same linear cost to every
    // generator, so individual dispatch values are not a useful reference:
    // multiple feasible dispatches have the same objective.  The aggregate
    // generation is fixed by the lossless DC balance equations and equals
    // the GridDyn active demand of 5733.37087 MW, including the constant real
    // shunt terms represented by the MATPOWER bus Gs column.
    double dispatchTotal = 0.0;
    for (int generatorId = 1; generatorId <= 12; ++generatorId) {
        auto* generator =
            dynamic_cast<griddyn::GridGenOpt*>(root->findByUserID("gen", generatorId));
        ASSERT_NE(generator, nullptr);
        const auto offset = generator->offsets.getOffsets(mode).gOffset;
        ASSERT_TRUE(std::isfinite(optimizer->values[offset]));
        dispatchTotal += optimizer->values[offset];
    }
    EXPECT_NEAR(dispatchTotal, 57.3337087, 1e-7);
    EXPECT_NEAR(optimizer->lastSolveResult().objectiveValue, 5733.37087, 1e-4);
    EXPECT_LE(optimizer->lastSolveResult().maximumConstraintViolation, 1e-7);
    EXPECT_LE(optimizer->lastSolveResult().maximumBoundViolation, 1e-7);
    EXPECT_EQ(optimizer->get("solution_valid"), 1.0);
}
