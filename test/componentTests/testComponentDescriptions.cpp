/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "griddyn/ExcitationLimiter.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/VoltageCompensator.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/generators/VariableGenerator.h"
#include "griddyn/limiters/ExcitationLimiterMNLEX2.h"
#include "griddyn/limiters/ExcitationLimiterOEL3C.h"
#include "griddyn/limiters/ExcitationLimiterOEL4C.h"
#include "griddyn/limiters/ExcitationLimiterUEL1.h"
#include "griddyn/limiters/ExcitationLimiterUEL2C.h"
#include "griddyn/links/AcDcConverter.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/links/Hvdc.h"
#include "griddyn/links/LongLine.h"
#include "griddyn/links/RawDcLine.h"
#include "griddyn/links/Subsystem.h"
#include "griddyn/links/ThreeWindingTransformer.h"
#include "griddyn/links/VSCShunt.h"
#include "griddyn/links/ZBreaker.h"
#include "griddyn/loads/AggregateLoad.h"
#include "griddyn/loads/ApproximatingLoad.h"
#include "griddyn/loads/CIMLoad.h"
#include "griddyn/loads/CompositeLoad.h"
#include "griddyn/loads/ElectronicLoad.h"
#include "griddyn/loads/ExponentialLoad.h"
#include "griddyn/loads/FDepLoad.h"
#include "griddyn/loads/FileLoad.h"
#include "griddyn/loads/FrequencySensitiveLoad.h"
#include "griddyn/loads/GridLabDLoad.h"
#include "griddyn/loads/IEELLoad.h"
#include "griddyn/loads/MotorDLoad.h"
#include "griddyn/loads/MotorLoad.h"
#include "griddyn/loads/MotorLoad3.h"
#include "griddyn/loads/MotorLoad5.h"
#include "griddyn/loads/RampLoad.h"
#include "griddyn/loads/ShuntTD.h"
#include "griddyn/loads/SourceLoad.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ThreePhaseLoad.h"
#include "griddyn/loads/WECCMotor3.h"
#include "griddyn/loads/WSCCLoad.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include "griddyn/primary/InfiniteBus.h"
#include "griddyn/renewables/DistributedConverter.h"
#include "griddyn/renewables/EPCGEN.h"
#include "griddyn/renewables/GridFormingConverter.h"
#include "griddyn/renewables/REECA1.h"
#include "griddyn/renewables/REECA1E.h"
#include "griddyn/renewables/REECA1G.h"
#include "griddyn/renewables/REECB1.h"
#include "griddyn/renewables/REECC1.h"
#include "griddyn/renewables/REGCA1.h"
#include "griddyn/renewables/REGCP1.h"
#include "griddyn/renewables/REPCA1.h"
#include "griddyn/renewables/WT3E1.h"
#include "griddyn/renewables/WT3G1.h"
#include "griddyn/renewables/WT4E1.h"
#include "griddyn/renewables/WT4G1.h"
#include "griddyn/renewables/WTARA1.h"
#include "griddyn/renewables/WTDS.h"
#include "griddyn/renewables/WTDTA1.h"
#include "griddyn/renewables/WTPTA1.h"
#include "griddyn/renewables/WTTQA1.h"
#include "griddyn/voltagecompensators/VoltageCompensatorIeeeVC.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace griddyn;

