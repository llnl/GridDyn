/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "griddyn/GridBus.h"
#include "griddyn/loads/ExponentialLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/solvers/SolverMode.hpp"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

namespace griddyn {
namespace {
    class DescribedAcBus: public AcBus {
      public:
        [[nodiscard]] const ModelDescription& getModelDescription() const override
        {
            static const auto description = ModelDescription::define<DescribedAcBus>(
                "DescribedAcBus",
                &AcBus::getModelDescription(),
                {state("voltage", units::puV, "Bus voltage magnitude"),
                 state("angle", units::rad, "Bus voltage angle")});
            return description;
        }
    };

    class FaultObservingLoad: public loads::ExponentialLoad {
      public:
        explicit FaultObservingLoad(GridBus& connectedBus)
        {
            bus = &connectedBus;
            opFlags.set(POWERFLOW_INITIALIZED);
        }
        void alert(CoreObject* /*object*/, int code) override
        {
            if (code == POTENTIAL_FAULT_CHANGE) {
                ++mNotifications;
            }
        }
        int mNotifications = 0;
    };
}  // namespace

TEST(ExponentialParameterTests, UsesExistingSetGetWithAliasesAndDimensionlessConversion)
{
    loads::ExponentialLoad load(0.5, 0.2);
    CoreObject& object = load;
    object.set("ap", 150.0, units::percent);
    object.set("alphaq", -0.5);
    EXPECT_NEAR(object.get("alphap"), 1.5, 1e-6);
    EXPECT_DOUBLE_EQ(object.get("ap", units::percent), 150.0);
    EXPECT_DOUBLE_EQ(object.get("aq"), -0.5);
    EXPECT_NEAR(load.getRealPower(0.9), 0.5 * std::pow(0.9, 1.5), 1e-8);
    EXPECT_NEAR(load.getReactivePower(0.9), 0.2 * std::pow(0.9, -0.5), 1e-12);
    object.set("alpha", 200.0, units::percent);
    EXPECT_NEAR(object.get("alphap"), 2.0, 1e-6);
    EXPECT_NEAR(object.get("alphaq"), 2.0, 1e-6);
}

TEST(ExponentialParameterTests, DelegatesInheritedPowerUnitsAndRetainsPowerFactorSideEffects)
{
    loads::ExponentialLoad load;
    load.set("basepower", 200.0, units::MW);
    load.set("p", 100.0, units::MW);
    load.set("q", 40.0, units::MVAR);
    load.setFlag("usepowerfactor", true);
    load.set("q", 60.0, units::MVAR);
    EXPECT_DOUBLE_EQ(load.get("p"), 0.5);
    EXPECT_DOUBLE_EQ(load.get("q"), 0.3);
    EXPECT_DOUBLE_EQ(load.get("p", units::MW), 100.0);
    EXPECT_DOUBLE_EQ(load.get("pf"), 0.6);
    load.set("p+", 20.0, units::MW);
    load.set("q*", 2.0);
    EXPECT_DOUBLE_EQ(load.get("p"), 0.6);
    EXPECT_DOUBLE_EQ(load.get("q"), 0.6);
    EXPECT_DOUBLE_EQ(load.get("pf"), 1.0);
    load.set("load p", 40.0, units::MW);
    EXPECT_DOUBLE_EQ(load.get("p"), 0.2);
    load.set("name", "worked_load");
    EXPECT_EQ(load.getName(), "worked_load");
}

TEST(ExponentialParameterTests, NumericDeclaredFlagsUseTheClassFlagSetter)
{
    loads::ExponentialLoad load;
    load.set("p", 0.5);
    load.set("q", 0.2);
    load.set("usepowerfactor", 1.0);
    EXPECT_TRUE(load.getFlag("usepowerfactor"));
    EXPECT_DOUBLE_EQ(load.get("pf"), 0.4);
    load.set("usepowerfactor", 0.0);
    EXPECT_FALSE(load.getFlag("usepowerfactor"));
    load.set("fixedshunt", -1.0);
    EXPECT_TRUE(load.getFlag("fixedshunt"));
}

TEST(ExponentialParameterTests, KeepsUnknownNamesDistinctFromRejectedValues)
{
    loads::ExponentialLoad load;
    load.set("alpha", 2.0);
    for (const double invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        EXPECT_THROW(load.set("ap", invalid), InvalidParameterValue);
        EXPECT_THROW(load.set("alpha", invalid), InvalidParameterValue);
    }
    EXPECT_DOUBLE_EQ(load.get("alphap"), 2.0);
    EXPECT_DOUBLE_EQ(load.get("alphaq"), 2.0);
    EXPECT_THROW(load.set("ap", 1.0, units::second), InvalidParameterValue);
    EXPECT_THROW(load.set("p", 1.0, units::second), InvalidParameterValue);
    EXPECT_THROW(load.set("ap", "invalid"), InvalidParameterValue);
    EXPECT_THROW(load.set("alpha", "invalid"), InvalidParameterValue);
    EXPECT_THROW(load.set("p", "invalid"), InvalidParameterValue);
    EXPECT_THROW(load.set("missing_parameter", 1.0), UnrecognizedParameter);
    EXPECT_EQ(load.get("missing_parameter"), kNullVal);
}

TEST(ExponentialParameterTests, PowerSettersNotifyOnceAndRejectBadUnitsBeforeChanging)
{
    GridBus bus(0.01, 0.0);
    FaultObservingLoad load(bus);
    for (const auto* name : {"p", "load p", "q", "load q"}) {
        const auto before = load.mNotifications;
        load.set(name, 20.0, units::MW);
        EXPECT_EQ(load.mNotifications, before + 1);
        EXPECT_DOUBLE_EQ(load.get(name, units::MW), 20.0);
        EXPECT_THROW(load.set(name, 5.0, units::second), InvalidParameterValue);
        EXPECT_EQ(load.mNotifications, before + 1);
        EXPECT_DOUBLE_EQ(load.get(name, units::MW), 20.0);
        EXPECT_THROW(load.get(name, units::second), InvalidParameterValue);
    }
}

TEST(ExponentialParameterTests, EnumeratesLocalAndInheritedDeclarations)
{
    loads::ExponentialLoad load;
    const auto& description = load.getModelDescription();
    EXPECT_EQ(description.name(), "ExponentialLoad");
    EXPECT_EQ(description.parent()->name(), "GridLoad");
    EXPECT_EQ(description.parent()->parent()->name(), "GridComponent");
    ASSERT_NE(description.find("alphap"), nullptr);
    EXPECT_EQ(description.find("ap"), description.find("alphap"));
    EXPECT_EQ(description.find("alphap")->description(), "Voltage exponent for real power");
    EXPECT_NE(description.find("p"), nullptr);
    EXPECT_NE(description.find("basepower"), nullptr);
    EXPECT_NE(description.find("prevTime"), nullptr);
    EXPECT_EQ(description.find("prevTime")->kind(), ModelFieldKind::LOCAL_STATE);
    EXPECT_EQ(description.find("enabled")->kind(), ModelFieldKind::FLAG);
    EXPECT_EQ(description.find("fixedshunt")->kind(), ModelFieldKind::FLAG);
    EXPECT_FALSE(description.find("fixedshunt")->hasUnits());
    EXPECT_EQ(description.find("operating_flags"), nullptr);
    EXPECT_EQ(description.find("derivatives"), nullptr);
    EXPECT_EQ(description.find("state"), nullptr);
    EXPECT_DOUBLE_EQ(load.get("basepower"), 100.0);
    EXPECT_FALSE(description.find("alpha")->readable());
    EXPECT_EQ(description.find("alpha")->kind(), ModelFieldKind::COMMAND);
    stringVec names;
    load.getParameterStrings(names, ParamStringType::NUMERIC);
    for (const auto* name : {"alphap", "ap", "alphaq", "alpha", "p", "q", "pf", "basepower"}) {
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end());
    }
    load.getParameterStrings(names, ParamStringType::LOCAL_NUM);
    EXPECT_NE(std::find(names.begin(), names.end(), "alphap"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "p"), names.end());
}

