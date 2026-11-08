/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "griddyn/Generator.h"
#include "optimization/gridDynOpt.h"
#include "optimization/gridOptObjects.h"
#include "optimization/models/gridAreaOpt.h"
#include "optimization/models/gridBusOpt.h"
#include "optimization/models/gridGenOpt.h"
#include "optimization/models/gridLinkOpt.h"
#include "optimization/models/gridLoadOpt.h"
#include "optimization/models/gridRelayOpt.h"
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace griddyn;

namespace {
class InspectableGenOpt: public GridGenOpt {
  public:
    using GridGenOpt::GridGenOpt;
    const std::vector<double>& coefficients(bool reactive = false) const
    {
        return reactive ? Qcoeff : Pcoeff;
    }
    double forecast() const { return m_forecast; }
    double penalty() const { return m_penaltyCost; }
};
}  // namespace

TEST(OptimizationDescriptionTests, ConcreteTypesShareInheritedInterfaceAndSnapshot)
{
    std::array<std::unique_ptr<GridOptObject>, 7> models{std::make_unique<GridOptObject>(),
                                                         std::make_unique<GridAreaOpt>(),
                                                         std::make_unique<GridBusOpt>(),
                                                         std::make_unique<GridGenOpt>(),
                                                         std::make_unique<GridLinkOpt>(),
                                                         std::make_unique<GridLoadOpt>(),
                                                         std::make_unique<GridRelayOpt>()};
    const std::array<std::string_view, 7> types{"GridOptObject",
                                                "GridAreaOpt",
                                                "GridBusOpt",
                                                "GridGenOpt",
                                                "GridLinkOpt",
                                                "GridLoadOpt",
                                                "GridRelayOpt"};
    for (std::size_t index = 0; index < models.size(); ++index) {
        auto& model = *models[index];
        const auto& description = model.getModelDescription();
        SCOPED_TRACE(types[index]);
        EXPECT_EQ(description.name(), types[index]);
        EXPECT_TRUE(description.describesExactType(model));
        EXPECT_EQ(description.parent()->name(), index == 0 ? "CoreObject" : "GridOptObject");

        model.set("name", "original");
        model.set("status", "out");
        EXPECT_FALSE(model.isEnabled());
        EXPECT_DOUBLE_EQ(model.get("enabled"), 0.0);
        model.set("status", 1.0);
        EXPECT_TRUE(model.getFlag("status"));
        model.setFlag("updates", true);
        EXPECT_DOUBLE_EQ(model.get("updates"), 1.0);
        model.set("period", 2.0, units::second);
        const auto saved = ModelFieldSnapshot::capture(model);
        model.set("period", 4000.0, units::ms);
        model.set("name", "changed");
        model.setFlag("updates", false);
        model.set("enabled", 0.0);
        saved.restore(model);
        // Renaming is a command, not captured parameter storage.
        EXPECT_EQ(model.getName(), "changed");
        EXPECT_NEAR(model.get("period", units::ms), 2000.0, 0.001);
        EXPECT_TRUE(model.isEnabled());
        EXPECT_TRUE(model.getFlag("updates"));

        stringVec names;
        model.getParameterStrings(names, ParamStringType::FLAGS);
        EXPECT_NE(std::find(names.begin(), names.end(), "enabled"), names.end());
        names.clear();
        model.getParameterStrings(names, ParamStringType::STR);
        EXPECT_NE(std::find(names.begin(), names.end(), "status"), names.end());

        const auto nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_NO_THROW(model.set("", nan));
        EXPECT_NO_THROW(model.set("# comment", nan));
        EXPECT_NO_THROW(model.set("", "ignored"));
        EXPECT_NO_THROW(model.set("# comment", "ignored"));
        EXPECT_THROW(model.set("unknown", nan), InvalidParameterValue);
        EXPECT_THROW(model.set("unknown", 1.0), UnrecognizedParameter);
    }
}

TEST(OptimizationDescriptionTests, GeneratorScalarAliasesAndSnapshotLevels)
{
    GridGenOpt model;
    model.set("heatrate", 3.0);
    model.set("fuel", 5.0);
    EXPECT_DOUBLE_EQ(model.get("fuelcost"), 5.0);
    EXPECT_DOUBLE_EQ(model.get("heatrate"), 3.0);
    const auto saved = ModelFieldSnapshot::capture(model);
    const auto stateOnly = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    model.set("heatrate", 4.0);
    model.set("fuelcost", 7.0);
    stateOnly.restore(model);
    EXPECT_DOUBLE_EQ(model.get("fuel"), 7.0);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("fuel"), 5.0);
    EXPECT_DOUBLE_EQ(model.get("heatrate"), 3.0);
    EXPECT_THROW(model.set("fuel", std::numeric_limits<double>::infinity()), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(model.get("fuel"), 5.0);

    stringVec names;
    model.getParameterStrings(names, ParamStringType::LOCAL_NUM);
    for (const auto name :
         {"heatrate", "fuel", "fuelcost", "p", "q", "forecast", "pmax", "penalty"}) {
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << name;
    }
}