namespace {
std::vector<std::unique_ptr<CoreObject>> componentModels()
{
    std::vector<std::unique_ptr<CoreObject>> models;
    models.emplace_back(std::make_unique<griddyn::ExcitationLimiter>());
    models.emplace_back(std::make_unique<griddyn::Generator>());
    models.emplace_back(std::make_unique<griddyn::GridBus>());
    models.emplace_back(std::make_unique<griddyn::Link>());
    models.emplace_back(std::make_unique<griddyn::VoltageCompensator>());
    models.emplace_back(std::make_unique<griddyn::loads::AggregateLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::ApproximatingLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::CIM5>());
    models.emplace_back(std::make_unique<griddyn::loads::CIM6>());
    models.emplace_back(std::make_unique<griddyn::loads::CompositeLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::ElectronicLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::ExponentialLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::FDepLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::FileLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::FrequencySensitiveLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::GridLabDLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::IEELLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::MotorDLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::MotorLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::MotorLoad3>());
    models.emplace_back(std::make_unique<griddyn::loads::MotorLoad5>());
    models.emplace_back(std::make_unique<griddyn::loads::RampLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::ShuntTD>());
    models.emplace_back(std::make_unique<griddyn::loads::SourceLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::Svd>());
    models.emplace_back(std::make_unique<griddyn::loads::ThreePhaseLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::WECCMotor3>());
    models.emplace_back(std::make_unique<griddyn::loads::WSCCLoad>());
    models.emplace_back(std::make_unique<griddyn::ZipLoad>());
    models.emplace_back(std::make_unique<griddyn::links::AcDcConverter>());
    models.emplace_back(std::make_unique<griddyn::AcLine>());
    models.emplace_back(std::make_unique<griddyn::links::AdjustableTransformer>());
    models.emplace_back(std::make_unique<griddyn::links::DcLink>());
    models.emplace_back(std::make_unique<griddyn::links::Hvdc>());
    models.emplace_back(std::make_unique<griddyn::links::LongLine>());
    models.emplace_back(std::make_unique<griddyn::links::RawDcLine>());
    models.emplace_back(std::make_unique<griddyn::Subsystem>());
    models.emplace_back(std::make_unique<griddyn::links::ThreeWindingTransformer>());
    models.emplace_back(std::make_unique<griddyn::links::VSCShunt>());
    models.emplace_back(std::make_unique<griddyn::links::ZBreaker>());
    models.emplace_back(std::make_unique<griddyn::AcBus>());
    models.emplace_back(std::make_unique<griddyn::DcBus>());
    models.emplace_back(std::make_unique<griddyn::infiniteBus>());
    models.emplace_back(std::make_unique<griddyn::DynamicGenerator>());
    models.emplace_back(std::make_unique<griddyn::RenewableGenerator>());
    models.emplace_back(std::make_unique<griddyn::VariableGenerator>());
    models.emplace_back(std::make_unique<griddyn::limiters::ExcitationLimiterMNLEX2>());
    models.emplace_back(std::make_unique<griddyn::limiters::ExcitationLimiterOEL3C>());
    models.emplace_back(std::make_unique<griddyn::limiters::ExcitationLimiterOEL4C>());
    models.emplace_back(std::make_unique<griddyn::limiters::ExcitationLimiterUEL1>());
    models.emplace_back(std::make_unique<griddyn::limiters::ExcitationLimiterUEL2C>());
    models.emplace_back(std::make_unique<griddyn::voltagecompensators::VoltageCompensatorIeeeVC>());
    models.emplace_back(
        std::make_unique<griddyn::DistributedConverter>(DistributedConverter::Variant::pv,
                                                        "distributed"));
    models.emplace_back(std::make_unique<griddyn::PVD1>());
    models.emplace_back(std::make_unique<griddyn::ESD1>());
    models.emplace_back(std::make_unique<griddyn::EV1>());
    models.emplace_back(std::make_unique<griddyn::EV2>());
    models.emplace_back(std::make_unique<griddyn::EPCGEN>());
    models.emplace_back(std::make_unique<griddyn::GridFormingConverter>());
    models.emplace_back(std::make_unique<griddyn::REGCV1>());
    models.emplace_back(std::make_unique<griddyn::REGCV2>());
    models.emplace_back(std::make_unique<griddyn::REGF1>());
    models.emplace_back(std::make_unique<griddyn::REGF2>());
    models.emplace_back(std::make_unique<griddyn::REGF3>());
    models.emplace_back(std::make_unique<griddyn::REECA1>());
    models.emplace_back(std::make_unique<griddyn::REECA1E>());
    models.emplace_back(std::make_unique<griddyn::REECA1G>());
    models.emplace_back(std::make_unique<griddyn::REECB1>());
    models.emplace_back(std::make_unique<griddyn::REECC1>());
    models.emplace_back(std::make_unique<griddyn::REGCA1>());
    models.emplace_back(std::make_unique<griddyn::REGCP1>());
    models.emplace_back(std::make_unique<griddyn::REPCA1>());
    models.emplace_back(std::make_unique<griddyn::WT3E1>());
    models.emplace_back(std::make_unique<griddyn::WT3G1>());
    models.emplace_back(std::make_unique<griddyn::WT4E1>());
    models.emplace_back(std::make_unique<griddyn::WT4G1>());
    models.emplace_back(std::make_unique<griddyn::WTARA1>());
    models.emplace_back(std::make_unique<griddyn::WTDS>());
    models.emplace_back(std::make_unique<griddyn::WTDTA1>());
    models.emplace_back(std::make_unique<griddyn::WTPTA1>());
    models.emplace_back(std::make_unique<griddyn::WTTQA1>());
    models.emplace_back(std::make_unique<griddyn::GridLoad>());
    models.emplace_back(std::make_unique<griddyn::loads::ExponentialLoad>());
    return models;
}
}  // namespace

