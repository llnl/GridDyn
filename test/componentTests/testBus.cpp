/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/CoreOwningPtr.hpp"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/loads/RampLoad.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include "griddyn/solvers/SolverInterface.h"
#include "units/units.hpp"
#include "utilities/MatrixDataCompact.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace griddyn;

namespace {
class PowerflowInitializedBus: public GridBus {
  public:
    using GridBus::GridBus;

    void markPowerflowInitialized() { opFlags.set(POWERFLOW_INITIALIZED); }
};

class FixedCapacityLink: public Link {
  public:
    explicit FixedCapacityLink(double capacity): transferCapacity(capacity) {}

    [[nodiscard]] double getMaxTransfer() const override { return transferCapacity; }

  private:
    double transferCapacity;
};

class SampledAcBus: public AcBus {
  public:
    [[nodiscard]] double angleRate() const { return m_dstate_dt[ANGLE_IN_LOCATION]; }
    [[nodiscard]] double voltageRate() const { return m_dstate_dt[VOLTAGE_IN_LOCATION]; }
};

class KeyGeneratorProbe: public DynamicGenerator {
  public:
    KeyGeneratorProbe(): DynamicGenerator(DynModel::SIMPLE)
    {
        opFlags.set(Generator::INTERNAL_FREQUENCY_CALCULATION);
    }
};

// Use public size/offset APIs so the numerical checks exercise real solver layouts.
void expectJacobianMatchesResidual(GridBus& bus, const SolverMode& mode)
{
    const auto count = bus.stateSize(mode);
    ASSERT_GT(count, 0U);
    ASSERT_LT(count, 6U);
    bus.setOffset(0, mode);
    std::vector<double> state(count, 1.1);
    MatrixDataCompact<6, 6> jacobian;
    StateData data(0.0, state.data());
    bus.jacobianElements(noInputs, data, jacobian, {}, mode);
    constexpr double delta = 1.0e-6;
    for (index_t column = 0; column < count; ++column) {
        std::vector<double> lower(count + 1, 12345.0);
        std::vector<double> upper(count + 1, 12345.0);
        state[column] -= delta;
        bus.residual(noInputs, data, lower.data(), mode);
        state[column] += 2.0 * delta;
        bus.residual(noInputs, data, upper.data(), mode);
        state[column] -= delta;
        EXPECT_DOUBLE_EQ(lower[count], 12345.0);
        EXPECT_DOUBLE_EQ(upper[count], 12345.0);
        for (index_t row = 0; row < count; ++row) {
            EXPECT_NEAR(jacobian.at(row, column), (upper[row] - lower[row]) / (2.0 * delta), 1.0e-7)
                << "row=" << row << " column=" << column;
        }
    }
}
}  // namespace

TEST(BusTests, ConnectedFlagMatchesRequestedState)
{
    GridBus bus("bus");
    ASSERT_TRUE(bus.isConnected());

    bus.setFlag("connected", false);
    EXPECT_FALSE(bus.isConnected());
    bus.setFlag("connected", false);
    EXPECT_FALSE(bus.isConnected());

    bus.setFlag("connected", true);
    EXPECT_TRUE(bus.isConnected());
    bus.setFlag("connected", true);
    EXPECT_TRUE(bus.isConnected());
}

TEST(BusTests, AcBusConnectedFlagMatchesRequestedState)
{
    AcBus bus("ac_bus");
    ASSERT_TRUE(bus.isConnected());

    bus.setFlag("connected", false);
    EXPECT_FALSE(bus.isConnected());
    bus.setFlag("connected", true);
    EXPECT_TRUE(bus.isConnected());
}

TEST(BusTests, BaseVoltageUnitsPropagateToAttachedComponents)
{
    auto* generator = new Generator("generator");
    auto* load = new GridLoad("load");
    GridBus bus("bus");
    bus.add(generator);
    bus.add(load);

    bus.set("basevoltage", 230000.0, units::V);

    EXPECT_DOUBLE_EQ(bus.get("basevoltage", units::V), 230000.0);
    EXPECT_DOUBLE_EQ(generator->get("basevoltage", units::V), 230000.0);
    EXPECT_DOUBLE_EQ(load->get("basevoltage", units::V), 230000.0);
}

TEST(BusTests, ZipLoadBaseVoltageUsesExplicitUnitsAtApiBoundary)
{
    auto* zip = new ZipLoad("zip");
    GridBus bus("bus");
    bus.add(zip);

    EXPECT_DOUBLE_EQ(zip->get("basevoltage"), 120.0);
    EXPECT_DOUBLE_EQ(zip->get("basevoltage", units::V), 120000.0);
    zip->set("basevoltage", 200000.0, units::V);
    EXPECT_DOUBLE_EQ(zip->get("vbase"), 200.0);
    EXPECT_DOUBLE_EQ(zip->get("basevoltage", units::V), 200000.0);

    bus.set("basevoltage", 200.0, units::kV);
    auto* ramp = new loads::RampLoad("ramp");
    bus.add(ramp);
    EXPECT_DOUBLE_EQ(ramp->get("basevoltage", units::V), 200000.0);
    std::unique_ptr<ZipLoad> copy(static_cast<ZipLoad*>(zip->clone()));
    EXPECT_DOUBLE_EQ(copy->get("basevoltage", units::V), 200000.0);
}

