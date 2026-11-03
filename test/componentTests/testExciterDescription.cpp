/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "griddyn/exciters/ExciterSEXS.h"
#include "griddyn/solvers/SolverMode.hpp"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace griddyn {
namespace {
    using exciters::ExciterSEXS;

    IOdata voltageInputs(double voltage)
    {
        IOdata inputs(exciterInputCount, 0.0);
        inputs[exciterVoltageInLocation] = voltage;
        return inputs;
    }

    void initialize(ExciterSEXS& model, double fieldVoltage = 0.6)
    {
        model.set("ka", 8.0);
        model.set("ta", 0.2);
        model.set("tb", 1.0);
        model.set("te", 0.5);
        model.set("vrmin", -0.5);
        model.set("vrmax", 1.2);
        model.dynInitializeA(timeZero, 0);
        IOdata fieldSet(4, 0.0);
        model.dynInitializeB(voltageInputs(1.0), {fieldVoltage}, fieldSet);
    }

    bool historyBit(const ExciterSEXS& model, std::string_view name)
    {
        return std::get<bool>(model.getModelDescription().find(name)->value(model));
    }

    ModelFieldEntry& entry(ModelFieldSnapshot& saved, std::string_view name)
    {
        const auto field = std::find_if(saved.mFields.begin(),
                                        saved.mFields.end(),
                                        [name](const auto& item) { return item.mName == name; });
        if (field == saved.mFields.end()) {
            throw FileOperationError("missing test field");
        }
        return *field;
    }
}  // namespace

TEST(ExciterDescriptionTests, DiscoversInheritedParametersSolverStatesAndLimiterHistory)
{
    ExciterSEXS model;
    const auto& description = model.getModelDescription();
    EXPECT_EQ(description.name(), "ExciterSEXS");
    ASSERT_NE(description.parent(), nullptr);
    EXPECT_EQ(description.parent()->name(), "Exciter");
    EXPECT_EQ(description.parent()->parent()->name(), "GridComponent");
    for (const auto* name : {"tb", "te", "ta", "ka", "vref", "vrmax", "vrmin", "basepower"}) {
        ASSERT_NE(description.find(name), nullptr);
        EXPECT_EQ(description.find(name)->kind(), ModelFieldKind::PARAMETER);
    }
    EXPECT_EQ(description.find("vrmax"), description.find("urmax"));
    EXPECT_EQ(description.find("vrmin"), description.find("urmin"));
    for (const auto* name : {"ef", "x"}) {
        ASSERT_NE(description.find(name), nullptr);
        EXPECT_EQ(description.find(name)->kind(), ModelFieldKind::STATE);
        EXPECT_FALSE(description.find(name)->readable());
    }
    for (const auto* name : {"vbias", "limiter_active", "limiter_high"}) {
        ASSERT_NE(description.find(name), nullptr);
        EXPECT_EQ(description.find(name)->kind(), ModelFieldKind::LOCAL_STATE);
        EXPECT_FALSE(description.find(name)->editable());
    }
    EXPECT_FALSE(description.trySetFlagLocal(model, "limiter_active", true));
    EXPECT_FALSE(description.trySetLocal(model, "limiter_high", 1.0));
    EXPECT_EQ(model.localStateNames(), (stringVec{"ef", "x"}));
    EXPECT_EQ(description.find("operating_flags"), nullptr);
    EXPECT_EQ(description.find("m_state"), nullptr);
    EXPECT_EQ(description.find("regulator_output"), nullptr);
    stringVec names;
    model.getParameterStrings(names, ParamStringType::NUMERIC);
    for (const auto* name : {"te", "tb", "ta", "urmax", "ka", "basepower"}) {
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end());
    }
    EXPECT_EQ(std::find(names.begin(), names.end(), "limiter_active"), names.end());
}

