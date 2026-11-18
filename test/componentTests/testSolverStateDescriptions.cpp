/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ModelParameter.hpp"
#include "core/ObjectFactory.hpp"
#include "griddyn/Governor.h"
#include "griddyn/blocks/LeadLagBlock.h"
#include "griddyn/blocks/PidBlock.h"
#include "griddyn/blocks/TransferFunctionBlock.h"
#include "griddyn/exciters/ExciterAC7B.h"
#include "griddyn/exciters/ExciterAC8B.h"
#include "griddyn/exciters/ExciterEXAC1.h"
#include "griddyn/exciters/ExciterEXAC2.h"
#include "griddyn/governors/GovernorGPWSCC.h"
#include "griddyn/governors/GovernorGast.h"
#include "griddyn/governors/GovernorHydro.h"
#include "griddyn/loads/MotorLoad3.h"
#include "griddyn/loads/MotorLoad5.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/relays/BusMeasurementSensor.h"
#include "griddyn/renewables/DistributedConverter.h"
#include "griddyn/renewables/GridFormingConverter.h"
#include "griddyn/renewables/REECA1.h"
#include "griddyn/renewables/REECC1.h"
#include "griddyn/renewables/REPCA1.h"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
namespace {
    class StateBase: public GridComponent {
      public:
        bool mEnabledVelocity = true;
        const ModelDescription& getModelDescription() const override
        {
            static const auto description = ModelDescription::define<StateBase>(
                "StateBase",
                &GridComponent::getModelDescription(),
                {state({"position", "x"}, units::m, 0),
                 state<StateBase>({"velocity", "v"}, units::m / units::s, [](const auto& model) {
                     return model.mEnabledVelocity ? 1 : kInvalidLocation;
                 })});
            return description;
        }
    };

    class StateDerived: public StateBase {
      public:
        bool mEnabledState = true;
        double mSetting = 0.0;
        StateDerived() { offsets.local().local.algSize = 3; }
        const ModelDescription& getModelDescription() const override
        {
            static const auto description = ModelDescription::define<StateDerived>(
                "StateDerived",
                &StateBase::getModelDescription(),
                {parameter({"position"}, &StateDerived::mSetting, units::m, "Position setting"),
                 state<StateDerived>({"position", "displacement"},
                                     units::m,
                                     [](const auto& model) {
                                         return model.mEnabledState ? 2 : kInvalidLocation;
                                     }),
                 state({"output"}, units::one, 0),
                 state("documentation_only", units::one)});
            return description;
        }
    };

    class GroupedStates: public GridComponent {
      public:
        GroupedStates()
        {
            auto& local = offsets.local().local;
            local.vSize = local.aSize = local.algSize = local.diffSize = 1;
        }
        const ModelDescription& getModelDescription() const override
        {
            static const auto description =
                ModelDescription::define<GroupedStates>("GroupedStates",
                                                        &GridComponent::getModelDescription(),
                                                        {state({"voltage"}, units::puV, 0),
                                                         state({"angle"}, units::rad, 1),
                                                         state({"aux"}, units::one, 2),
                                                         state({"memory"}, units::one, 3)});
            return description;
        }
    };

    class NamedOutputBlock: public GridBlock {
      public:
        void renameOutput(const std::string& nameValue) { outputName = nameValue; }
    };

    class ModeOwnedState: public GridComponent {
      public:
        const ModelDescription& getModelDescription() const override
        {
            static const auto description = ModelDescription::define<ModeOwnedState>(
                "ModeOwnedState",
                &GridComponent::getModelDescription(),
                {state<ModeOwnedState>({"operating_point"},
                                       units::one,
                                       [](const auto& model, const auto& mode) {
                                           return isDynamic(mode) ?
                                               kInvalidLocation :
                                               model.offsets.getAlgOffset(mode);
                                       })});
            return description;
        }
        StateSizes localStateSizes(const SolverMode& mode) const override
        {
            StateSizes sizes;
            if (!isDynamic(mode)) {
                sizes.algSize = 1;
            }
            return sizes;
        }
    };