TEST(BusTests, CheckCapableRejectsLoadAboveMaximumGeneration)
{
    auto* generator = new Generator("generator");
    auto* load = new GridLoad(2.0, 0.0, "load");
    PowerflowInitializedBus bus("bus");
    generator->set("pmin", 0.0);
    generator->set("pmax", 1.0);
    bus.add(generator);
    bus.add(load);
    bus.markPowerflowInitialized();

    EXPECT_FALSE(bus.checkCapable());
}

TEST(BusTests, CheckCapableRejectsMinimumGenerationAboveLoad)
{
    auto* generator = new Generator("generator");
    PowerflowInitializedBus bus("bus");
    generator->set("pmin", 1.0);
    generator->set("pmax", 2.0);
    bus.add(generator);
    bus.markPowerflowInitialized();

    EXPECT_FALSE(bus.checkCapable());
}

TEST(BusTests, CheckCapableAcceptsFeasibleGenerationRange)
{
    auto* generator = new Generator("generator");
    auto* load = new GridLoad(0.5, 0.0, "load");
    PowerflowInitializedBus bus("bus");
    generator->set("pmin", 0.0);
    generator->set("pmax", 1.0);
    bus.add(generator);
    bus.add(load);
    bus.markPowerflowInitialized();

    EXPECT_TRUE(bus.checkCapable());
}

TEST(BusTests, CheckCapableAllowsOnlyConfiguredSolverToleranceAtCapacityBoundaries)
{
    GridDynSimulation simulation("capacity_tolerance");
    simulation.set("tolerance", 1.0e-4);
    EXPECT_DOUBLE_EQ(simulation.get("tolerance"), 1.0e-4);
    EXPECT_DOUBLE_EQ(simulation.get("rtol"), 1.0e-4);
    auto* bus = new PowerflowInitializedBus("bus");
    auto* generator = new Generator("generator");
    auto* load = new GridLoad(0.3 + 5.0e-5, 0.0, "load");
    generator->set("pmin", 0.0);
    generator->set("pmax", 0.3);
    bus->add(generator);
    bus->add(load);
    bus->markPowerflowInitialized();
    simulation.add(bus);

    EXPECT_TRUE(bus->checkCapable());
    load->set("p", 0.3 + 2.0e-4);
    EXPECT_FALSE(bus->checkCapable());

    load->set("p", 0.2);
    generator->set("pmin", 0.2 + 5.0e-5);
    EXPECT_TRUE(bus->checkCapable());
    generator->set("pmin", 0.2 + 2.0e-4);
    EXPECT_FALSE(bus->checkCapable());

    const auto solver = simulation.getSolverInterface("powerflow");
    ASSERT_NE(solver, nullptr);
    solver->set("tolerance", 1.0e-6);
    load->set("p", 0.3 + 5.0e-7);
    generator->set("pmin", 0.0);
    EXPECT_TRUE(bus->checkCapable());
    load->set("p", 0.3 + 2.0e-6);
    EXPECT_FALSE(bus->checkCapable());
}

TEST(BusTests, CheckCapableIgnoresDisabledGeneration)
{
    auto* generator = new Generator("generator");
    auto* load = new GridLoad(1.0, 0.0, "load");
    PowerflowInitializedBus bus("bus");
    generator->set("pmin", 0.0);
    generator->set("pmax", 2.0);
    bus.add(generator);
    bus.add(load);
    generator->disable();
    bus.markPowerflowInitialized();

    EXPECT_FALSE(bus.checkCapable());
}

TEST(BusTests, CheckCapableIgnoresDisabledLoad)
{
    auto* load = new GridLoad(1.0, 0.0, "load");
    PowerflowInitializedBus bus("bus");
    bus.add(load);
    load->disable();
    bus.markPowerflowInitialized();

    EXPECT_TRUE(bus.checkCapable());
}

TEST(BusTests, CheckCapableIgnoresDisabledLinkTransfer)
{
    auto* load = new GridLoad(1.0, 0.0, "load");
    FixedCapacityLink link(2.0);
    PowerflowInitializedBus bus("bus");
    bus.add(&link);
    bus.add(load);
    link.disable();
    bus.markPowerflowInitialized();

    EXPECT_FALSE(bus.checkCapable());
}

TEST(BusTests, AlternateConstructorsSupportCachingAndDisconnect)
{
    GridBus fixed(1.04, 0.2);
    AcBus acBus(1.04, 0.2);
    for (GridBus* bus : std::array<GridBus*, 2>{&fixed, &acBus}) {
        bus->updateLocalCache(noInputs, emptyStateData, cLocalSolverMode);
        ASSERT_EQ(bus->getOutputsRef().size(), 3U);
        EXPECT_DOUBLE_EQ(bus->getOutputsRef()[VOLTAGE_IN_LOCATION], 1.04);
        EXPECT_DOUBLE_EQ(bus->getOutputsRef()[ANGLE_IN_LOCATION], 0.2);
        bus->disconnect();
        EXPECT_FALSE(bus->isConnected());
        EXPECT_DOUBLE_EQ(bus->getVoltage(), 0.0);
    }
}