TEST(ExciterDescriptionTests, UsesExistingSetGetUnitsAliasesAndParentDispatch)
{
    ExciterSEXS model;
    CoreObject& object = model;
    object.set("te", 250.0, units::ms);
    object.set("tb", 750.0, units::ms);
    object.set("ta", 125.0, units::ms);
    object.set("urmax", 175.0, units::percent);
    object.set("urmin", -50.0, units::percent);
    object.set("vref", 105.0, units::percent);
    object.set("vbias", 5.0, units::percent);
    object.set("basepower", 200.0, units::MW);
    EXPECT_NEAR(object.get("te"), 0.25, 1e-6);
    EXPECT_NEAR(object.get("tb"), 0.75, 1e-6);
    EXPECT_NEAR(object.get("ta"), 0.125, 1e-6);
    EXPECT_DOUBLE_EQ(object.get("te", units::ms), 250.0);
    EXPECT_NEAR(object.get("vrmax"), 1.75, 1e-6);
    EXPECT_NEAR(object.get("vrmin"), -0.5, 1e-6);
    EXPECT_NEAR(object.get("vref"), 1.05, 1e-6);
    EXPECT_NEAR(object.get("vbias"), 0.05, 1e-6);
    EXPECT_DOUBLE_EQ(object.get("basepower"), 200.0);
    EXPECT_THROW(object.set("te", 2.0, units::MW), InvalidParameterValue);
    EXPECT_THROW(object.set("ta", 2.0, units::MW), InvalidParameterValue);
    EXPECT_THROW(object.set("vbias", 2.0, units::second), InvalidParameterValue);
    EXPECT_THROW(object.set("tb", "invalid"), InvalidParameterValue);
    EXPECT_THROW(object.set("missing", 1.0), UnrecognizedParameter);
    EXPECT_NEAR(object.get("te"), 0.25, 1e-6);
    model.set("te", 0.0);
    model.set("tb", 0.0);
    EXPECT_DOUBLE_EQ(model.get("te"), 0.0);
    EXPECT_DOUBLE_EQ(model.get("tb"), 0.0);
}

TEST(ExciterDescriptionTests, ResolvesBothStatesInDaeAndDifferentialLayouts)
{
    ExciterSEXS model;
    initialize(model);
    for (const auto& mode : {cDaeSolverMode, cDynDiffSolverMode}) {
        ASSERT_EQ(model.stateSize(mode), 2);
        model.setOffset(3, mode);
        EXPECT_EQ(model.findIndex("ef", mode), 3);
        EXPECT_EQ(model.findIndex("x", mode), 4);
        stringVec names;
        model.getStateName(names, mode);
        ASSERT_GE(names.size(), 5U);
        EXPECT_TRUE(names[3].ends_with(":ef"));
        EXPECT_TRUE(names[4].ends_with(":x"));
        model.setOffset(7, mode);
        EXPECT_EQ(model.findIndex("ef", mode), 7);
        EXPECT_EQ(model.findIndex("x", mode), 8);
    }
    EXPECT_EQ(model.findIndex("ef", cDynAlgSolverMode), kNullLocation);
    const auto saved = ModelFieldSnapshot::capture(model);
    for (const auto& item : saved.mFields) {
        EXPECT_NE(item.mName, "ef");
        EXPECT_NE(item.mName, "x");
        EXPECT_NE(item.mName, "derivatives");
        EXPECT_NE(item.mName, "operating_flags");
    }
}

