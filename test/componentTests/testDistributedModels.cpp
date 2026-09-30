/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "fileInput/fileInput.h"
#include "fileInput/gridReadAndes.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/events/Player.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/relays/DGProtectionRelay.h"
#include "griddyn/renewables/DistributedConverter.h"
#include "utilities/MatrixDataSparse.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace griddyn;

TEST(DistributedModels, FactoryAndSignedHostPower)
{
    for (const auto* type : {"pvd1", "esd1", "ev1", "ev2"}) {
        auto* object = CoreObjectFactory::instance()->createObject("renewable_model", type);
        ASSERT_NE(object, nullptr);
        EXPECT_NE(dynamic_cast<DistributedConverter*>(object), nullptr);
        delete object;
    }
    for (const auto* type : {"dgprct1", "dgprctext"}) {
        auto* object = CoreObjectFactory::instance()->createObject("relay", type);
        ASSERT_NE(object, nullptr);
        EXPECT_NE(dynamic_cast<DGProtectionRelay*>(object), nullptr);
        delete object;
    }
    RenewableGenerator host;
    auto* storage = new ESD1;
    storage->set("socinit", 0.5);
    host.add(storage);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0, 1.0}, {-0.3, 0.1}, fields);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0, 1.0}, emptyStateData, cLocalSolverMode)[0], 0.3, 1e-12);
    EXPECT_NEAR(storage->get("soc"), 0.5, 1e-12);
}

TEST(DistributedModels, CurrentLagsAndPlayerSetpoints)
{
    PVD1 model;
    model.set("pqflag", 1.0);
    model.set("ialim", 0.7);
    model.set("tip", 0.1);
    model.set("tiq", 0.1);
    model.set("vrflag", 1.0);
    model.set("frflag", 1.0);
    model.dynInitializeA(0.0, 0);
    IOdata fields;
    model.dynInitializeB({1.0, 0.0, 1.0, 1.0}, {0.3, 0.0}, fields);
    model.set("pref", 0.6);  // Event/Player targets the ordinary numeric property.
    std::array<double, 4> rates{};
    model.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], 3.0, 1e-12);
    model.set("paux", 0.5);
    model.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], 4.0, 1e-12);  // 0.7 current limit.
    model.set("blocked", 1.0);
    model.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_LT(rates[2], 0.0);
}

TEST(DistributedModels, VoltageAndFrequencyLatches)
{
    PVD1 model;
    model.set("pref", 0.5);
    model.set("vrflag", 0.0);
    model.set("frflag", 1.0);
    model.dynInitializeA(0.0, 0);
    IOdata fields;
    model.dynInitializeB({1.0, 0.0, 1.0, 1.0}, {0.5, 0.0}, fields);
    model.rootCheck({0.8, 0.0, 1.0, 0.8}, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    EXPECT_EQ(model.get("voltage_latched"), 1.0);
    model.rootCheck({1.0, 0.0, 1.0, 1.0}, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    EXPECT_EQ(model.get("voltage_latched"), 1.0);
    model.set("reset", 1.0);
    EXPECT_EQ(model.get("voltage_latched"), 0.0);
    model.rootCheck({1.0, 0.0, 0.98, 1.0},
                    emptyStateData,
                    cLocalSolverMode,
                    CheckLevel::FULL_CHECK);
    EXPECT_EQ(model.get("frequency_latched"), 1.0);
    model.rootCheck({1.0, 0.0, 1.0, 1.0}, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    EXPECT_EQ(model.get("frequency_latched"), 0.0);
}

TEST(DistributedModels, StorageAndVehicleChargeLimits)
{
    ESD1 storage;
    storage.set("socinit", 0.5);
    storage.set("en", 1.0);
    storage.dynInitializeA(0.0, 0);
    IOdata fields;
    storage.dynInitializeB({1.0, 0.0, 1.0, 1.0}, {-0.4, 0.0}, fields);
    std::array<double, 5> rates{};
    storage.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_GT(rates[4], 0.0);
    storage.set("socinit", 1.0);
    storage.dynInitializeB({1.0, 0.0, 1.0, 1.0}, {-0.4, 0.0}, fields);
    storage.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_EQ(rates[4], 0.0);

    EV1 ev1;
    ev1.set("pmn", -0.2);
    ev1.dynInitializeA(0.0, 0);
    ev1.dynInitializeB({1.0, 0.0, 1.0, 1.0}, {-0.1, 0.0}, fields);
    ev1.set("pref", -0.5);
    ev1.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], (-0.2 + 0.1) / 0.02, 1e-12);

    EV2 ev2;
    ev2.set("pmx", 1.0);
    ev2.set("pmn", -1.0);
    ev2.set("pcap", 0.3);
    ev2.dynInitializeA(0.0, 0);
    ev2.dynInitializeB({1.0, 0.0, 1.0, 1.0}, {0.1, 0.0}, fields);
    ev2.set("pref", 0.8);
    ev2.derivative({1.0, 0.0, 1.0, 1.0}, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], (0.3 - 0.1) / 0.02, 1e-12);
}