TEST(BusTests, LoadRemovalPreservesSubsequentRemovalAndReplacement)
{
    auto first = makeOwningPtr<GridLoad>("first");
    auto second = makeOwningPtr<GridLoad>("second");
    auto third = makeOwningPtr<GridLoad>("third");
    auto replacement = makeOwningPtr<GridLoad>("replacement");
    GridBus bus;
    bus.add(first.get());
    bus.add(second.get());
    bus.add(third.get());
    bus.remove(first.get());
    EXPECT_EQ(bus.getLoad(0), second.get());
    bus.replaceLoad(third.get(), replacement.get());
    EXPECT_EQ(bus.getLoad(1), replacement.get());
    bus.remove(second.get());
    bus.remove(replacement.get());
    EXPECT_EQ(bus.getLoad(), nullptr);
    EXPECT_DOUBLE_EQ(bus.get("subobjectcount"), 0.0);
}

TEST(BusTests, GeneratorRemovalPreservesSubsequentRemovalAndReplacement)
{
    auto first = makeOwningPtr<Generator>("first");
    auto second = makeOwningPtr<Generator>("second");
    auto third = makeOwningPtr<Generator>("third");
    auto replacement = makeOwningPtr<Generator>("replacement");
    GridBus bus;
    bus.add(first.get());
    bus.add(second.get());
    bus.add(third.get());
    bus.remove(first.get());
    EXPECT_EQ(bus.getGen(0), second.get());
    bus.replaceGenerator(third.get(), replacement.get());
    EXPECT_EQ(bus.getGen(1), replacement.get());
    bus.remove(second.get());
    bus.remove(replacement.get());
    EXPECT_EQ(bus.getGen(), nullptr);
    EXPECT_DOUBLE_EQ(bus.get("subobjectcount"), 0.0);
}

TEST(BusTests, ReplacementHandlesAliasedComponentsWithoutCorruptingOwnership)
{
    GridBus bus;
    auto* first = new Generator("first");
    auto* second = new Generator("second");
    auto* load = new GridLoad("load");
    auto* otherLoad = new GridLoad("otherLoad");
    bus.add(first);
    bus.add(second);
    bus.add(load);
    bus.add(otherLoad);
    bus.replaceGenerator(first, first);
    bus.replaceLoad(load, load);
    EXPECT_THROW(bus.replaceGenerator(first, second), ObjectAddFailure);
    EXPECT_THROW(bus.replaceLoad(load, otherLoad), ObjectAddFailure);
    EXPECT_EQ(bus.getGen(0), first);
    EXPECT_EQ(bus.getGen(1), second);
    EXPECT_EQ(bus.getLoad(0), load);
    EXPECT_EQ(bus.getLoad(1), otherLoad);
}

TEST(BusTests, BulkUpdatesReachOwnedComponents)
{
    GridBus bus;
    auto* generator = new Generator;
    auto* load = new GridLoad;
    bus.add(generator);
    bus.add(load);
    bus.set("basepower", 50.0);
    EXPECT_DOUBLE_EQ(generator->basePower(), 50.0);
    EXPECT_DOUBLE_EQ(load->basePower(), 50.0);
    bus.setAll("all", "basefrequency", 50.0, units::Hz);
    EXPECT_NEAR(generator->get("basefrequency", units::Hz), 50.0, 1.0e-10);
    EXPECT_NEAR(load->get("basefrequency", units::Hz), 50.0, 1.0e-10);
}

TEST(BusTests, ReactiveGenerationUsesSystemPowerBase)
{
    GridBus bus;
    bus.set("basepower", 50.0);
    bus.set("q", 25.0, units::MVAR);
    ASSERT_NE(bus.getGen(), nullptr);
    EXPECT_DOUBLE_EQ(bus.getGen()->getReactivePower(), -0.5);
}

TEST(BusTests, DefaultBaseVoltageReachesNewAndExistingComponentsInVolts)
{
    GridBus bus;
    auto* generator = new Generator;
    bus.add(generator);
    EXPECT_DOUBLE_EQ(generator->get("basevoltage", units::V), 120000.0);
    bus.set("basevoltage", 200.0);
    auto* load = new GridLoad;
    bus.add(load);
    EXPECT_DOUBLE_EQ(bus.baseVoltage(), 200000.0);
    EXPECT_DOUBLE_EQ(generator->get("basevoltage", units::V), 200000.0);
    EXPECT_DOUBLE_EQ(load->get("basevoltage", units::V), 200000.0);
    auto* replacement = new GridLoad;
    bus.replaceLoad(load, replacement);
    EXPECT_DOUBLE_EQ(replacement->get("basevoltage", units::V), 200000.0);
    bus.set("voltage", 190.0, units::kV);
    EXPECT_DOUBLE_EQ(bus.getVoltage(), 0.95);
    EXPECT_DOUBLE_EQ(bus.get("voltage", units::V), 190000.0);
}