    void checkLayout(GridComponent& model, const stringVec& expected, index_t algebraicCount)
    {
        ASSERT_EQ(model.solverStateNames(), expected);
        ASSERT_EQ(model.getModelDescription().solverStateNames(model), expected);
        ASSERT_EQ(model.stateSize(cLocalSolverMode), expected.size());
        for (const auto& mode : {cDaeSolverMode, cDynAlgSolverMode, cDynDiffSolverMode}) {
            model.loadStateSizes(mode);
            model.setOffset(11, mode);
            stringVec labels;
            model.getStateName(labels, mode);
            for (index_t slot = 0; std::cmp_less(slot, expected.size()); ++slot) {
                const bool algebraic = slot < algebraicCount;
                const bool present = algebraic ? hasAlgebraic(mode) : hasDifferential(mode);
                const index_t index = present ?
                    11 + slot - ((!hasAlgebraic(mode)) ? algebraicCount : 0) :
                    kNullLocation;
                const auto& name = expected[slot];
                SCOPED_TRACE(name);
                EXPECT_EQ(model.findIndex(name, mode), index);
                const auto* field = model.getModelDescription().find(name, ModelFieldKind::STATE);
                ASSERT_NE(field, nullptr);
                EXPECT_EQ(model.getModelDescription().stateIndex(model, name), slot);
                if (field->kind() != ModelFieldKind::INDEXED_STATE) {
                    for (const auto& alias : field->names()) {
                        EXPECT_EQ(model.findIndex(alias, mode), index);
                    }
                }
                if (present) {
                    ASSERT_LT(static_cast<std::size_t>(index), labels.size());
                    EXPECT_EQ(labels[index], model.getName() + ':' + name);
                }
            }
        }
    }
}  // namespace

TEST(SolverStateDescriptionTests, InheritanceAliasesInactiveStatesAndSparseLayouts)
{
    StateDerived model;
    EXPECT_EQ(model.solverStateNames(), (stringVec{"output", "velocity", "position"}));
    const auto& description = model.getModelDescription();
    EXPECT_EQ(description.stateIndex(model, "x"), 2);
    EXPECT_EQ(description.stateIndex(model, "displacement"), 2);
    EXPECT_EQ(description.stateIndex(model, "v"), 1);
    EXPECT_FALSE(description.stateIndex(model, "documentation_only"));
    EXPECT_FALSE(description.stateIndex(model, "missing"));
    model.mEnabledState = false;
    EXPECT_EQ(model.solverStateNames(), (stringVec{"output", "velocity"}));
    EXPECT_EQ(description.stateIndex(model, "x"), kInvalidLocation);
    EXPECT_EQ(model.findIndex("x", cLocalSolverMode), kInvalidLocation);
    model.mEnabledState = true;
    model.loadStateSizes(cDaeSolverMode);
    model.setOffset(7, cDaeSolverMode);
    EXPECT_EQ(model.findIndex("x", cDaeSolverMode), 9);
    EXPECT_EQ(model.findIndex("v", cDaeSolverMode), 8);
    model.mEnabledVelocity = false;
    EXPECT_EQ(model.solverStateNames(), (stringVec{"output", "", "position"}));
    stringVec labels;
    model.getStateName(labels, cDaeSolverMode);
    ASSERT_GE(labels.size(), 10U);
    EXPECT_EQ(labels[8], model.getName() + ":alg_state_1");
    EXPECT_EQ(labels[9], model.getName() + ":position");
    labels[7] = "existing label";
    labels[8].clear();
    labels[9].clear();
    model.getStateName(labels, cDaeSolverMode);
    EXPECT_EQ(labels[7], "existing label");
    EXPECT_EQ(labels[9], model.getName() + ":position");
    const auto snapshot = ModelFieldSnapshot::capture(model);
    EXPECT_EQ(std::count_if(snapshot.mFields.begin(),
                            snapshot.mFields.end(),
                            [](const auto& field) { return field.mName == "position"; }),
              1);
    StateBase other;
    EXPECT_THROW(static_cast<void>(
                     description.find("displacement", ModelFieldKind::STATE)->stateIndex(other)),
                 InvalidParameterValue);
}