TEST(ComponentDescriptionTests, ExactTypesAndFieldSnapshotsAcrossAllMigratedFamilies)
{
    for (const auto& model : componentModels()) {
        const auto& description = model->getModelDescription();
        SCOPED_TRACE(description.name());
        EXPECT_TRUE(description.describesExactType(*model));
        const auto parameters = ModelFieldSnapshot::capture(*model);
        EXPECT_NO_THROW(parameters.restore(*model));
        const auto retainedState = ModelFieldSnapshot::capture(*model, FieldSnapshotLevel::STATE);
        EXPECT_NO_THROW(retainedState.restore(*model));
    }
}

TEST(ComponentDescriptionTests, AliasLookupAndNumericGetAgreeWithDeclaredStorage)
{
    for (const auto& model : componentModels()) {
        const auto& description = model->getModelDescription();
        SCOPED_TRACE(description.name());
        for (const auto& field : description.fields()) {
            SCOPED_TRACE(field.name());
            for (const auto& alias : field.names()) {
                if (field.kind() == ModelFieldKind::STATE) {
                    EXPECT_EQ(description.find(alias, ModelFieldKind::STATE), &field);
                } else if (field.kind() == ModelFieldKind::INDEXED_COMMAND) {
                    EXPECT_EQ(description.find(alias + "0", ModelFieldKind::INDEXED_COMMAND), &field);
                } else {
                    EXPECT_EQ(description.findLocal(alias), &field);
                }
            }
            if (field.kind() == ModelFieldKind::PARAMETER &&
                field.valueType() != ModelFieldType::STRING) {
                const auto expected = description.tryGetLocal(*model, field.name());
                ASSERT_TRUE(expected.has_value());
                EXPECT_DOUBLE_EQ(model->get(field.name()), *expected);
                for (const auto& alias : field.names()) {
                    EXPECT_DOUBLE_EQ(model->get(alias), *expected);
                }
            }
        }
    }
}

TEST(ComponentDescriptionTests, NumericFlagsUseTheBooleanInterface)
{
    for (const auto& model : componentModels()) {
        const auto& description = model->getModelDescription();
        SCOPED_TRACE(description.name());
        for (const auto& field : description.fields()) {
            if (field.kind() != ModelFieldKind::FLAG) {
                continue;
            }
            SCOPED_TRACE(field.name());
            const bool original = std::get<bool>(field.value(*model));
            EXPECT_EQ(model->getFlag(field.name()), original);
            EXPECT_DOUBLE_EQ(model->get(field.name()), original ? 1.0 : 0.0);
            // Reapplying the current flag also exercises model-specific side effects safely.
            EXPECT_NO_THROW(model->set(field.name(), original ? 1.0 : 0.0));
            EXPECT_EQ(model->getFlag(field.name()), original);
        }
    }
}