TEST(BusTests, DirectPathsFollowUnbranchedActiveLinks)
{
    GridBus start("start");
    GridBus middle("middle");
    GridBus target("target");
    GridBus branch("branch");
    Link first("first");
    Link second("second");
    Link extra("extra");
    first.updateBus(&start, 1);
    first.updateBus(&middle, 2);
    second.updateBus(&middle, 1);
    second.updateBus(&target, 2);
    EXPECT_TRUE(start.directPath(&target));
    EXPECT_EQ(start.getDirectPath(&target),
              (std::vector<GridComponent*>{&start, &first, &second, &target}));
    EXPECT_TRUE(middle.directPath(&target, &first));
    extra.updateBus(&middle, 1);
    extra.updateBus(&branch, 2);
    EXPECT_FALSE(start.directPath(&target));
    extra.disable();
    EXPECT_TRUE(start.directPath(&target));
    second.disconnect();
    EXPECT_FALSE(start.directPath(&target));
    EXPECT_FALSE(start.directPath(nullptr));
}

TEST(BusTests, DirectPathTerminatesOnCycles)
{
    GridBus first("first");
    GridBus second("second");
    GridBus third("third");
    GridBus absent("absent");
    Link one("one");
    Link two("two");
    Link three("three");
    one.updateBus(&first, 1);
    one.updateBus(&second, 2);
    two.updateBus(&second, 1);
    two.updateBus(&third, 2);
    three.updateBus(&third, 1);
    three.updateBus(&first, 2);
    EXPECT_TRUE(first.getDirectPath(&absent, &three).empty());
}

TEST(AcBusTests, VoltageAndPowerLimitsRespectExplicitUnits)
{
    AcBus bus;
    bus.set("basevoltage", 200.0);
    bus.set("basepower", 50.0);
    bus.set("voltage", 190.0, units::kV);
    bus.set("vtarget", 210.0, units::kV);
    bus.set("vmin", 180.0, units::kV);
    bus.set("vmax", 220.0, units::kV);
    bus.set("pmin", 10.0, units::MW);
    bus.set("pmax", 40.0, units::MW);
    bus.set("qmin", -15.0, units::MVAR);
    bus.set("qmax", 20.0, units::MVAR);
    EXPECT_DOUBLE_EQ(bus.getVoltage(), 0.95);
    EXPECT_DOUBLE_EQ(bus.get("vtarget"), 1.05);
    EXPECT_DOUBLE_EQ(bus.get("vmin"), 0.9);
    EXPECT_DOUBLE_EQ(bus.get("vmax"), 1.1);
    EXPECT_DOUBLE_EQ(bus.get("pmin", units::MW), 10.0);
    EXPECT_DOUBLE_EQ(bus.get("pmax", units::MW), 40.0);
    EXPECT_DOUBLE_EQ(bus.get("qmin", units::MVAR), -15.0);
    EXPECT_DOUBLE_EQ(bus.get("qmax", units::MVAR), 20.0);
}

TEST(AcBusTests, PowerMinimumInitializationDoesNotOverwriteReactiveMinimum)
{
    AcBus bus;
    bus.set("vtol", 1.0e-6);
    bus.set("atol", 1.0e-6);
    bus.set("type", "slk");
    bus.set("pmin", 0.2);
    auto* generator = new Generator;
    generator->set("qmin", -0.4);
    bus.add(generator);
    bus.pFlowInitializeA(0.0, 0);
    EXPECT_DOUBLE_EQ(generator->getPmin(), 0.2);
    EXPECT_DOUBLE_EQ(generator->getQmin(), -0.4);
}

TEST(AcBusTests, ReactiveLimitBypassPreservesBoundsAndDispatchesBeyondThem)
{
    for (const auto busType : {"pv", "slk"}) {
        for (const double requiredQ : {-0.3, 0.3}) {
            for (const bool ignoreLimits : {false, true}) {
                SCOPED_TRACE(busType);
                SCOPED_TRACE(requiredQ);
                SCOPED_TRACE(ignoreLimits);
                AcBus bus;
                bus.set("type", busType);
                bus.set("vtol", 1e-6);
                bus.set("atol", 1e-6);
                auto* generator = new Generator;
                generator->set("qmin", -0.1);
                generator->set("qmax", 0.1);
                bus.add(generator);
                bus.add(new ZipLoad(0.0, requiredQ));
                const auto flags = ignoreLimits ? (1U << IGNORE_REACTIVE_POWER_LIMITS) : 0U;
                bus.pFlowInitializeA(0.0, flags);
                bus.pFlowInitializeB();
                bus.powerFlowAdjust(noInputs, flags, CheckLevel::FULL_CHECK);
                if (ignoreLimits) {
                    EXPECT_EQ(bus.getType(),
                              std::string_view(busType) == "pv" ? GridBus::BusType::PV :
                                                                  GridBus::BusType::SLK);
                    EXPECT_NEAR(-generator->getReactivePower(), requiredQ, 1e-12);
                } else {
                    EXPECT_EQ(bus.getType(),
                              std::string_view(busType) == "pv" ? GridBus::BusType::PQ :
                                                                  GridBus::BusType::AFIX);
                    EXPECT_NEAR(-generator->getReactivePower(),
                                std::copysign(0.1, requiredQ),
                                1e-12);
                }
                EXPECT_DOUBLE_EQ(generator->getQmin(), -0.1);
                EXPECT_DOUBLE_EQ(generator->getQmax(), 0.1);
            }
        }
    }
}