TEST(OptimizationDescriptionTests, IndexedCostCoefficientsGrowAtBoundaryAndRejectBadSuffixes)
{
    InspectableGenOpt model;
    model.set("p0", 2.0);
    model.set("p1", 3.0);
    model.set("p4", 5.0);
    model.set("q0", 7.0);
    model.set("q1", 11.0);
    EXPECT_EQ(model.coefficients(), (std::vector<double>{2.0, 3.0, 0.0, 0.0, 5.0}));
    EXPECT_EQ(model.coefficients(true), (std::vector<double>{7.0, 11.0}));
    const auto& description = model.getModelDescription();
    ASSERT_NE(description.find("p1"), nullptr);
    EXPECT_EQ(description.find("p1")->kind(), ModelFieldKind::INDEXED_COMMAND);
    EXPECT_EQ(description.find("pmax")->kind(), ModelFieldKind::COMMAND);
    for (const auto name : {"p", "q", "p-1", "q1x", "p99999999999999999999999999999"}) {
        EXPECT_EQ(description.findLocal(name), nullptr) << name;
        EXPECT_THROW(model.set(name, 1.0), UnrecognizedParameter) << name;
    }
    EXPECT_THROW(model.set("p0", std::numeric_limits<double>::infinity()), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(model.coefficients().front(), 2.0);
}

TEST(OptimizationDescriptionTests, GeneratorCostConversionsAndPhysicalLimitForwarding)
{
    GridDynOptimization simulation;
    simulation.set("basepower", 100.0);
    Generator generator;
    generator.setParent(&simulation);
    generator.set("basepower", 100.0);
    InspectableGenOpt model(&generator);
    const auto linearUnits = units::currency / units::MW / units::hr;
    const auto quadraticUnits = units::currency / units::MW.pow(2) / units::hr;
    model.set("constant", 2.0);
    model.set("linear", 3.0, linearUnits);
    model.set("quad", 4.0, quadraticUnits);
    model.set("constantq", 5.0);
    model.set("linearq", 6.0, linearUnits);
    model.set("quadq", 7.0, quadraticUnits);
    EXPECT_EQ(model.coefficients(), (std::vector<double>{2.0, 300.0, 40000.0}));
    EXPECT_EQ(model.coefficients(true), (std::vector<double>{5.0, 600.0, 70000.0}));
    model.set("forecast", 50.0, units::MW);
    model.set("penalty", 8.0, linearUnits);
    EXPECT_DOUBLE_EQ(model.forecast(), 0.5);
    EXPECT_DOUBLE_EQ(model.penalty(), 800.0);
    InspectableGenOpt imported(&generator);
    imported.loadMatPowerCostCoeff({4.0, 3.0, 2.0}, 0, 2);
    EXPECT_EQ(model.coefficients(), imported.coefficients());
    model.set("linear", 9.0, units::currency / units::puMW / units::hr);
    model.set("quadq", 11.0, units::currency / units::puMW.pow(2) / units::hr);
    EXPECT_DOUBLE_EQ(model.coefficients()[1], 9.0);
    EXPECT_DOUBLE_EQ(model.coefficients(true)[2], 11.0);
    const auto previous = model.coefficients();
    EXPECT_THROW(model.set("linear", 12.0, units::second), InvalidParameterValue);
    EXPECT_EQ(model.coefficients(), previous);
    model.set("pmax", 150.0, units::MW);
    model.set("pmin", 25.0, units::MW);
    EXPECT_DOUBLE_EQ(generator.getPmax(0.0), 1.5);
    EXPECT_DOUBLE_EQ(generator.getPmin(0.0), 0.25);
    EXPECT_EQ(model.sourceObject(), &generator);

    InspectableGenOpt unattached;
    EXPECT_THROW(unattached.set("quadratic", 12.0, units::second), InvalidParameterValue);
    EXPECT_THROW(unattached.set("quadq", 12.0, units::second), InvalidParameterValue);
    EXPECT_TRUE(unattached.coefficients().empty());
    EXPECT_TRUE(unattached.coefficients(true).empty());
    EXPECT_NO_THROW(unattached.set("linear", 3.0));
    EXPECT_DOUBLE_EQ(unattached.coefficients()[1], 3.0);
}

TEST(OptimizationDescriptionTests, ReservedTolerancesAreExplicitAndUseCommonInputGate)
{
    GridAreaOpt area;
    GridBusOpt bus;
    GridLinkOpt link;
    GridRelayOpt relay;
    for (auto* model : std::array<GridOptObject*, 4>{&area, &bus, &link, &relay}) {
        for (const auto name : {"voltagetolerance", "vtol", "angletolerance", "atol"}) {
            const auto* field = model->getModelDescription().findLocal(name);
            ASSERT_NE(field, nullptr);
            EXPECT_EQ(field->kind(), ModelFieldKind::COMMAND);
            EXPECT_NE(field->description().find("ignored"), std::string_view::npos);
            EXPECT_NO_THROW(model->set(name, 1e-6));
            EXPECT_THROW(model->set(name, std::numeric_limits<double>::infinity()),
                         InvalidParameterValue);
        }
    }
    EXPECT_NO_THROW(relay.set("angleetolerance", 1e-6));
}

TEST(OptimizationDescriptionTests, StatusCommandPreservesBusDisableCascade)
{
    GridBusOpt bus;
    GridLinkOpt link;
    bus.add(&link);
    bus.set("status", "out");
    EXPECT_FALSE(bus.isEnabled());
    EXPECT_FALSE(link.isEnabled());
    bus.remove(&link);
}

TEST(OptimizationDescriptionTests, SimulationToleranceUsesSharedParameterStorage)
{
    GridDynOptimization simulation;
    simulation.set("optimtol", 1e-7);
    EXPECT_DOUBLE_EQ(simulation.get("optimtol"), 1e-7);
    EXPECT_DOUBLE_EQ(simulation.get("rtol"), 1e-7);
    simulation.set("tolerance", 2e-7);
    EXPECT_DOUBLE_EQ(simulation.get("optimtol"), 2e-7);
    const auto* field = simulation.getModelDescription().findLocal("optimtol");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->kind(), ModelFieldKind::PARAMETER);
    EXPECT_THROW(simulation.set("optimtol", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    EXPECT_DOUBLE_EQ(simulation.get("optimtol"), 2e-7);
    EXPECT_NO_THROW(simulation.set("# comment", "ignored"));
}