TEST(ComponentDescriptionTests, IgnoreCommentsAndRejectNonfiniteBeforeUnknownNames)
{
    for (const auto& model : componentModels()) {
        SCOPED_TRACE(model->getModelDescription().name());
        const auto before = ModelFieldSnapshot::capture(*model);
        EXPECT_NO_THROW(model->set("", std::numeric_limits<double>::quiet_NaN()));
        EXPECT_NO_THROW(model->set("# comment", std::numeric_limits<double>::infinity()));
        const auto after = ModelFieldSnapshot::capture(*model);
        ASSERT_EQ(before.mFields.size(), after.mFields.size());
        for (std::size_t index = 0; index < before.mFields.size(); ++index) {
            EXPECT_EQ(before.mFields[index].mValue, after.mFields[index].mValue);
        }
        EXPECT_THROW(model->set("unknown_numeric_field", std::numeric_limits<double>::quiet_NaN()),
                     InvalidParameterValue);
    }
}

TEST(ComponentDescriptionTests, LoadCoefficientStorageAndFrequencyDependencies)
{
    loads::IEELLoad ieel;
    ieel.set("a7", 2.0);
    ieel.set("n1", 1.5);
    EXPECT_DOUBLE_EQ(ieel.get("a7"), 2.0);
    EXPECT_DOUBLE_EQ(ieel.get("n1"), 1.5);
    EXPECT_TRUE(ieel.checkFlag(USES_BUS_FREQUENCY));
    const auto saved = ModelFieldSnapshot::capture(ieel);
    ieel.set("a7", 0.0);
    EXPECT_FALSE(ieel.checkFlag(USES_BUS_FREQUENCY));
    saved.restore(ieel);
    EXPECT_DOUBLE_EQ(ieel.get("a7"), 2.0);
    EXPECT_DOUBLE_EQ(ieel.get("n1"), 1.5);
    // Snapshot restoration only restores fields; model reconstruction is a separate operation.
    ieel.set("a7", ieel.get("a7"));
    EXPECT_TRUE(ieel.checkFlag(USES_BUS_FREQUENCY));
    EXPECT_THROW(ieel.set("a9", 1.0), UnrecognizedParameter);

    loads::ElectronicLoad electronic;
    electronic.set("pfrq", 3.0);
    EXPECT_DOUBLE_EQ(electronic.get("a7"), 3.0);
    electronic.set("frel", 0.5);
    EXPECT_DOUBLE_EQ(electronic.get("frcel"), 0.5);
    EXPECT_THROW(electronic.set("frel", 1.1), InvalidParameterValue);

    loads::WSCCLoad wscc;
    wscc.set("p1", 25.0);
    wscc.set("lpd", 0.5);
    EXPECT_DOUBLE_EQ(wscc.get("p1"), 25.0);
    EXPECT_TRUE(wscc.checkFlag(USES_BUS_FREQUENCY));
    const auto wsccSaved = ModelFieldSnapshot::capture(wscc);
    wscc.set("p1", 50.0);
    wsccSaved.restore(wscc);
    EXPECT_DOUBLE_EQ(wscc.get("p1"), 25.0);
}

TEST(ComponentDescriptionTests, SharedValidatorsUnitsAndParentFallback)
{
    loads::MotorDLoad motor;
    motor.set("trst", 250.0, units::ms);
    EXPECT_NEAR(motor.get("Trst"), 0.25, 1e-6);
    EXPECT_NEAR(motor.get("trst", units::ms), 250.0, 1e-4);
    EXPECT_THROW(motor.set("frst", -0.1), InvalidParameterValue);
    EXPECT_THROW(motor.set("frst", 1.1), InvalidParameterValue);
    EXPECT_THROW(motor.set("vstall", 0.0), InvalidParameterValue);
    motor.set("p", 0.8);
    EXPECT_DOUBLE_EQ(motor.get("p"), 0.8);

    REGCA1 converter;
    converter.set("TG", 250.0, units::ms);
    EXPECT_NEAR(converter.get("tg"), 0.25, 1e-6);
    EXPECT_THROW(converter.set("tg", 1.0, units::MW), InvalidParameterValue);
    converter.setFlag("lvplsw", false);
    EXPECT_DOUBLE_EQ(converter.get("lvplsw"), 0.0);
    converter.set("lvplsw", 2.0);
    EXPECT_TRUE(converter.getFlag("lvplsw"));
}