TEST(AcBusTests, ReactiveLimitBypassStillEnforcesRealPowerLimits)
{
    AcBus bus;
    bus.set("type", "afix");
    bus.set("vtol", 1e-6);
    bus.set("atol", 1e-6);
    auto* generator = new Generator;
    generator->set("pmin", -0.1);
    generator->set("pmax", 0.1);
    bus.add(generator);
    bus.add(new ZipLoad(0.3, 0.0));
    const auto flags = 1U << IGNORE_REACTIVE_POWER_LIMITS;
    bus.pFlowInitializeA(0.0, flags);
    bus.pFlowInitializeB();
    bus.powerFlowAdjust(noInputs, flags, CheckLevel::FULL_CHECK);
    EXPECT_EQ(bus.getType(), GridBus::BusType::PQ);
    EXPECT_DOUBLE_EQ(-generator->getRealPower(), 0.1);
}

TEST(AcBusTests, ClonePreservesTargetsAndBusTypes)
{
    AcBus original;
    original.set("type", "slk");
    original.set("dyntype", "fixedvoltage");
    original.set("vtarget", 1.04);
    original.set("atarget", 0.3);
    std::unique_ptr<AcBus> copy(static_cast<AcBus*>(original.clone()));
    EXPECT_DOUBLE_EQ(copy->get("atarget"), 0.3);
    EXPECT_DOUBLE_EQ(copy->get("vtarget"), 1.04);
    EXPECT_EQ(copy->getType(), original.getType());
    EXPECT_EQ(copy->getDynType(), original.getDynType());
}

TEST(AcBusTests, FrequencyEstimatorCanBeEnabledAndDisabledBeforeInitialization)
{
    AcBus bus;
    bus.setFlag("compute_frequency", true);
    EXPECT_TRUE(bus.getFlag("compute_frequency"));
    EXPECT_DOUBLE_EQ(bus.get("subobjectcount"), 1.0);
    bus.setFlag("compute_frequency", false);
    EXPECT_FALSE(bus.getFlag("compute_frequency"));
    EXPECT_DOUBLE_EQ(bus.get("subobjectcount"), 0.0);
    bus.setFlag("use_frequency", true);
    EXPECT_TRUE(bus.getFlag("compute_frequency"));
    bus.configureFrequencyFilter(0.02, 0.1, 60.0);
    EXPECT_DOUBLE_EQ(bus.get("subobjectcount"), 1.0);
    bus.setFlag("use_frequency", false);
    EXPECT_FALSE(bus.getFlag("use_frequency"));
    EXPECT_DOUBLE_EQ(bus.get("subobjectcount"), 0.0);
    stringVec parameters;
    bus.getParameterStrings(parameters, ParamStringType::flags);
    EXPECT_NE(std::find(parameters.begin(), parameters.end(), "compute_frequency"),
              parameters.end());
}

TEST(AcBusTests, StoredFrequencyRemainsAvailableBeforeEstimatorInitialization)
{
    AcBus bus;
    bus.set("frequency", 1.02);
    bus.setFlag("compute_frequency", true);

    EXPECT_DOUBLE_EQ(bus.getFreq(emptyStateData, cDynAlgSolverMode), 1.02);
    EXPECT_DOUBLE_EQ(bus.getFreq(emptyStateData, cLocalSolverMode), 1.02);
    EXPECT_EQ(bus.getOutputLoc(cPflowSolverMode, FREQUENCY_IN_LOCATION), kNullLocation);
    EXPECT_EQ(bus.getOutputLoc(cDynAlgSolverMode, FREQUENCY_IN_LOCATION), kNullLocation);
}

TEST(AcBusTests, InternalGeneratorFrequencyHasNoLocationInAlgebraicOnlyMode)
{
    AcBus bus;
    auto* generator = new KeyGeneratorProbe();
    bus.add(generator);
    bus.dynInitializeA(0.0, 0);
    bus.stateSize(cDynAlgSolverMode);
    bus.setOffset(0, cDynAlgSolverMode);

    EXPECT_EQ(bus.getOutputLoc(cDynAlgSolverMode, FREQUENCY_IN_LOCATION), kNullLocation);

    const auto differentialSize = bus.stateSize(cDaeSolverMode);
    bus.setOffset(0, cDaeSolverMode);
    const auto frequencyLocation = bus.getOutputLoc(cDaeSolverMode, FREQUENCY_IN_LOCATION);
    EXPECT_NE(frequencyLocation, kNullLocation);
    EXPECT_LT(frequencyLocation, differentialSize);
}

