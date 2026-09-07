/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "optimization/gridDynOpt.h"
#include "optimization/models/gridBusOpt.h"
#include "optimization/models/gridGenOpt.h"
#include "optimization/models/gridLinkOpt.h"
#include "optimization/optHelperClasses.h"
#include "optimization/optimizerInterface.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path makePyPowerCasePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" / std::string{fileName};
}

std::filesystem::path makeMatPowerCasePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "matlab_test_files" /
        std::string{fileName};
}

std::filesystem::path makeValidationCasePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "validation_tests" /
        std::string{fileName};
}

griddyn::OptimizationMode makeDcMode(griddyn::LinearityMode linearity)
{
    return griddyn::OptimizationMode{griddyn::FlowModel::DC, linearity, 0, 1, 1.0};
}

void expectFinite(const std::vector<double>& values)
{
    for (const auto value : values) {
        EXPECT_TRUE(std::isfinite(value));
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
    // and fixed-angle buses own their reference-angle row. Passive branches
    // contribute flow terms to the connected buses rather than owning a row.
    EXPECT_EQ(bus1->constraintSize(mode), 2);
    EXPECT_EQ(bus2->constraintSize(mode), 1);
    EXPECT_EQ(branch->constraintSize(mode), 0);
    EXPECT_EQ(root->constraintSize(mode), 3);
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

    std::vector<index_t> usedObjectiveOffsets{bus1Offsets.aOffset,
                                              bus2Offsets.aOffset,
                                              generatorOffsets.gOffset};
    std::sort(usedObjectiveOffsets.begin(), usedObjectiveOffsets.end());
    EXPECT_EQ(usedObjectiveOffsets, (std::vector<index_t>{0, 1, 2}));

    std::vector<index_t> usedConstraintOffsets{bus1Offsets.constraintOffset,
                                               bus1Offsets.constraintOffset + 1,
                                               bus2Offsets.constraintOffset};
    std::sort(usedConstraintOffsets.begin(), usedConstraintOffsets.end());
    EXPECT_EQ(usedConstraintOffsets, (std::vector<index_t>{0, 1, 2}));

    gds->initializeOptimizationModel(mode);
    EXPECT_EQ(bus1->getGen(0), preloadedGeneratorOpt);
    EXPECT_EQ(root->constraintSize(mode), 3);
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

    auto* physicalBranch = gds->findByUserID("link", 1);
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
                physicalGenerator->getRealPower(),
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
    ASSERT_EQ(root->constraintSize(mode), 3);

    auto optimizer = griddyn::makeOptimizer(gds.get(), mode);
    ASSERT_NE(optimizer, nullptr);
    optimizer->setName("dcopf-dry-run");
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    ASSERT_TRUE(optimizer->isInitialized());
    EXPECT_EQ(optimizer->size(), 3);
    EXPECT_EQ(optimizer->constraintCount(), 3);
    EXPECT_EQ(optimizer->values.size(), 3U);
    EXPECT_EQ(optimizer->lowerBounds.size(), 3U);
    EXPECT_EQ(optimizer->upperBounds.size(), 3U);
    EXPECT_EQ(optimizer->constraintValues.size(), 3U);
    EXPECT_EQ(optimizer->constraintLowerBounds.size(), 3U);
    EXPECT_EQ(optimizer->constraintUpperBounds.size(), 3U);
    EXPECT_EQ(optimizer->gradient.size(), 3U);
    EXPECT_EQ(optimizer->variableType.size(), 3U);
    EXPECT_EQ(optimizer->tolerances.size(), 3U);
    EXPECT_EQ(optimizer->multipliers.size(), 3U);
    EXPECT_EQ(optimizer->linearObjective.points(), 2);
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
    EXPECT_EQ(root->constraintSize(mode), 10);

    auto optimizer = griddyn::makeOptimizer(gds.get(), mode);
    ASSERT_NE(optimizer, nullptr);
    ASSERT_EQ(optimizer->allocate(root->objSize(mode), root->constraintSize(mode)),
              FUNCTION_EXECUTION_SUCCESS);
    optimizer->setMaxNonZeros(root->objSize(mode) * root->constraintSize(mode));
    optimizer->initialize(0.0);

    EXPECT_TRUE(optimizer->isInitialized());
    EXPECT_EQ(optimizer->size(), 12);
    EXPECT_EQ(optimizer->constraintCount(), 10);
    EXPECT_EQ(optimizer->values.size(), 12U);
    EXPECT_EQ(optimizer->lowerBounds.size(), 12U);
    EXPECT_EQ(optimizer->upperBounds.size(), 12U);
    EXPECT_EQ(optimizer->constraintValues.size(), 10U);
    EXPECT_EQ(optimizer->constraintLowerBounds.size(), 10U);
    EXPECT_EQ(optimizer->constraintUpperBounds.size(), 10U);
    EXPECT_EQ(optimizer->gradient.size(), 12U);
    EXPECT_EQ(optimizer->variableType.size(), 12U);
    EXPECT_EQ(optimizer->tolerances.size(), 12U);
    EXPECT_EQ(optimizer->multipliers.size(), 10U);
    EXPECT_EQ(optimizer->linearObjective.points(), 6);
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