TEST(ExponentialParameterTests, SolverDeclarationsResolveThroughTheActiveLayout)
{
    DescribedAcBus bus;
    bus.set("type", "pq");
    bus.pFlowInitializeA(timeZero, 0);
    ASSERT_GT(bus.stateSize(cPflowSolverMode), 0);
    bus.setOffset(2, cPflowSolverMode);
    const auto* voltage = bus.getModelDescription().find("voltage");
    ASSERT_NE(voltage, nullptr);
    EXPECT_EQ(voltage->kind(), ModelFieldKind::STATE);
    const auto firstIndex = bus.findIndex(voltage->name(), cPflowSolverMode);
    ASSERT_NE(firstIndex, kNullLocation);
    ASSERT_NE(firstIndex, kInvalidLocation);
    const auto angleIndex = bus.findIndex("angle", cPflowSolverMode);
    ASSERT_NE(angleIndex, kNullLocation);
    ASSERT_NE(angleIndex, kInvalidLocation);
    EXPECT_NE(firstIndex, angleIndex);
    stringVec stateNames;
    bus.getStateName(stateNames, cPflowSolverMode);
    ASSERT_LT(firstIndex, static_cast<index_t>(stateNames.size()));
    ASSERT_LT(angleIndex, static_cast<index_t>(stateNames.size()));
    EXPECT_TRUE(stateNames[firstIndex].ends_with(":voltage"));
    EXPECT_TRUE(stateNames[angleIndex].ends_with(":angle"));
    EXPECT_EQ(bus.findIndex("voltage", cDynDiffSolverMode), kNullLocation);
    std::vector<double> solverValues(16, 0.0);
    ASSERT_LT(firstIndex, static_cast<index_t>(solverValues.size()));
    solverValues[firstIndex] = 1.04;
    EXPECT_DOUBLE_EQ(solverValues[bus.findIndex(voltage->name(), cPflowSolverMode)], 1.04);
    bus.setOffset(8, cPflowSolverMode);
    EXPECT_EQ(bus.findIndex(voltage->name(), cPflowSolverMode), firstIndex + 6);
    EXPECT_EQ(bus.findIndex("angle", cPflowSolverMode), angleIndex + 6);
    const auto saved = ModelFieldSnapshot::capture(bus);
    EXPECT_EQ(std::find_if(saved.mFields.begin(),
                           saved.mFields.end(),
                           [](const auto& item) {
                               return item.mName == "voltage" || item.mName == "angle";
                           }),
              saved.mFields.end());
}