TEST(AcBusTests, SampledAngleRateUsesConsecutiveSamples)
{
    SampledAcBus bus;
    bus.set("vtol", 1.0e-6);
    bus.set("atol", 1.0e-6);
    bus.set("angle", 0.2);
    bus.pFlowInitializeA(0.0, 0);
    bus.pFlowInitializeB();
    bus.stateSize(cDynAlgSolverMode);
    bus.setOffset(0, cDynAlgSolverMode);
    std::array<double, 2> state{};
    const auto& offsets = bus.getOffsets(cDynAlgSolverMode);
    state[offsets.vOffset] = 1.0;
    state[offsets.aOffset] = 0.3;
    bus.setState(1.0, state.data(), nullptr, cDynAlgSolverMode);
    EXPECT_NEAR(bus.angleRate(), 0.1, 1.0e-12);
    state[offsets.aOffset] = 0.4;
    bus.setState(2.0, state.data(), nullptr, cDynAlgSolverMode);
    EXPECT_NEAR(bus.angleRate(), 0.1, 1.0e-12);
    bus.setState(3.0, state.data(), nullptr, cDynAlgSolverMode);
    EXPECT_DOUBLE_EQ(bus.angleRate(), 0.0);
}

TEST(AcBusTests, SampledRatesUseAcceptedStatesAcrossPredictorSteps)
{
    SampledAcBus bus;
    bus.set("vtol", 1.0e-6);
    bus.set("atol", 1.0e-6);
    bus.pFlowInitializeA(0.0, 0);
    bus.pFlowInitializeB();
    bus.stateSize(cDynAlgSolverMode);
    bus.setOffset(0, cDynAlgSolverMode);
    std::array<double, 2> state{};
    const auto& offsets = bus.getOffsets(cDynAlgSolverMode);
    state[offsets.vOffset] = 1.1;
    state[offsets.aOffset] = 0.3;
    bus.setState(1.0, state.data(), nullptr, cDynAlgSolverMode);
    bus.timestep(1.5, noInputs, cDynAlgSolverMode);

    state[offsets.vOffset] = 1.2;
    state[offsets.aOffset] = 0.4;
    bus.setState(2.0, state.data(), nullptr, cDynAlgSolverMode);
    EXPECT_NEAR(bus.voltageRate(), 0.1, 1.0e-12);
    EXPECT_NEAR(bus.angleRate(), 0.1, 1.0e-12);
}

TEST(BusTests, StateAccessFallsBackWithoutAnExternalState)
{
    AcBus acBus;
    DcBus dcBus;
    for (GridBus* bus : std::array<GridBus*, 2>{&acBus, &dcBus}) {
        bus->setVoltageAngle(1.03, 0.25);
        bus->stateSize(cPflowSolverMode);
        bus->setOffset(0, cPflowSolverMode);
        EXPECT_DOUBLE_EQ(bus->getVoltage(emptyStateData, cPflowSolverMode), 1.03);
        EXPECT_DOUBLE_EQ(bus->getVoltage(nullptr, cPflowSolverMode), 1.03);
        EXPECT_DOUBLE_EQ(bus->getAngle(emptyStateData, cPflowSolverMode), 0.25);
    }
}

TEST(BusTests, PartitionedVoltageAccessUsesPairedAlgebraicState)
{
    AcBus acBus;
    DcBus dcBus;
    SolverMode differential = cDynDiffSolverMode;
    differential.pairedOffsetIndex = DYNAMIC_ALGEBRAIC;
    for (GridBus* bus : std::array<GridBus*, 2>{&acBus, &dcBus}) {
        const auto count = bus->stateSize(cDynAlgSolverMode);
        bus->setOffset(0, cDynAlgSolverMode);
        std::vector<double> algebraic(count, 1.07);
        std::array<double, 1> differentialValues{0.0};
        StateData data(0.0, differentialValues.data());
        data.algState = algebraic.data();
        EXPECT_DOUBLE_EQ(bus->getVoltage(data, differential), 1.07);
        data.fullState = data.algState;
        data.algState = nullptr;
        EXPECT_DOUBLE_EQ(bus->getVoltage(data, differential), 1.07);
    }
}

TEST(DcBusTests, AcceptsOnlyDcCapableLinksThroughEitherAddOverload)
{
    DcBus bus;
    Link acLink;
    links::DcLink dcLink;
    EXPECT_THROW(bus.add(&acLink), UnrecognizedObjectException);
    EXPECT_THROW(bus.add(static_cast<CoreObject*>(&acLink)), UnrecognizedObjectException);
    bus.add(&dcLink);
    bus.add(&dcLink);
    EXPECT_EQ(bus.getLink(0), &dcLink);
    EXPECT_EQ(bus.getLink(1), nullptr);
}

TEST(DcBusTests, VoltageUsageAndLocationsRespectModeAndConnectivity)
{
    DcBus bus;
    EXPECT_TRUE(bus.useVoltage(cPflowSolverMode));
    EXPECT_FALSE(bus.useVoltage(cDynDiffSolverMode));
    EXPECT_FALSE(bus.useVoltage(cEmptySolverMode));
    bus.stateSize(cPflowSolverMode);
    bus.setOffset(0, cPflowSolverMode);
    EXPECT_NE(bus.getOutputLoc(cPflowSolverMode), kNullLocation);
    bus.set("type", "pv");
    EXPECT_FALSE(bus.useVoltage(cPflowSolverMode));
    bus.set("type", "pq");
    bus.disconnect();
    EXPECT_FALSE(bus.useVoltage(cPflowSolverMode));
    EXPECT_EQ(bus.getOutputLoc(cPflowSolverMode), kNullLocation);
}

