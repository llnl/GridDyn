/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"

#include "optimization/gridDynOpt.h"
#include "optimization/models/gridBusOpt.h"
#include "optimization/models/gridGenOpt.h"
#include "optimization/models/gridLinkOpt.h"
#include "optimization/optHelperClasses.h"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path makePyPowerCasePath(std::string_view fileName)
{
    return std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "pypower_tests" /
        std::string{fileName};
}

griddyn::OptimizationMode makeDcMode(griddyn::LinearityMode linearity)
{
    return griddyn::OptimizationMode{griddyn::FlowModel::DC, linearity, 0, 1, 1.0};
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

    std::vector<index_t> usedObjectiveOffsets{
        bus1Offsets.aOffset, bus2Offsets.aOffset, generatorOffsets.gOffset};
    std::sort(usedObjectiveOffsets.begin(), usedObjectiveOffsets.end());
    EXPECT_EQ(usedObjectiveOffsets, (std::vector<index_t>{0, 1, 2}));

    std::vector<index_t> usedConstraintOffsets{
        bus1Offsets.constraintOffset, bus1Offsets.constraintOffset + 1, bus2Offsets.constraintOffset};
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
    std::vector<index_t> usedObjectiveOffsets{
        bus1Offsets.aOffset, bus2Offsets.aOffset, generatorOffsets.gOffset};
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