TEST(SolverStateDescriptionTests, FixedGovernorLayoutAndAliases)
{
    governors::GovernorGast model;
    model.dynInitializeA(timeZero, 0);
    checkLayout(model, {"pmech", "valve", "turbine", "temperature"}, 1);
}

TEST(SolverStateDescriptionTests, AdditionalFixedLayoutsUseSharedLookup)
{
    governors::GovernorGPWSCC gpwscc;
    gpwscc.dynInitializeA(timeZero, 0);
    checkLayout(gpwscc,
                {"pmech",
                 "td_filter",
                 "pid_integral",
                 "derivative_filter",
                 "pelec_filter",
                 "valve",
                 "gate",
                 "turbine"},
                1);
    governors::GovernorHydro hydro;
    hydro.dynInitializeA(timeZero, 0);
    checkLayout(hydro, {"pmech", "leadlag", "gate_lag", "waterway"}, 1);
    exciters::ExciterAC7B ac7;
    ac7.dynInitializeA(timeZero, 0);
    checkLayout(ac7, {"efd", "vmeas", "pidint", "pidder", "piint", "xf3", "ve"}, 1);
    exciters::ExciterAC8B ac8;
    ac8.dynInitializeA(timeZero, 0);
    checkLayout(ac8, {"efd", "vmeas", "pidint", "pidder", "vr", "ve"}, 1);
}

TEST(SolverStateDescriptionTests, VoltageAngleAlgebraicAndDifferentialGroups)
{
    GroupedStates model;
    EXPECT_EQ(model.solverStateNames(), (stringVec{"voltage", "angle", "aux", "memory"}));
    // SolverOffsets places angles before voltages, unlike the object's local name order.
    for (const auto& mode :
         {cDaeSolverMode, cDynAlgSolverMode, cDynDiffSolverMode, cPflowSolverMode}) {
        model.loadStateSizes(mode);
        model.setOffset(11, mode);
        EXPECT_EQ(model.findIndex("voltage", mode), hasAlgebraic(mode) ? 12 : kNullLocation);
        EXPECT_EQ(model.findIndex("angle", mode), hasAlgebraic(mode) ? 11 : kNullLocation);
        EXPECT_EQ(model.findIndex("aux", mode), hasAlgebraic(mode) ? 13 : kNullLocation);
        EXPECT_EQ(model.findIndex("memory", mode),
                  hasDifferential(mode) ? (hasAlgebraic(mode) ? 14 : 11) : kNullLocation);
        stringVec labels;
        model.getStateName(labels, mode);
        if (hasAlgebraic(mode)) {
            ASSERT_GE(labels.size(), 14U);
            EXPECT_EQ(labels[11], model.getName() + ":angle");
            EXPECT_EQ(labels[12], model.getName() + ":voltage");
            EXPECT_EQ(labels[13], model.getName() + ":aux");
        }
        if (hasDifferential(mode)) {
            EXPECT_EQ(labels[hasAlgebraic(mode) ? 14 : 11], model.getName() + ":memory");
        }
    }
}

TEST(SolverStateDescriptionTests, Exac1AllOptionalLayoutsAndInheritedExac2)
{
    for (const bool transducer : {false, true}) {
        for (const bool leadLag : {false, true}) {
            for (const bool regulator : {false, true}) {
                SCOPED_TRACE(std::to_string(transducer) + std::to_string(leadLag) +
                             std::to_string(regulator));
                for (const bool derived : {false, true}) {
                    exciters::ExciterEXAC1 base;
                    exciters::ExciterEXAC2 child;
                    auto& model = derived ? child : base;
                    model.set("tr", transducer ? 0.02 : 0.0);
                    model.set("tb", leadLag ? 0.5 : 0.0);
                    model.set("tc", 0.0);
                    model.set("ta", regulator ? 0.02 : 0.0);
                    model.dynInitializeA(timeZero, 0);
                    stringVec names{"efd"};
                    if (!regulator) {
                        names.emplace_back("va");
                    }
                    if (transducer) {
                        names.emplace_back("vmeas");
                    }
                    if (leadLag) {
                        names.emplace_back("ll");
                    }
                    if (regulator) {
                        names.emplace_back("va");
                    }
                    names.emplace_back("ve");
                    names.emplace_back("wf");
                    checkLayout(model, names, regulator ? 1 : 2);
                    if (!transducer) {
                        EXPECT_EQ(model.findIndex("vmeas", cDaeSolverMode), kInvalidLocation);
                    }
                    if (!leadLag) {
                        EXPECT_EQ(model.findIndex("leadlag", cDaeSolverMode), kInvalidLocation);
                    }
                }
            }
        }
    }
}