TEST(DcBusTests, StateNamesRetainCallerPrefix)
{
    DcBus bus("dc");
    bus.stateSize(cPflowSolverMode);
    bus.setOffset(0, cPflowSolverMode);
    stringVec names;
    bus.getStateName(names, cPflowSolverMode, "area:");
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names[0], "area:dc:voltage");
}

TEST(BusTests, AcAndDcJacobiansMatchPowerBalanceResiduals)
{
    AcBus acBus;
    DcBus dcBus;
    for (GridBus* bus : std::array<GridBus*, 2>{&acBus, &dcBus}) {
        auto* load = new ZipLoad;
        load->set("yp", 2.0);
        load->set("yq", 1.0);
        bus->add(load);
        expectJacobianMatchesResidual(*bus, cPflowSolverMode);
        load->disable();
        expectJacobianMatchesResidual(*bus, cPflowSolverMode);
        load->enable();
        bus->set("type", "slk");
        expectJacobianMatchesResidual(*bus, cPflowSolverMode);
    }
}

TEST(BusTests, ExtendedDcStatesDoNotReadOrWriteReactivePowerSlots)
{
    AcBus acBus;
    DcBus dcBus;
    SolverMode mode = cPflowSolverMode;
    mode.extended_state = true;
    setDC(mode);
    for (GridBus* bus : std::array<GridBus*, 2>{&acBus, &dcBus}) {
        expectJacobianMatchesResidual(*bus, mode);
        EXPECT_DOUBLE_EQ(bus->getLoadReactive(), 0.0);
    }
}

TEST(BusTests, CachedOutputLocationsMatchModeSpecificQueries)
{
    GridBus fixed;
    AcBus acBus;
    DcBus dcBus;
    for (GridBus* bus : std::array<GridBus*, 3>{&fixed, &acBus, &dcBus}) {
        bus->stateSize(cPflowSolverMode);
        bus->setOffset(2, cPflowSolverMode);
        std::array<double, 6> state{1.0, 1.0, 1.0, 0.2, 0.0, 0.0};
        StateData data(0.0, state.data());
        bus->updateLocalCache(noInputs, data, cPflowSolverMode);
        EXPECT_EQ(bus->getOutputLocsRef(), bus->getOutputLocs(cPflowSolverMode));
        for (index_t output = 0; output < 3; ++output) {
            EXPECT_EQ(bus->getOutputLoc(cPflowSolverMode, output),
                      bus->getOutputLocs(cPflowSolverMode)[output]);
        }
        bus->disconnect();
        EXPECT_EQ(bus->getOutputLocsRef(), (IOlocs{kNullLocation, kNullLocation, kNullLocation}));
    }
}

TEST(AcBusTests, BusTypesSelectExpectedPowerBalanceEquations)
{
    AcBus bus;
    struct Case {
        const char* name;
        bool angle;
        bool voltage;
    };
    const std::array<Case, 4> powerflow{{
        {.name = "pq", .angle = true, .voltage = true},
        {.name = "afix", .angle = false, .voltage = true},
        {.name = "pv", .angle = true, .voltage = false},
        {.name = "slk", .angle = false, .voltage = false},
    }};
    for (const auto& entry : powerflow) {
        bus.set("pflowtype", entry.name);
        EXPECT_EQ(bus.useAngle(cPflowSolverMode), entry.angle) << entry.name;
        EXPECT_EQ(bus.useVoltage(cPflowSolverMode), entry.voltage) << entry.name;
    }
    const std::array<Case, 4> dynamics{{
        {.name = "normal", .angle = true, .voltage = true},
        {.name = "fixedangle", .angle = false, .voltage = true},
        {.name = "fixedvoltage", .angle = true, .voltage = false},
        {.name = "dynslk", .angle = false, .voltage = false},
    }};
    for (const auto& entry : dynamics) {
        bus.set("dyntype", entry.name);
        EXPECT_EQ(bus.useAngle(cDaeSolverMode), entry.angle) << entry.name;
        EXPECT_EQ(bus.useVoltage(cDaeSolverMode), entry.voltage) << entry.name;
    }
    EXPECT_THROW(bus.set("pflowtype", "invalid"), InvalidParameterValue);
    EXPECT_THROW(bus.set("dyntype", "invalid"), InvalidParameterValue);
}

TEST(DcBusTests, BusTypesSelectExpectedVoltageEquation)
{
    DcBus bus;
    for (const auto* type : {"pq", "afix"}) {
        bus.set("pflowtype", type);
        EXPECT_TRUE(bus.useVoltage(cPflowSolverMode));
    }
    for (const auto* type : {"pv", "slk"}) {
        bus.set("pflowtype", type);
        EXPECT_FALSE(bus.useVoltage(cPflowSolverMode));
    }
    for (const auto* type : {"normal", "fixedangle"}) {
        bus.set("dyntype", type);
        EXPECT_TRUE(bus.useVoltage(cDaeSolverMode));
    }
    for (const auto* type : {"fixedvoltage", "dynslk"}) {
        bus.set("dyntype", type);
        EXPECT_FALSE(bus.useVoltage(cDaeSolverMode));
    }
    EXPECT_THROW(bus.set("pflowtype", "invalid"), InvalidParameterValue);
    stringVec parameters;
    bus.getParameterStrings(parameters, ParamStringType::str);
    EXPECT_NE(std::find(parameters.begin(), parameters.end(), "pflowtype"), parameters.end());
}