TEST(ExciterDescriptionTests, RestoresLimitedPhysicsWithoutReinitializingForBothSolverModes)
{
    for (const auto& mode : {cDaeSolverMode, cDynDiffSolverMode}) {
        for (const auto driveVoltage : {0.2, 2.0}) {
            SCOPED_TRACE(driveVoltage);
            SCOPED_TRACE(mode.offsetIndex);
            ExciterSEXS model;
            initialize(model);
            const auto inputs = voltageInputs(driveVoltage);
            EXPECT_EQ(
                model.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK),
                ChangeCode::JACOBIAN_CHANGE);
            ASSERT_TRUE(historyBit(model, "limiter_active"));
            EXPECT_EQ(historyBit(model, "limiter_high"), driveVoltage < 1.0);
            ASSERT_EQ(model.stateSize(mode), 2);
            model.setOffset(3, mode);
            model.setRootOffset(0, mode);
            constexpr std::size_t vectorSize = 5;
            std::vector<double> states(vectorSize, 0.0);
            std::vector<double> savedRates(vectorSize, 0.0);
            const CoreTime savedTime(1.25);
            model.guessState(savedTime, states.data(), savedRates.data(), mode);
            StateData data(savedTime, states.data(), savedRates.data());
            model.derivative(inputs, data, savedRates.data(), mode);
            data.cj = 2.0;
            model.setState(savedTime, states.data(), savedRates.data(), mode);
            std::array<double, 1> expectedRoots{};
            model.rootTest(inputs, data, expectedRoots.data(), mode);
            IOlocs inputLocations(exciterInputCount, kNullLocation);
            inputLocations[exciterVoltageInLocation] = 10;
            MatrixDataSparse<double> expectedJacobian;
            model.jacobianElements(inputs, data, expectedJacobian, inputLocations, mode);
            expectedJacobian.compact();
            const auto saved = ModelFieldSnapshot::capture(model);
            const auto savedBias = model.get("vbias");

            model.set("ka", 2.0);
            model.set("ta", 0.3);
            model.set("tb", 0.8);
            model.set("te", 0.7);
            model.set("vrmax", 0.5);
            model.set("vrmin", -0.25);
            model.set("vref", 1.1);
            model.set("vbias", -0.2);
            auto alteredStates = states;
            alteredStates[3] = -0.1;
            alteredStates[4] = 0.0;
            std::vector<double> alteredRates(vectorSize, 9.0);
            model.setState(9.0, alteredStates.data(), alteredRates.data(), mode);
            const auto oppositeInputs = voltageInputs(driveVoltage < 1.0 ? 3.0 : 0.0);
            model.rootCheck(oppositeInputs,
                            emptyStateData,
                            cLocalSolverMode,
                            CheckLevel::FULL_CHECK);
            model.rootCheck(oppositeInputs,
                            emptyStateData,
                            cLocalSolverMode,
                            CheckLevel::FULL_CHECK);
            ASSERT_TRUE(historyBit(model, "limiter_active"));
            EXPECT_EQ(historyBit(model, "limiter_high"), driveVoltage > 1.0);

            saved.restore(model);
            // The solver payload and model-owned copies use the existing state path.
            model.setState(savedTime, states.data(), savedRates.data(), mode);
            EXPECT_EQ(model.currentTime(), savedTime);
            EXPECT_DOUBLE_EQ(model.get("vbias"), savedBias);
            EXPECT_DOUBLE_EQ(model.get("vrmax"), 1.2);
            EXPECT_DOUBLE_EQ(model.get("vrmin"), -0.5);
            EXPECT_TRUE(historyBit(model, "limiter_active"));
            EXPECT_EQ(historyBit(model, "limiter_high"), driveVoltage < 1.0);
            EXPECT_DOUBLE_EQ(model.getOutput(), states[3]);
            std::vector<double> actualRates(vectorSize, 0.0);
            model.derivative(inputs, data, actualRates.data(), mode);
            for (const auto index : {3, 4}) {
                EXPECT_DOUBLE_EQ(actualRates[index], savedRates[index]);
            }
            std::vector<double> residual(vectorSize, 0.0);
            model.residual(inputs, data, residual.data(), mode);
            EXPECT_NEAR(residual[3], 0.0, 1e-12);
            EXPECT_NEAR(residual[4], 0.0, 1e-12);
            std::array<double, 1> actualRoots{};
            model.rootTest(inputs, data, actualRoots.data(), mode);
            EXPECT_EQ(actualRoots, expectedRoots);
            MatrixDataSparse<double> actualJacobian;
            model.jacobianElements(inputs, data, actualJacobian, inputLocations, mode);
            actualJacobian.compact();
            for (const auto row : {3, 4}) {
                for (const auto column : {3, 4, 10}) {
                    EXPECT_DOUBLE_EQ(actualJacobian.at(row, column),
                                     expectedJacobian.at(row, column));
                }
            }
            EXPECT_EQ(model.rootCheck(voltageInputs(1.0),
                                      emptyStateData,
                                      cLocalSolverMode,
                                      CheckLevel::FULL_CHECK),
                      ChangeCode::JACOBIAN_CHANGE);
            EXPECT_FALSE(historyBit(model, "limiter_active"));
        }
    }
}