TEST(DistributedModels, ConverterDaeJacobians)
{
    for (bool storageVariant : {false, true}) {
        std::unique_ptr<DistributedConverter> model = storageVariant ?
            std::unique_ptr<DistributedConverter>(std::make_unique<ESD1>()) :
            std::unique_ptr<DistributedConverter>(std::make_unique<PVD1>());
        model->set("vrflag", 1.0);
        model->set("frflag", 1.0);
        model->dynInitializeA(0.0, 0);
        IOdata fields;
        const IOdata inputs{1.0, 0.0, 1.0, 1.0};
        model->dynInitializeB(inputs, {0.2, 0.05}, fields);
        model->setOffset(0, cDaeSolverMode);
        auto state = model->getStates();
        std::vector<double> rates(state.size(), 0.0);
        StateData data(0.0, state.data(), rates.data());
        data.stateSize = static_cast<count_t>(state.size());
        data.cj = 1.0;
        MatrixDataSparse<double> jacobian;
        model->jacobianElements(inputs, data, jacobian, {10, 11, 12, 13}, cDaeSolverMode);
        const auto residualAt = [&](const std::vector<double>& stateValues,
                                    const std::vector<double>& rateValues) {
            StateData trial(0.0, stateValues.data(), rateValues.data());
            trial.stateSize = static_cast<count_t>(stateValues.size());
            std::vector<double> residual(stateValues.size(), 0.0);
            model->residual(inputs, trial, residual.data(), cDaeSolverMode);
            return residual;
        };
        const auto base = residualAt(state, rates);
        constexpr double step = 1e-7;
        for (std::size_t col = 0; col < state.size(); ++col) {
            auto shifted = state;
            auto drates = rates;
            shifted[col] += step;
            drates[col] += step;
            const auto perturbed = residualAt(shifted, drates);
            for (std::size_t row = 0; row < state.size(); ++row) {
                EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(col)),
                            (perturbed[row] - base[row]) / step,
                            1e-5)
                    << "variant " << storageVariant << " row " << row << " column " << col;
            }
        }
    }
}