TEST(DcBusTests, TwoBusResistiveNetworkSolvesToAnalyticalVoltage)
{
    GridDynSimulation simulation("dc_network");
    auto* source = new DcBus("source");
    auto* demand = new DcBus("demand");
    auto* line = new links::DcLink(0.1, 0.0, "line");
    source->set("type", "slk");
    auto* generator = new Generator("supply");
    generator->set("pmin", 0.0);
    generator->set("pmax", 2.0);
    source->add(generator);
    demand->add(new GridLoad(0.5, 0.0, "load"));
    simulation.add(source);
    simulation.add(demand);
    line->updateBus(source, 1);
    line->updateBus(demand, 2);
    simulation.add(line);

    simulation.powerflow();

    ASSERT_EQ(simulation.currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
    EXPECT_EQ(demand->getType(), GridBus::BusType::PQ);
    // V * (1 - V) / R = P for a constant-power load and a 1 pu source.
    const double expectedVoltage = (1.0 + std::sqrt(1.0 - (4.0 * 0.1 * 0.5))) / 2.0;
    EXPECT_NEAR(demand->getVoltage(), expectedVoltage, 1.0e-7);
    EXPECT_DOUBLE_EQ(source->getVoltage(), 1.0);
    demand->updateLocalCache();
    EXPECT_NEAR(demand->lastError(), 0.0, 1.0e-7);
}

TEST(DcBusTests, CoupledLinkJacobianMatchesBothBusPowerBalances)
{
    DcBus source("source");
    DcBus demand("demand");
    links::DcLink line(0.1, 0.0);
    line.updateBus(&source, 1);
    line.updateBus(&demand, 2);
    source.stateSize(cPflowSolverMode);
    demand.stateSize(cPflowSolverMode);
    source.setOffset(0, cPflowSolverMode);
    demand.setOffset(1, cPflowSolverMode);
    line.stateSize(cPflowSolverMode);
    std::array<double, 2> state{1.05, 0.95};
    StateData data(0.0, state.data());
    MatrixDataCompact<2, 2> jacobian;
    source.jacobianElements(noInputs, data, jacobian, {}, cPflowSolverMode);
    demand.jacobianElements(noInputs, data, jacobian, {}, cPflowSolverMode);
    constexpr double delta = 1.0e-6;
    for (index_t column = 0; column < 2; ++column) {
        std::array<double, 2> lower{};
        std::array<double, 2> upper{};
        state[column] -= delta;
        source.residual(noInputs, data, lower.data(), cPflowSolverMode);
        demand.residual(noInputs, data, lower.data(), cPflowSolverMode);
        state[column] += 2.0 * delta;
        source.residual(noInputs, data, upper.data(), cPflowSolverMode);
        demand.residual(noInputs, data, upper.data(), cPflowSolverMode);
        state[column] -= delta;
        for (index_t row = 0; row < 2; ++row) {
            EXPECT_NEAR(jacobian.at(row, column),
                        (upper[row] - lower[row]) / (2.0 * delta),
                        1.0e-7);
        }
    }
}

TEST(BusTests, DisconnectionClearsCachedPowerAndVoltage)
{
    GridBus fixed;
    AcBus acBus;
    DcBus dcBus;
    for (GridBus* bus : std::array<GridBus*, 3>{&fixed, &acBus, &dcBus}) {
        bus->add(new GridLoad(0.25, 0.0));
        bus->updateLocalCache();
        ASSERT_DOUBLE_EQ(bus->getLoadReal(), 0.25);
        bus->disconnect();
        bus->updateLocalCache();
        EXPECT_DOUBLE_EQ(bus->getLoadReal(), 0.0);
        EXPECT_DOUBLE_EQ(bus->lastError(), 0.0);
        EXPECT_DOUBLE_EQ(bus->getOutputsRef()[VOLTAGE_IN_LOCATION], 0.0);
    }
}

TEST(AcBusTests, PartialDesiredOutputsAndEstimatorLifecycleAreSafe)
{
    AcBus bus;
    bus.set("vtol", 1.0e-6);
    bus.set("atol", 1.0e-6);
    bus.set("angle", 0.3);
    bus.configureFrequencyFilter(0.02, 0.1);
    bus.setFlag("compute_frequency", false);
    bus.pFlowInitializeA(0.0, 0);
    bus.pFlowInitializeB();
    bus.dynInitializeA(0.0, 0);
    IOdata fieldSet;
    bus.dynInitializeB(noInputs, {1.02}, fieldSet);
    EXPECT_DOUBLE_EQ(bus.getVoltage(), 1.02);
    EXPECT_DOUBLE_EQ(bus.getAngle(), 0.3);
    EXPECT_FALSE(bus.getFlag("compute_frequency"));
    EXPECT_THROW(bus.setFlag("compute_frequency", true), InvalidParameterValue);
    EXPECT_THROW(bus.configureFrequencyFilter(0.02, 0.1), InvalidParameterValue);
}