TEST(SolverStateDescriptionTests, LeadLagLayoutTracksLimitConfiguration)
{
    for (const bool limited : {false, true}) {
        blocks::LeadLagBlock model(2.0, 0.5);
        model.setFlag("use_limits", limited);
        model.dynInitializeA(timeZero, 0);
        checkLayout(model,
                    limited ? stringVec{"output", "unlimited_output", "lag_state"} :
                              stringVec{"unlimited_output", "lag_state"},
                    limited ? 2 : 1);
    }
}

TEST(SolverStateDescriptionTests, PreservesRegisteredModelLayoutsAcrossZeroTimeConstants)
{
    std::ifstream input(GRIDDYN_TEST_DIRECTORY "/model_descriptions/solver_layouts.json");
    ASSERT_TRUE(input.is_open());
    nlohmann::json baseline;
    input >> baseline;
    auto factory = CoreObjectFactory::instance();
    for (const auto& entry : baseline) {
        const auto category = entry.at("category").get<std::string>();
        const auto type = entry.at("type").get<std::string>();
        const auto zero = entry.at("zero").get<std::string>();
        SCOPED_TRACE(::testing::Message() << category << ':' << type << ':' << zero);
        std::unique_ptr<CoreObject> object(factory->createObject(category, type));
        auto* model = dynamic_cast<GridComponent*>(object.get());
        ASSERT_NE(model, nullptr);
        if (!zero.empty()) {
            model->set(zero, 0.0);
        }
        model->dynInitializeA(timeZero, 0);
        EXPECT_EQ(model->getModelDescription().name(), entry.at("model").get<std::string>());
        EXPECT_EQ(model->stateSize(cLocalSolverMode), entry.at("size").get<count_t>());
        EXPECT_EQ(model->solverStateNames(), entry.at("names").get<stringVec>());
        model->loadStateSizes(cDaeSolverMode);
        model->setOffset(7, cDaeSolverMode);
        for (const auto& name : model->solverStateNames()) {
            ASSERT_FALSE(name.empty());
            const auto index = model->findIndex(name, cDaeSolverMode);
            EXPECT_NE(index, kInvalidLocation);
            if (category != "bus") {
                EXPECT_NE(index, kNullLocation);
            }
        }
    }
}

TEST(SolverStateDescriptionTests, IndexedFamiliesTrackOrderAndRejectMalformedNames)
{
    for (const int order : {0, 1, 8, 20}) {
        blocks::TransferFunctionBlock model(order);
        model.dynInitializeA(timeZero, 0);
        stringVec names{"output"};
        for (int index = 0; index < order; ++index) {
            names.push_back("x" + std::to_string(index));
        }
        checkLayout(model, names, 1);
        for (const auto& name : {"x", "x-1", "x1oops", "x999999999999999999999999999"}) {
            EXPECT_EQ(model.findIndex(name, cDaeSolverMode), kInvalidLocation);
        }
        EXPECT_EQ(model.findIndex("x" + std::to_string(order), cDaeSolverMode), kInvalidLocation);
    }
}