TEST(ExciterDescriptionTests, StateOnlyRestoresLimiterMemoryWhileKeepingChangedParameters)
{
    ExciterSEXS model;
    initialize(model);
    model.rootCheck(voltageInputs(0.2), emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    const auto savedBias = model.get("vbias");
    const auto saved = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    model.set("te", 0.8);
    model.set("vrmax", 3.0);
    model.set("vbias", -0.5);
    model.rootCheck(voltageInputs(3.0), emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("te"), 0.8);
    EXPECT_DOUBLE_EQ(model.get("vrmax"), 3.0);
    EXPECT_DOUBLE_EQ(model.get("vbias"), savedBias);
    EXPECT_TRUE(historyBit(model, "limiter_active"));
    EXPECT_TRUE(historyBit(model, "limiter_high"));
}

TEST(ExciterDescriptionTests, KeepsDistinctLimiterHistoryAtTheSameBoundaryState)
{
    ExciterSEXS model;
    initialize(model);
    model.set("ka", 4.0);
    model.set("ta", 0.25);
    model.set("vbias", 0.0);
    model.set("vrmax", 1.0);
    model.setRootOffset(0, cLocalSolverMode);
    const auto inputs = voltageInputs(1.0);
    std::vector<double> states{0.5, 0.25};
    std::vector<double> rates(2, 0.0);
    model.setState(2.0, states.data(), rates.data(), cLocalSolverMode);
    EXPECT_EQ(model.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK),
              ChangeCode::NO_CHANGE);
    const auto freeBranch = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    const StateData data(2.0, states.data(), rates.data());
    IOlocs inputLocations(exciterInputCount, kNullLocation);
    MatrixDataSparse<double> freeJacobian;
    model.jacobianElements(inputs, data, freeJacobian, inputLocations, cLocalSolverMode);
    freeJacobian.compact();

    model.rootTrigger(2.0, inputs, {1}, cLocalSolverMode);
    ASSERT_TRUE(historyBit(model, "limiter_active"));
    ASSERT_TRUE(historyBit(model, "limiter_high"));
    const auto limitedBranch = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    EXPECT_EQ(model.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK),
              ChangeCode::NO_CHANGE);
    MatrixDataSparse<double> limitedJacobian;
    model.jacobianElements(inputs, data, limitedJacobian, inputLocations, cLocalSolverMode);
    limitedJacobian.compact();
    EXPECT_NE(freeJacobian.at(0, 1), limitedJacobian.at(0, 1));

    freeBranch.restore(model);
    EXPECT_FALSE(historyBit(model, "limiter_active"));
    EXPECT_EQ(model.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK),
              ChangeCode::NO_CHANGE);
    limitedBranch.restore(model);
    EXPECT_TRUE(historyBit(model, "limiter_active"));
    EXPECT_EQ(model.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK),
              ChangeCode::NO_CHANGE);
    std::vector<double> actualStates(2, 0.0);
    model.guessState(2.0, actualStates.data(), rates.data(), cLocalSolverMode);
    EXPECT_EQ(actualStates, states);
}

TEST(ExciterDescriptionTests, CapturesTheAdjustedInitialLimitAndRestoresItWithoutInitialization)
{
    ExciterSEXS model;
    initialize(model, 2.0);
    EXPECT_DOUBLE_EQ(model.get("vrmax"), 2.0);
    EXPECT_TRUE(historyBit(model, "limiter_active"));
    const auto saved = ModelFieldSnapshot::capture(model);
    const auto savedBias = model.get("vbias");
    model.set("vrmax", 5.0);
    model.set("vbias", -1.0);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("vrmax"), 2.0);
    EXPECT_DOUBLE_EQ(model.get("vbias"), savedBias);
    EXPECT_TRUE(historyBit(model, "limiter_active"));
}

TEST(ExciterDescriptionTests, RejectsMalformedLimiterStorageBeforeChangingAnyFields)
{
    ExciterSEXS model;
    initialize(model);
    auto saved = ModelFieldSnapshot::capture(model);
    entry(saved, "limiter_high").mValue = std::string("invalid Boolean storage");
    model.set("te", 0.9);
    model.set("vbias", 0.25);
    EXPECT_THROW(saved.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("te"), 0.9);
    EXPECT_DOUBLE_EQ(model.get("vbias"), 0.25);
}

}  // namespace griddyn
