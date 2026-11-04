/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "core/coreDefinitions.hpp"
#include "griddyn/exciters/ExciterAC7B.h"
#include "griddyn/exciters/ExciterAC8B.h"
#include "griddyn/exciters/ExciterDC1A.h"
#include "griddyn/exciters/ExciterDC2A.h"
#include "griddyn/exciters/ExciterESAC1A.h"
#include "griddyn/exciters/ExciterESAC2A.h"
#include "griddyn/exciters/ExciterESAC5A.h"
#include "griddyn/exciters/ExciterESAC6A.h"
#include "griddyn/exciters/ExciterESST1A.h"
#include "griddyn/exciters/ExciterESST2A.h"
#include "griddyn/exciters/ExciterESST3A.h"
#include "griddyn/exciters/ExciterESST4B.h"
#include "griddyn/exciters/ExciterEXAC1.h"
#include "griddyn/exciters/ExciterEXAC2.h"
#include "griddyn/exciters/ExciterEXAC4.h"
#include "griddyn/exciters/ExciterEXPIC1.h"
#include "griddyn/exciters/ExciterEXST1.h"
#include "griddyn/exciters/ExciterIEEET3.h"
#include "griddyn/exciters/ExciterIEEEX1.h"
#include "griddyn/exciters/ExciterIEEEtype1.h"
#include "griddyn/exciters/ExciterIEEEtype2.h"
#include "griddyn/exciters/ExciterSCRX.h"
#include "griddyn/exciters/ExciterSEXS.h"
#include "griddyn/solvers/SolverMode.hpp"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
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

    double operatingBias(const Exciter& model)
    {
        return std::get<double>(model.getModelDescription().find("operating_bias")->value(model));
    }

    void restoreOperatingBias(ExciterSEXS& model, double value)
    {
        auto saved = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
        entry(saved, "operating_bias").mValue = value;
        saved.restore(model);
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
    for (const auto* name :
         {"tb", "te", "ta", "ka", "vref", "vrmax", "vrmin", "vbias", "basepower"}) {
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
    for (const auto* name : {"operating_bias", "limiter_active", "limiter_high"}) {
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
    for (const auto* name : {"te", "tb", "ta", "urmax", "ka", "vbias", "basepower"}) {
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end());
    }
    EXPECT_EQ(std::find(names.begin(), names.end(), "limiter_active"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "operating_bias"), names.end());
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
    EXPECT_THROW(object.set("vbias", "invalid"), InvalidParameterValue);
    EXPECT_THROW(object.set("operating_bias", 0.2), UnrecognizedParameter);
    EXPECT_THROW(object.set("tb", "invalid"), InvalidParameterValue);
    EXPECT_THROW(object.set("missing", 1.0), UnrecognizedParameter);
    EXPECT_NEAR(object.get("te"), 0.25, 1e-6);
    EXPECT_NEAR(object.get("vbias"), 0.05, 1e-6);
    EXPECT_DOUBLE_EQ(operatingBias(model), 0.0);
    model.set("te", 0.0);
    model.set("tb", 0.0);
    EXPECT_DOUBLE_EQ(model.get("te"), 0.0);
    EXPECT_DOUBLE_EQ(model.get("tb"), 0.0);
}

TEST(ExciterDescriptionTests, CalculatesOperatingBiasWithoutOverwritingConfiguredBias)
{
    ExciterSEXS model;
    model.set("vbias", 0.4);
    initialize(model);
    EXPECT_DOUBLE_EQ(model.get("vbias"), 0.4);
    EXPECT_NEAR(operatingBias(model), 0.6 / 8.0, 1e-12);
    model.set("vbias", 0.8);
    EXPECT_NEAR(operatingBias(model), 0.6 / 8.0, 1e-12);
    initialize(model, 0.4);
    EXPECT_DOUBLE_EQ(model.get("vbias"), 0.8);
    EXPECT_NEAR(operatingBias(model), 0.4 / 8.0, 1e-12);
}

TEST(ExciterDescriptionTests, SeedsOperatingBiasFromConfigurationWhenFieldOutputIsUnspecified)
{
    for (const auto& desiredOutput : {IOdata{}, IOdata{kNullVal}}) {
        ExciterSEXS model;
        model.set("vbias", 0.4);
        initialize(model);
        ASSERT_NE(operatingBias(model), model.get("vbias"));
        model.dynInitializeA(timeZero, 0);
        IOdata fieldSet(4, 0.0);
        model.dynInitializeB(voltageInputs(1.0), desiredOutput, fieldSet);
        EXPECT_DOUBLE_EQ(model.get("vbias"), 0.4);
        EXPECT_DOUBLE_EQ(operatingBias(model), 0.4);
        EXPECT_NEAR(model.getOutput(), 0.4 / 8.0, 1e-12);
        model.set("vbias", 0.8);
        EXPECT_DOUBLE_EQ(operatingBias(model), 0.4);
        model.dynInitializeA(timeZero, 0);
        model.dynInitializeB(voltageInputs(1.0), desiredOutput, fieldSet);
        EXPECT_DOUBLE_EQ(operatingBias(model), 0.8);
        EXPECT_NEAR(model.getOutput(), 0.8 / 8.0, 1e-12);
    }
}

TEST(ExciterDescriptionTests, ClonesConfiguredAndOperatingBiasSeparately)
{
    ExciterSEXS model;
    model.set("vbias", 0.4);
    initialize(model);
    const std::unique_ptr<CoreObject> object(model.clone());
    const auto* cloned = dynamic_cast<const ExciterSEXS*>(object.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_DOUBLE_EQ(cloned->get("vbias"), 0.4);
    EXPECT_DOUBLE_EQ(operatingBias(*cloned), operatingBias(model));
    EXPECT_NE(operatingBias(*cloned), cloned->get("vbias"));
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
            model.set("vbias", 0.4);
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
            const auto savedBias = operatingBias(model);

            model.set("ka", 2.0);
            model.set("ta", 0.3);
            model.set("tb", 0.8);
            model.set("te", 0.7);
            model.set("vrmax", 0.5);
            model.set("vrmin", -0.25);
            model.set("vref", 1.1);
            model.set("vbias", -0.2);
            restoreOperatingBias(model, -0.2);
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
            EXPECT_DOUBLE_EQ(model.get("vbias"), 0.4);
            EXPECT_DOUBLE_EQ(operatingBias(model), savedBias);
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
    const auto savedBias = operatingBias(model);
    const auto saved = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    model.set("te", 0.8);
    model.set("vrmax", 3.0);
    model.set("vbias", -0.5);
    restoreOperatingBias(model, -0.5);
    model.rootCheck(voltageInputs(3.0), emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("te"), 0.8);
    EXPECT_DOUBLE_EQ(model.get("vrmax"), 3.0);
    EXPECT_DOUBLE_EQ(model.get("vbias"), -0.5);
    EXPECT_DOUBLE_EQ(operatingBias(model), savedBias);
    EXPECT_TRUE(historyBit(model, "limiter_active"));
    EXPECT_TRUE(historyBit(model, "limiter_high"));
}

TEST(ExciterDescriptionTests, KeepsDistinctLimiterHistoryAtTheSameBoundaryState)
{
    ExciterSEXS model;
    initialize(model);
    model.set("ka", 4.0);
    model.set("ta", 0.25);
    restoreOperatingBias(model, 0.0);
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
    const auto savedBias = operatingBias(model);
    model.set("vrmax", 5.0);
    model.set("vbias", -1.0);
    restoreOperatingBias(model, -1.0);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("vrmax"), 2.0);
    EXPECT_DOUBLE_EQ(model.get("vbias"), 0.0);
    EXPECT_DOUBLE_EQ(operatingBias(model), savedBias);
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
    restoreOperatingBias(model, 0.5);
    EXPECT_THROW(saved.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("te"), 0.9);
    EXPECT_DOUBLE_EQ(model.get("vbias"), 0.25);
    EXPECT_DOUBLE_EQ(operatingBias(model), 0.5);
}

TEST(ExciterDescriptionTests, CoversEveryConcreteExciterAndItsActiveStateNames)
{
    std::vector<std::unique_ptr<Exciter>> models;
    models.emplace_back(std::make_unique<exciters::ExciterAC7B>());
    models.emplace_back(std::make_unique<exciters::ExciterAC8B>());
    models.emplace_back(std::make_unique<exciters::ExciterDC1A>());
    models.emplace_back(std::make_unique<exciters::ExciterDC2A>());
    models.emplace_back(std::make_unique<exciters::ExciterESAC1A>());
    models.emplace_back(std::make_unique<exciters::ExciterESAC2A>());
    models.emplace_back(std::make_unique<exciters::ExciterESAC5A>());
    models.emplace_back(std::make_unique<exciters::ExciterESAC6A>());
    models.emplace_back(std::make_unique<exciters::ExciterESST1A>());
    models.emplace_back(std::make_unique<exciters::ExciterESST2A>());
    models.emplace_back(std::make_unique<exciters::ExciterESST3A>());
    models.emplace_back(std::make_unique<exciters::ExciterESST4B>());
    models.emplace_back(std::make_unique<exciters::ExciterEXAC1>());
    models.emplace_back(std::make_unique<exciters::ExciterEXAC2>());
    models.emplace_back(std::make_unique<exciters::ExciterEXAC4>());
    models.emplace_back(std::make_unique<exciters::ExciterEXPIC1>());
    models.emplace_back(std::make_unique<exciters::ExciterEXST1>());
    models.emplace_back(std::make_unique<exciters::ExciterIEEET3>());
    models.emplace_back(std::make_unique<exciters::ExciterIEEEX1>());
    models.emplace_back(std::make_unique<exciters::ExciterIEEEtype1>());
    models.emplace_back(std::make_unique<exciters::ExciterIEEEtype2>());
    models.emplace_back(std::make_unique<exciters::ExciterSCRX>());
    models.emplace_back(std::make_unique<exciters::ExciterSEXS>());
    for (const auto& model : models) {
        const auto& description = model->getModelDescription();
        SCOPED_TRACE(std::string(description.name()));
        EXPECT_NO_THROW(model->set("", std::numeric_limits<double>::quiet_NaN()));
        EXPECT_NO_THROW(model->set("# ignored", std::numeric_limits<double>::infinity()));
        EXPECT_NO_THROW(model->set("", "ignored"));
        EXPECT_NO_THROW(model->set("# ignored", "ignored"));
        EXPECT_THROW(model->set("unknown_parameter", std::numeric_limits<double>::quiet_NaN()),
                     InvalidParameterValue);
        EXPECT_TRUE(description.describesExactType(*model));
        const auto saved = ModelFieldSnapshot::capture(*model);
        EXPECT_FALSE(saved.mFields.empty());
        EXPECT_NO_THROW(saved.restore(*model));
        const auto stateOnly = ModelFieldSnapshot::capture(*model, FieldSnapshotLevel::STATE);
        EXPECT_NO_THROW(stateOnly.restore(*model));
        for (const auto& name : model->localStateNames()) {
            EXPECT_NE(description.find(name, ModelFieldKind::STATE), nullptr) << name;
        }
        for (const auto& field : description.fields()) {
            if (field.kind() != ModelFieldKind::PARAMETER ||
                field.valueType() != ModelFieldType::REAL) {
                continue;
            }
            const auto stored = std::get<double>(field.value(*model));
            if (std::isfinite(stored)) {
                EXPECT_DOUBLE_EQ(model->get(field.name()), stored) << field.name();
            }
        }
    }
}

TEST(ExciterDescriptionTests, DistinguishesExpic1ParameterAndStateWithTheSameName)
{
    exciters::ExciterEXPIC1 model;
    const auto& description = model.getModelDescription();
    ASSERT_NE(description.find("vr1"), nullptr);
    EXPECT_EQ(description.find("vr1")->kind(), ModelFieldKind::PARAMETER);
    ASSERT_NE(description.find("vr1", ModelFieldKind::STATE), nullptr);
    EXPECT_EQ(description.find("vr1", ModelFieldKind::STATE)->kind(), ModelFieldKind::STATE);
}

TEST(ExciterDescriptionTests, UsesMemberDispatchAndPreservesSpecialValidation)
{
    exciters::ExciterAC7B simple;
    simple.set("tr", 250.0, units::ms);
    EXPECT_NEAR(simple.get("tr"), 0.25, 1e-6);
    EXPECT_NEAR(simple.get("tr", units::ms), 250.0, 1e-4);

    exciters::ExciterESAC2A validated;
    EXPECT_THROW(validated.set("tr", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    validated.set("uel_enabled", -2.0);
    EXPECT_TRUE(validated.getFlag("uel_enabled"));
    EXPECT_THROW(validated.set("uel_enabled", std::numeric_limits<double>::quiet_NaN()),
                 InvalidParameterValue);
    EXPECT_TRUE(validated.getFlag("uel_enabled"));
    EXPECT_THROW(validated.set("oel_enabled", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    const auto basePower = validated.get("basepower");
    EXPECT_THROW(validated.set("basepower", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    EXPECT_DOUBLE_EQ(validated.get("basepower"), basePower);
    EXPECT_NO_THROW(validated.set("# ignored", std::numeric_limits<double>::quiet_NaN()));
    EXPECT_NO_THROW(validated.set("", std::numeric_limits<double>::infinity()));

    exciters::ExciterIEEEtype1 inherited;
    inherited.set("ke", 2.0);
    EXPECT_DOUBLE_EQ(inherited.get("ke"), 2.0);
    const auto* limiter = inherited.getModelDescription().find("limiter");
    ASSERT_NE(limiter, nullptr);
    EXPECT_EQ(limiter->kind(), ModelFieldKind::COMMAND);
}

TEST(ExciterDescriptionTests, RestoresPackedLimiterHistoryInAnotherExciter)
{
    exciters::ExciterAC7B model;
    auto original = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    auto changed = original;
    entry(changed, "pid_limited").mValue = true;
    changed.restore(model);
    const auto* limiter = model.getModelDescription().find("pid_limited");
    ASSERT_NE(limiter, nullptr);
    EXPECT_TRUE(std::get<bool>(limiter->value(model)));
    original.restore(model);
    EXPECT_FALSE(std::get<bool>(limiter->value(model)));
}

TEST(ExciterDescriptionTests, DispatchesValidatedFieldsAndInheritedAliases)
{
    exciters::ExciterEXAC1 exac1;
    EXPECT_THROW(exac1.set("te", 0.0), InvalidParameterValue);
    exac1.set("te", 250.0, units::ms);
    EXPECT_NEAR(exac1.get("te", units::ms), 250.0, 1e-4);

    exciters::ExciterEXAC2 exac2;
    EXPECT_THROW(exac2.set("kl", 0.0), InvalidParameterValue);
    exciters::ExciterESST3A esst3a;
    EXPECT_THROW(esst3a.set("kp", 0.0), InvalidParameterValue);
    exciters::ExciterEXST1 exst1;
    EXPECT_THROW(exst1.set("ta", 0.0), InvalidParameterValue);

    exciters::ExciterSCRX scrx;
    scrx.set("k", 3.0);
    scrx.set("efdmax", 5.0);
    EXPECT_DOUBLE_EQ(scrx.get("ka"), 3.0);
    EXPECT_DOUBLE_EQ(scrx.get("emax"), 5.0);

    exciters::ExciterIEEEtype2 type2;
    type2.set("tf1", 500.0, units::ms);
    EXPECT_NEAR(type2.get("tf1", units::ms), 500.0, 1e-4);
}

TEST(ExciterDescriptionTests, ValidatesLocalFieldsThroughTheirDescriptions)
{
    exciters::ExciterESST3A esst3a;
    const auto& esst3aDescription = esst3a.getModelDescription();
    const auto originalGain = esst3a.get("kp");
    EXPECT_THROW(esst3aDescription.trySetLocal(esst3a, "kp", 0.0), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(esst3a.get("kp"), originalGain);
    EXPECT_THROW(esst3aDescription.trySetLocal(
                     esst3a, "vimax", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    esst3a.set("theta_p", 0.2);
    EXPECT_NEAR(esst3a.get("thetap"), 0.2, 1e-6);

    exciters::ExciterEXST1 exst1;
    EXPECT_THROW(exst1.getModelDescription().trySetLocal(exst1, "tb", 0.0),
                 InvalidParameterValue);
    exciters::ExciterESST1A esst1a;
    EXPECT_THROW(esst1a.set("uel", 1.5), InvalidParameterValue);
}

}  // namespace griddyn