TEST(SolverStateDescriptionTests, ConfiguredOutputNamesAndPidMemoryUseTheSameBindings)
{
    NamedOutputBlock model;
    model.renameOutput("measured_power");
    model.dynInitializeA(timeZero, 0);
    EXPECT_EQ(model.solverStateNames(), (stringVec{"measured_power"}));
    model.loadStateSizes(cDaeSolverMode);
    model.setOffset(13, cDaeSolverMode);
    EXPECT_EQ(model.findIndex("measured_power", cDaeSolverMode), 13);
    EXPECT_EQ(model.findIndex("output", cDaeSolverMode), 13);
    stringVec labels;
    model.getStateName(labels, cDaeSolverMode);
    EXPECT_EQ(labels[13], model.getName() + ":measured_power");
    model.renameOutput("renamed_power");
    EXPECT_EQ(model.findIndex("measured_power", cDaeSolverMode), kInvalidLocation);
    EXPECT_EQ(model.findIndex("renamed_power", cDaeSolverMode), 13);
    for (const bool limited : {false, true}) {
        blocks::PidBlock pid;
        pid.setFlag("use_limits", limited);
        pid.dynInitializeA(timeZero, 0);
        const stringVec expected = limited ?
            stringVec{"output", "ramp_limits", "unlimited_output", "deriv_delay", "integral"} :
            stringVec{"output", "deriv_delay", "integral"};
        checkLayout(pid, expected, 0);
        EXPECT_EQ(pid.findIndex("derivative", cDaeSolverMode), 11 + (limited ? 3 : 1));
        EXPECT_EQ(pid.findIndex("integral", cDaeSolverMode), 11 + (limited ? 4 : 2));
    }
}

TEST(SolverStateDescriptionTests, MotorStatesMoveBetweenPowerFlowAndDynamicPartitions)
{
    loads::MotorLoad5 model;
    model.loadStateSizes(cLocalSolverMode);
    EXPECT_EQ(model.solverStateNames(),
              (stringVec{"ir", "im", "slip", "erp", "emp", "erpp", "empp"}));
    for (const auto& mode :
         {cPflowSolverMode, cDaeSolverMode, cDynAlgSolverMode, cDynDiffSolverMode}) {
        model.loadStateSizes(mode);
        model.setOffset(17, mode);
        const auto names = model.solverStateNames();
        stringVec labels;
        model.getStateName(labels, mode);
        for (index_t slot = 0; std::cmp_less(slot, names.size()); ++slot) {
            const bool algebraic = !isDynamic(mode) || slot < 2;
            const bool present = algebraic ? hasAlgebraic(mode) : hasDifferential(mode);
            const index_t expected =
                present ? 17 + slot - ((!hasAlgebraic(mode)) ? 2 : 0) : kNullLocation;
            EXPECT_EQ(model.findIndex(names[slot], mode), expected);
            if (present) {
                EXPECT_EQ(labels[expected], model.getName() + ':' + names[slot]);
            }
        }
    }
}

TEST(SolverStateDescriptionTests, ModeOwnedStatesAndChildReferencesHaveDistinctLabels)
{
    ModeOwnedState operatingPoint;
    EXPECT_TRUE(operatingPoint.solverStateNames().empty());
    operatingPoint.loadStateSizes(cPflowSolverMode);
    operatingPoint.setOffset(9, cPflowSolverMode);
    EXPECT_EQ(operatingPoint.findIndex("operating_point", cPflowSolverMode), 9);
    EXPECT_EQ(operatingPoint.findIndex("operating_point", cDaeSolverMode), kInvalidLocation);
    stringVec labels;
    operatingPoint.getStateName(labels, cPflowSolverMode);
    EXPECT_EQ(labels[9], operatingPoint.getName() + ":operating_point");
    Governor governor;
    governor.dynInitializeA(timeZero, 0);
    governor.loadStateSizes(cDaeSolverMode);
    governor.setOffset(7, cDaeSolverMode);
    EXPECT_TRUE(governor.solverStateNames().empty());
    for (const auto& name : {"pm", "dbo", "w"}) {
        const auto* field = governor.getModelDescription().find(name, ModelFieldKind::STATE);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->kind(), ModelFieldKind::STATE_REFERENCE);
        EXPECT_FALSE(field->editable());
        EXPECT_NE(governor.findIndex(name, cDaeSolverMode), kInvalidLocation);
    }
    labels.clear();
    governor.getStateName(labels, cDaeSolverMode);
    for (const auto& label : labels) {
        EXPECT_FALSE(label.ends_with(":pm") || label.ends_with(":dbo") || label.ends_with(":w"));
    }
}