TEST(DistributedModels, ConverterPartitionedSolverStates)
{
    ESD1 model;
    model.set("socinit", 0.4);
    model.dynInitializeA(0.0, 0);
    IOdata fields;
    const IOdata inputs{1.0, 0.0, 1.0, 1.0};
    model.dynInitializeB(inputs, {0.2, 0.05}, fields);
    auto algMode = cDynAlgSolverMode;
    auto diffMode = cDynDiffSolverMode;
    algMode.pairedOffsetIndex = diffMode.offsetIndex;
    diffMode.pairedOffsetIndex = algMode.offsetIndex;
    const auto algSize = model.stateSize(algMode);
    const auto diffSize = model.stateSize(diffMode);
    model.setOffset(0, algMode);
    model.setOffset(0, diffMode);
    std::vector<double> algebraic(algSize);
    std::vector<double> differential(diffSize);
    std::vector<double> rates(differential.size());
    model.guessState(0.0, algebraic.data(), nullptr, algMode);
    model.guessState(0.0, differential.data(), rates.data(), diffMode);
    StateData algData(0.0, algebraic.data(), rates.data());
    algData.stateSize = static_cast<count_t>(algebraic.size());
    algData.diffState = differential.data();
    algData.pairIndex = diffMode.offsetIndex;
    std::vector<double> algResidual(algebraic.size());
    model.residual(inputs, algData, algResidual.data(), algMode);
    EXPECT_NEAR(algResidual[0], 0.0, 1e-12);
    EXPECT_NEAR(algResidual[1], 0.0, 1e-12);
    MatrixDataSparse<double> algJacobian;
    model.jacobianElements(inputs, algData, algJacobian, {10, 11, 12, 13}, algMode);
    EXPECT_NEAR(algJacobian.at(0, 0), -1.0, 1e-12);
    StateData diffData(0.0, differential.data(), rates.data());
    diffData.stateSize = static_cast<count_t>(differential.size());
    diffData.algState = algebraic.data();
    diffData.pairIndex = algMode.offsetIndex;
    diffData.cj = 1.0;
    std::vector<double> diffResidual(differential.size());
    model.residual(inputs, diffData, diffResidual.data(), diffMode);
    for (double value : diffResidual) {
        EXPECT_NEAR(value, 0.0, 1e-10);
    }
    MatrixDataSparse<double> diffJacobian;
    model.jacobianElements(inputs, diffData, diffJacobian, {10, 11, 12, 13}, diffMode);
    EXPECT_NEAR(diffJacobian.at(0, 0), (-1.0 / model.get("tip")) - 1.0, 1e-5);
}

TEST(DistributedModels, ProtectionUsesAreaRelayAndExternalPlayerValue)
{
    GridDynSimulation simulation;
    auto* bus = new AcBus("protectedBus");
    simulation.add(bus);
    auto* host = new RenewableGenerator("distributedDevice");
    auto* converter = new PVD1("converter");
    host->add(converter);
    bus->add(host);
    auto* relay = new DGPRCTExt("protection");
    relay->setSource(bus);
    relay->setSink(converter);
    relay->set("fen", 0.0);
    relay->set("ven", 1.0);
    simulation.add(relay);
    relay->pFlowObjectInitializeA(0.0, 0);
    relay->dynObjectInitializeA(0.0, 0);
    relay->set("external_voltage", 0.05);  // A Player can set the same property.
    relay->updateA(0.01);
    EXPECT_EQ(relay->get("lock"), 1.0);
    EXPECT_EQ(converter->get("blocked"), 1.0);
    EXPECT_NEAR(bus->get("voltage"), 1.0, 1e-12);
    relay->set("tres", 0.1);
    relay->set("external_voltage", 1.0);
    relay->updateA(0.05);
    EXPECT_EQ(relay->get("lock"), 1.0);
    relay->updateA(0.12);
    EXPECT_EQ(relay->get("lock"), 1.0);
    relay->updateA(0.16);
    EXPECT_EQ(relay->get("lock"), 0.0);
    EXPECT_EQ(converter->get("blocked"), 0.0);
}