TEST(ComponentDescriptionTests, FlagDispatchRetainsSchedulingAndInputLayoutChanges)
{
    loads::FileLoad schedule;
    schedule.set("absolute", 1.0);
    EXPECT_TRUE(schedule.getFlag("absolute"));
    EXPECT_DOUBLE_EQ(schedule.get("absolute"), 1.0);
    schedule.setFlag("relative", true);
    EXPECT_FALSE(schedule.getFlag("absolute"));
    schedule.set("step", 1.0);
    EXPECT_TRUE(schedule.getFlag("step"));
    schedule.setFlag("interpolate", true);
    EXPECT_FALSE(schedule.getFlag("step"));

    loads::ThreePhaseLoad phases;
    phases.set("three_phase_input", 1.0);
    EXPECT_TRUE(phases.getFlag("three_phase_input"));
    EXPECT_EQ(phases.numInputs(), 7);
    phases.setFlag("three_phase_inputs", false);
    EXPECT_DOUBLE_EQ(phases.get("three_phase_input"), 0.0);
    EXPECT_EQ(phases.numInputs(), 3);
}

TEST(ComponentDescriptionTests, LineAdmittanceTapUnitsAndWindingForwarding)
{
    AcLine line;
    line.set("r", 0.1);
    line.set("x", 0.2);
    EXPECT_DOUBLE_EQ(line.get("r"), 0.1);
    EXPECT_NEAR(line.get("z"), std::hypot(0.1, 0.2), 1e-12);
    line.set("tapangle", 90.0, units::deg);
    EXPECT_NEAR(line.get("tapangle", units::deg), 90.0, 1e-5);
    line.set("ratio", 1.1);
    EXPECT_DOUBLE_EQ(line.get("tap"), 1.1);
    const auto saved = ModelFieldSnapshot::capture(line);
    line.set("r", 0.3);
    saved.restore(line);
    EXPECT_DOUBLE_EQ(line.get("r"), 0.1);

    links::ThreeWindingTransformer transformer;
    transformer.set("r2", 0.02);
    transformer.set("tap3", 1.05);
    EXPECT_DOUBLE_EQ(transformer.get("r2"), 0.02);
    EXPECT_DOUBLE_EQ(transformer.get("tap3"), 1.05);
}

TEST(ComponentDescriptionTests, LimiterCurvesAndConfiguredThresholds)
{
    limiters::ExcitationLimiterUEL1 uel;
    uel.set("tu2", 100.0, units::ms);
    EXPECT_NEAR(uel.get("tu2"), 0.1, 1e-6);
    uel.set("ku_r", 2.0);
    EXPECT_DOUBLE_EQ(uel.get("kur"), 2.0);

    limiters::ExcitationLimiterUEL2C curve;
    curve.set("p0", 0.0);
    curve.set("q0", -0.2);
    curve.set("p1", 1.0);
    curve.set("q1", -0.1);
    EXPECT_DOUBLE_EQ(curve.get("q1"), -0.1);
    const auto snapshot = ModelFieldSnapshot::capture(curve);
    curve.set("q1", 0.0);
    snapshot.restore(curve);
    EXPECT_DOUBLE_EQ(curve.get("q1"), -0.1);
    EXPECT_THROW(curve.set("p7", 1.0), UnrecognizedParameter);

    ExcitationLimiter generic;
    generic.set("threshold", 0.9);
    EXPECT_NO_THROW(generic.dynObjectInitializeA(0.0, 0));
}

TEST(ComponentDescriptionTests, BusOperatingValuesAreNotCapturedAsParameters)
{
    GridBus bus;
    bus.set("angle", 90.0, units::deg);
    EXPECT_NEAR(bus.get("angle", units::deg), 90.0, 1e-5);
    EXPECT_NEAR(bus.get("ang", units::deg), 90.0, 1e-5);
    const auto& description = bus.getModelDescription();
    for (const auto* name : {"angle", "voltage", "frequency"}) {
        ASSERT_NE(description.find(name, ModelFieldKind::STATE), nullptr);
        EXPECT_EQ(description.find(name, ModelFieldKind::PARAMETER), nullptr);
    }
    const auto saved = ModelFieldSnapshot::capture(bus);
    for (const auto& field : saved.mFields) {
        EXPECT_NE(field.mName, "angle");
        EXPECT_NE(field.mName, "voltage");
        EXPECT_NE(field.mName, "frequency");
    }
}