TEST(SolverStateDescriptionTests, EveryRegisteredNativeStateHasALocationBinding)
{
    const std::set<std::string> categories{"area",
                                           "bus",
                                           "load",
                                           "link",
                                           "generator",
                                           "exciter",
                                           "governor",
                                           "genmodel",
                                           "stabilizer",
                                           "pss",
                                           "block",
                                           "relay",
                                           "limiter",
                                           "excitationlimiter",
                                           "vcomp",
                                           "voltagecompensator",
                                           "converter",
                                           "source",
                                           "controller"};
    auto factory = CoreObjectFactory::instance();
    std::set<std::string> seen;
    std::size_t states = 0;
    for (const auto& category : factory->getFactoryNames()) {
        if (!categories.contains(category)) {
            continue;
        }
        for (const auto& type : factory->getTypeNames(category)) {
            if (type.find("fmi") != std::string::npos || type.find("helics") != std::string::npos) {
                continue;
            }
            std::unique_ptr<CoreObject> object(factory->createObject(category, type));
            const auto* model = dynamic_cast<const GridComponent*>(object.get());
            if (model == nullptr) {
                continue;
            }
            const auto& description = model->getModelDescription();
            if (!seen.insert(std::string(description.name())).second) {
                continue;
            }
            SCOPED_TRACE(description.name());
            for (const auto* table = &description; table != nullptr; table = table->parent()) {
                for (const auto& field : table->fields()) {
                    if (!field.isState()) {
                        continue;
                    }
                    SCOPED_TRACE(field.name());
                    EXPECT_TRUE(field.hasStateBinding());
                    EXPECT_FALSE(field.editable());
                    ++states;
                }
            }
        }
    }
    EXPECT_GT(seen.size(), 80U);
    EXPECT_GT(states, 100U);
}

TEST(SolverStateDescriptionTests, BlockLimitCombinationsNameOnlyAllocatedSlots)
{
    const std::set<std::string> classes{"GridBlock",
                                        "ControlBlock",
                                        "DerivativeBlock",
                                        "FilteredDerivativeBlock",
                                        "PidBlock",
                                        "TransferFunctionBlock",
                                        "DelayBlock",
                                        "IntegralBlock",
                                        "DeadbandBlock"};
    auto factory = CoreObjectFactory::instance();
    std::set<std::string> seen;
    for (const auto& type : factory->getTypeNames("block")) {
        std::unique_ptr<CoreObject> probe(factory->createObject("block", type));
        const auto name = std::string(probe->getModelDescription().name());
        if (!classes.contains(name) || !seen.insert(name).second) {
            continue;
        }
        for (int mask = 0; mask < 4; ++mask) {
            SCOPED_TRACE(name + ':' + std::to_string(mask));
            std::unique_ptr<CoreObject> object(factory->createObject("block", type));
            auto* model = dynamic_cast<GridComponent*>(object.get());
            ASSERT_NE(model, nullptr);
            model->setFlag("use_limits", (mask & 1) != 0);
            model->setFlag("use_ramp_limits", (mask & 2) != 0);
            model->dynInitializeA(timeZero, 0);
            const auto names = model->solverStateNames();
            EXPECT_EQ(names.size(), static_cast<std::size_t>(model->stateSize(cLocalSolverMode)));
            EXPECT_EQ(std::set<std::string>(names.begin(), names.end()).size(), names.size());
            checkLayout(*model, names, model->algSize(cLocalSolverMode));
        }
    }
    EXPECT_GE(seen.size(), 7U);
}