TEST(DistributedModels, AndesJsonCreatesIndependentDevices)
{
    const auto path = std::filesystem::temp_directory_path() / "griddyn_distributed_import.json";
    {
        std::ofstream file(path);
        file << R"({"Bus":[{"idx":1,"name":"bus1","Vn":100,"v0":1}],
"PV":[{"idx":3,"bus":1,"p0":0.2,"q0":0.02}],
"BusFreq":[{"idx":"frequency1","bus":1}],
"PVD1":[{"idx":"pvA","name":"pvA","bus":1,"gen":3,"Sn":10,
"pqflag":1,"gammap":0.5,"gammaq":0.5,"busf":"frequency1"},
{"idx":"pvB","name":"pvB","bus":1,"gen":3,"Sn":10,
"pqflag":1,"gammap":0.5,"gammaq":0.5,"busf":"frequency1"}],
"ESD1":[{"idx":"storage","bus":1,"gen":3,"Sn":10,"pqflag":1,"SOCinit":0.7}],
"EV1":[{"idx":"vehicle1","bus":1,"gen":3,"Sn":10,"pqflag":1,"pmn":-0.4}],
"EV2":[{"idx":"vehicle2","bus":1,"gen":3,"Sn":10,"pqflag":1,"pmn":-0.4,"pcap":0.6}],
"DGPRCT1":[{"idx":"tripA","dev":"pvA","fen":0,"Ven":1}],
"DGPRCTExt":[{"idx":"tripB","dev":"vehicle2","fen":0,"Ven":1}]})";
    }
    GridDynSimulation simulation;
    ASSERT_TRUE(loadAndesJson(&simulation, path.string()));
    std::filesystem::remove(path);
    auto* bus = simulation.getBus(0);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(bus->getGen(0), nullptr);
    EXPECT_FALSE(bus->getGen(0)->isEnabled());
    auto* first = dynamic_cast<RenewableGenerator*>(bus->getGen(1));
    auto* second = dynamic_cast<RenewableGenerator*>(bus->getGen(2));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first->find("electrical"), second->find("electrical"));
    EXPECT_NEAR(first->get("p"), -0.1, 1e-12);  // GridDyn's bus-injection sign.
    auto* storage = dynamic_cast<RenewableGenerator*>(bus->getGen(3));
    auto* vehicle1 = dynamic_cast<RenewableGenerator*>(bus->getGen(4));
    auto* vehicle2 = dynamic_cast<RenewableGenerator*>(bus->getGen(5));
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(vehicle1, nullptr);
    ASSERT_NE(vehicle2, nullptr);
    auto* storageModel = dynamic_cast<ESD1*>(storage->find("electrical"));
    auto* vehicle1Model = dynamic_cast<EV1*>(vehicle1->find("electrical"));
    auto* vehicle2Model = dynamic_cast<EV2*>(vehicle2->find("electrical"));
    ASSERT_NE(storageModel, nullptr);
    ASSERT_NE(vehicle1Model, nullptr);
    ASSERT_NE(vehicle2Model, nullptr);
    EXPECT_NEAR(storageModel->get("socinit"), 0.7, 1e-12);
    EXPECT_NEAR(vehicle1Model->get("pmn"), -0.4, 1e-12);
    EXPECT_NEAR(vehicle2Model->get("pcap"), 0.6, 1e-12);
    ASSERT_NE(simulation.getRelay(0), nullptr);
    EXPECT_NE(dynamic_cast<DGPRCT1*>(simulation.getRelay(0)), nullptr);
    EXPECT_NE(dynamic_cast<DGPRCTExt*>(simulation.getRelay(1)), nullptr);
}

TEST(DistributedModels, XmlFactoryAndPlayerUpdate)
{
    const auto path = std::filesystem::temp_directory_path() / "griddyn_distributed_model.xml";
    {
        std::ofstream file(path);
        file << R"(<griddyn name="distributed_xml"><bus name="bus1">
<generator name="pv" type="renewable_dynamic" p="0.2" q="0.0" mbase="100">
<renewable_model name="pv_electrical" type="pvd1" pqflag="1" pref="0.2"/>
</generator></bus>
<relay name="pv_protection" type="dgprct1" source="bus1" sink="pv_electrical"
       fen="0" ven="1"/>
</griddyn>)";
    }
    GridDynSimulation simulation;
    loadFile(&simulation, path.string());
    std::filesystem::remove(path);
    auto* bus = simulation.getBus(0);
    ASSERT_NE(bus, nullptr);
    auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(host, nullptr);
    auto* model = dynamic_cast<PVD1*>(host->find("electrical"));
    ASSERT_NE(model, nullptr);
    EXPECT_NEAR(model->get("pref"), 0.2, 1e-12);
    EXPECT_NE(dynamic_cast<DGPRCT1*>(simulation.getRelay(0)), nullptr);
    events::Player player(0.0, 0.0);
    ASSERT_TRUE(player.setTarget(model, "pref"));
    player.setTimeValue(std::vector<CoreTime>{0.1}, std::vector<double>{0.35});
    player.initialize();
    player.trigger(0.1);
    EXPECT_NEAR(model->get("pref"), 0.35, 1e-12);
}