TEST(ComponentDescriptionTests, RenewableSelectorsArraysAndDerivedHooks)
{
    REECA1 model;
    const auto inputs = model.numInputs();
    model.set("QFLAG", 1.0);
    EXPECT_DOUBLE_EQ(model.get("qflag"), 1.0);
    EXPECT_GT(model.numInputs(), inputs);
    EXPECT_THROW(model.set("qflag", 0.5), InvalidParameterValue);
    model.set("vq4", 1.2);
    EXPECT_DOUBLE_EQ(model.get("vq4"), 1.2);

    REECA1E extended;
    extended.set("pflag", 1.0);
    EXPECT_EQ(extended.numInputs(), 7);
    REECB1 reduced;
    EXPECT_THROW(reduced.set("vq1", 0.2), InvalidParameterValue);

    REGCV1 forming;
    forming.set("rf", 0.01);
    EXPECT_DOUBLE_EQ(forming.get("ra"), 0.01);
    const auto snapshot = ModelFieldSnapshot::capture(forming);
    forming.set("ra", 0.02);
    snapshot.restore(forming);
    EXPECT_DOUBLE_EQ(forming.get("rf"), 0.01);
}

TEST(ComponentDescriptionTests, IndexedOperationsAreDescribedAlongsideExactCommands)
{
    Subsystem subsystem;
    loads::AggregateLoad aggregate;
    loads::CompositeLoad composite;
    loads::FileLoad file;
    loads::GridLabDLoad gridlab;
    const auto checkIndexed = [](const CoreObject& model, std::string_view name) {
        SCOPED_TRACE(name);
        const auto* field = model.getModelDescription().findLocal(name);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->kind(), ModelFieldKind::INDEXED_COMMAND);
    };
    for (const auto* name : {"bus2", "bus_2", "connection#3"}) {
        checkIndexed(subsystem, name);
    }
    checkIndexed(aggregate, "subload_2");
    checkIndexed(aggregate, "fraction#2");
    checkIndexed(composite, "component2");
    checkIndexed(composite, "fraction2");
    checkIndexed(file, "column_2");
    checkIndexed(gridlab, "file#2");
    checkIndexed(gridlab, "workdir2");

    EXPECT_EQ(file.getModelDescription().findLocal("column")->kind(), ModelFieldKind::COMMAND);
    file.set("column_2", "p q");
    composite.set("componentfraction2", 0.6);
    EXPECT_DOUBLE_EQ(composite.get("fraction2"), 0.6);
    EXPECT_THROW(composite.set("fraction2", -0.1), InvalidParameterValue);
    EXPECT_EQ(composite.getModelDescription().findLocal("fraction2junk"), nullptr);
    EXPECT_EQ(composite.getModelDescription().findLocal("fraction-2"), nullptr);
}

TEST(ComponentDescriptionTests, CustomAliasesAndInverseFlagsRemainClassHandled)
{
    loads::MotorLoad motor;
    const auto& motorDescription = motor.getModelDescription();
    EXPECT_EQ(motorDescription.trySetLocal(motor, "beta", 0.25), SetResult::COMMAND);
    motor.set("beta", 0.25);
    EXPECT_DOUBLE_EQ(motor.get("beta"), 0.25);
    EXPECT_NE(motorDescription.findLocal("mbase"), nullptr);

    loads::FileLoad file;
    const auto& fileDescription = file.getModelDescription();
    EXPECT_EQ(fileDescription.trySetLocal(file, "relative", 1.0), SetResult::FLAG);
    EXPECT_FALSE(file.getFlag("absolute"));
    file.set("relative", 0.0);
    EXPECT_TRUE(file.getFlag("absolute"));
    file.set("interpolate", 1.0);
    EXPECT_FALSE(file.getFlag("step"));

    loads::ThreePhaseLoad phase;
    phase.set("ignore_phase", 1.0);
    EXPECT_FALSE(phase.getFlag("use_abs_angle"));
    phase.set("ignorevoltagephase", 0.0);
    EXPECT_TRUE(phase.getFlag("use_abs_angle"));
}