TEST(ExponentialParameterTests, RestoresNamedFlagsAndTheIndependentPowerFactorTarget)
{
    loads::ExponentialLoad load(0.5, 0.2);
    load.set("qratio", 0.7);
    EXPECT_EQ(load.getModelDescription().find("qratio")->kind(), ModelFieldKind::PARAMETER);
    load.setFlag("fixedshunt", true);
    EXPECT_TRUE(load.getFlag("usepowerfactor"));
    EXPECT_TRUE(load.getFlag("fixedshunt"));
    const auto saved = ModelFieldSnapshot::capture(load);
    load.set("qratio", 1.2);
    load.setFlag("usepowerfactor", false);
    load.setFlag("fixedshunt", false);
    saved.restore(load);
    EXPECT_TRUE(load.getFlag("usepowerfactor"));
    EXPECT_TRUE(load.getFlag("fixedshunt"));
    EXPECT_DOUBLE_EQ(load.get("pf"), 0.7);
    EXPECT_DOUBLE_EQ(load.get("qratio"), 0.7);
    EXPECT_NE(load.get("pf"), load.get("q") / load.get("p"));
    load.setFlag("usepowerfactor", false);
    EXPECT_THROW(load.set("qratio", 1.0, units::second), InvalidParameterValue);
    EXPECT_FALSE(load.getFlag("usepowerfactor"));
    EXPECT_DOUBLE_EQ(load.get("qratio"), 0.7);
    load.set("qratio", 70.0, units::percent);
    EXPECT_NEAR(load.get("qratio"), 0.7, 1e-6);
    EXPECT_TRUE(load.getFlag("usepowerfactor"));
}

TEST(ExponentialParameterTests, RestoresParametersAndAssociatedOperatingValuesWithoutSolving)
{
    loads::ExponentialLoad load(0.5, 0.2);
    load.set("alpha", 2.0);
    load.set("period", 0.25);
    const auto originalPower = load.getRealPower(0.9);
    const auto saved = ModelFieldSnapshot::capture(load);
    load.set("basepower", 200.0);
    load.set("p", 0.9);
    load.set("alpha", 4.0);
    load.set("period", 2.0);
    load.disable();
    saved.restore(load);
    EXPECT_DOUBLE_EQ(load.get("basepower"), 100.0);
    EXPECT_DOUBLE_EQ(load.get("p"), 0.5);
    EXPECT_DOUBLE_EQ(load.get("q"), 0.2);
    EXPECT_DOUBLE_EQ(load.get("alphap"), 2.0);
    EXPECT_DOUBLE_EQ(load.get("period"), 0.25);
    EXPECT_TRUE(load.isEnabled());
    EXPECT_DOUBLE_EQ(load.getRealPower(0.9), originalPower);
}

TEST(ExponentialParameterTests, StateRestoreRetainsParametersAndRestoresFlagsAndLocalMemory)
{
    loads::ExponentialLoad load(0.5, 0.2);
    load.set("alpha", 2.0);
    load.set("period", 0.25);
    load.set("nextupdatetime", 1.25);
    load.setFlag("usepowerfactor", true);
    load.setFlag("fixedshunt", true);
    const auto operatingPoint = ModelFieldSnapshot::capture(load, FieldSnapshotLevel::STATE);
    load.set("p", 0.9);
    load.set("alpha", 3.0);
    load.set("period", 2.0);
    load.set("nextupdatetime", 4.0);
    load.set("qratio", 1.3);
    load.setFlag("usepowerfactor", false);
    load.setFlag("fixedshunt", false);
    operatingPoint.restore(load);
    EXPECT_DOUBLE_EQ(load.get("p"), 0.9);
    EXPECT_DOUBLE_EQ(load.get("alphap"), 3.0);
    EXPECT_DOUBLE_EQ(load.get("period"), 2.0);
    EXPECT_DOUBLE_EQ(load.get("qratio"), 1.3);
    EXPECT_DOUBLE_EQ(load.get("nextupdate"), 1.25);
    EXPECT_TRUE(load.getFlag("usepowerfactor"));
    EXPECT_TRUE(load.getFlag("fixedshunt"));

    auto edited = ModelFieldSnapshot::capture(load);
    const auto field =
        std::find_if(edited.mFields.begin(), edited.mFields.end(), [](const auto& value) {
            return value.mDeclaringClass == "ExponentialLoad" && value.mName == "alphap";
        });
    ASSERT_NE(field, edited.mFields.end());
    field->mValue = std::numeric_limits<double>::quiet_NaN();
    EXPECT_NO_THROW(edited.restore(load));
    EXPECT_TRUE(std::isnan(load.get("alphap")));
}

}  // namespace griddyn