TEST(SolverStateDescriptionTests, ConverterAndElectricalControllerVariantsHaveCompleteLayouts)
{
    using Variant = GridFormingConverter::Variant;
    for (const auto variant : {Variant::cv1, Variant::cv2, Variant::f1, Variant::f2, Variant::f3}) {
        SCOPED_TRACE(static_cast<int>(variant));
        GridFormingConverter model(variant);
        if (variant == Variant::f2) {
            model.set("pll", std::string_view{"pll2a"});
        }
        model.dynInitializeA(timeZero, 0);
        stringVec names;
        if (variant == Variant::cv1) {
            names = {"Pe", "Qe", "dw", "delta", "PIvd", "PIvq", "PIId", "PIIq", "ud", "uq"};
        } else if (variant == Variant::cv2) {
            names = {"Pe", "Qe", "dw", "delta", "PIvd", "PIvq", "Id", "Iq"};
        } else {
            names = {"Pe",
                     "Qe",
                     "delta",
                     "Psen",
                     "Qsen",
                     "Psig",
                     "Qsig",
                     "PIplim",
                     "PIqlim",
                     "PIvd",
                     "PIvq",
                     "PIId",
                     "PIIq",
                     "ud",
                     "uq"};
            if (variant == Variant::f2) {
                names.emplace_back("INTw");
            }
            if (variant == Variant::f3) {
                names.emplace_back("vref2");
            }
        }
        checkLayout(model, names, 2);
    }
    for (const auto variant : {DistributedConverter::Variant::pv,
                               DistributedConverter::Variant::storage,
                               DistributedConverter::Variant::ev1,
                               DistributedConverter::Variant::ev2}) {
        DistributedConverter model(variant, "distributed");
        model.dynInitializeA(timeZero, 0);
        stringVec names{"Pe", "Qe", "Ip", "Iq"};
        if (variant != DistributedConverter::Variant::pv) {
            names.emplace_back("SOC");
        }
        checkLayout(model, names, 2);
    }
    for (const bool qControl : {false, true}) {
        for (const bool vControl : {false, true}) {
            REECA1 model;
            model.set("qflag", qControl ? 1.0 : 0.0);
            model.set("vflag", vControl ? 1.0 : 0.0);
            model.dynInitializeA(timeZero, 0);
            stringVec names{"Ipcmd", "Iqcmd", "Vf", "Pf", "Pord"};
            if (qControl) {
                names.emplace_back("Vpi");
            } else {
                names.emplace_back("Qf");
            }
            if (qControl && vControl) {
                names.emplace_back("Qpi");
            }
            checkLayout(model, names, 2);
        }
    }
    REECC1 storage;
    storage.set("t", 10.0);
    storage.set("socmax", 0.9);
    storage.set("socmin", 0.1);
    storage.set("socini", 0.5);
    storage.set("pfflag", 1.0);
    storage.dynInitializeA(timeZero, 0);
    checkLayout(storage, {"Ipcmd", "Iqcmd", "Vf", "Pf", "Pord", "Qf", "SOC", "PgenFilter"}, 2);
    for (const bool frequencyControl : {false, true}) {
        REPCA1 model;
        model.set("fflag", frequencyControl ? 1.0 : 0.0);
        model.dynInitializeA(timeZero, 0);
        stringVec names{"Pext", "Qext", "Vf", "Qf", "Qi", "Qlag"};
        if (frequencyControl) {
            names.insert(names.end(), {"PbranchFilter", "Pintegral", "Plag"});
        }
        checkLayout(model, names, 2);
    }
}

TEST(SolverStateDescriptionTests, MeasurementModelsDeclareTheirNamesInsteadOfStoringCopies)
{
    AcBus bus("source");
    PLL1Sensor first;
    first.setSource(&bus);
    first.dynInitializeA(timeZero, 0);
    checkLayout(first, {"df", "filtered_angle", "pi_integral", "ae", "am"}, 1);
    PLL2Sensor second;
    second.setSource(&bus);
    second.dynInitializeA(timeZero, 0);
    checkLayout(second, {"df", "pi_integral", "am"}, 1);
    BusROCOFSensor rocof;
    rocof.setSource(&bus);
    rocof.dynInitializeA(timeZero, 0);
    checkLayout(rocof, {"df", "dfdt", "angle_lag", "angle_washout", "frequency_washout"}, 2);
}

}  // namespace griddyn
