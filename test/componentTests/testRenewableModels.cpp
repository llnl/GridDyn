/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "fileInput/ReaderInfo.h"
#include "fileInput/fileInput.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/renewables/REECA1.h"
#include "griddyn/renewables/REECB1.h"
#include "griddyn/renewables/REGCA1.h"
#include "griddyn/renewables/REPCA1.h"
#include "griddyn/renewables/WTARA1.h"
#include "griddyn/renewables/WTDTA1.h"
#include "griddyn/renewables/WTPTA1.h"
#include "griddyn/renewables/WTTQA1.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace griddyn;

namespace {
std::string renewableDyrRecord(std::string_view model, int busId = 101)
{
    std::string payload;
    if (model == "REGCA1") {
        payload = "1 .1 10 1 .5 1 1.2 .8 .4 -1.5 .1 .7 1 -1 0";
    } else if (model == "REECA1") {
        payload = "0 0 1 0 0 1 .8 1.2 .02 -.02 .02 1 999 -999 0 0 0 0 "
                  ".02 999 -999 999 -999 1 .1 1 .1 1 .02 999 -999 999 0 999 .02 "
                  ".2 2 .4 4 .8 8 1 10 .2 2 .4 4 .8 8 1 12";
    } else if (model == "REECB1") {
        payload = "0 0 1 0 1 .8 1.2 .02 -.02 .02 1 999 -999 0 .02 "
                  "999 -999 999 -999 1 .1 1 .1 .02 999 -999 999 0 999 .02";
    } else if (model == "REPCA1") {
        payload = "0 0 0 '1' 0 1 0 .02 1 .1 1 1 .8 0 0 0 "
                  "999 -999 -.1 .1 999 -999 1 .1 .02 -.0002833 .0002833 "
                  ".05 -.05 999 -999 .02 10 10";
    } else if (model == "WTDTA1") {
        payload = "3 0 .5 1 1";
    } else if (model == "WTARA1") {
        payload = "1 0";
    } else if (model == "WTPTA1") {
        payload = ".1 0 .1 0 0 .3 30 0 5 -5";
    } else if (model == "WTTQA1") {
        payload = "0 0 .1 .05 30 3 0 .4 .58 .8 .72 1.2 .86 1.6 1 999";
    }
    return std::to_string(busId) + " '" + std::string{model} + "' '1' " + payload + " /\n";
}

std::unique_ptr<GridDynSimulation> renewableDyrSimulation()
{
    auto simulation = std::make_unique<GridDynSimulation>();
    auto* bus = new AcBus("bus101");
    bus->setUserID(101);
    simulation->add(bus);
    auto* generator = new Generator("bus101_Gen_1");
    generator->setUserID(77);
    generator->set("p", 0.8);
    generator->set("q", 0.1);
    generator->set("mbase", 50.0, units::MVAR);
    bus->add(generator);
    return simulation;
}

void loadRenewableRecords(GridDynSimulation& simulation,
                          const std::vector<std::string_view>& models,
                          int busId = 101)
{
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_renewable_" + std::to_string(simulation.getID()) + ".dyr");
    {
        std::ofstream stream(file);
        for (auto model : models) {
            stream << renewableDyrRecord(model, busId);
        }
    }
    try {
        loadDyr(&simulation, file.string(), BasicReaderInfo{});
    }
    catch (...) {
        std::filesystem::remove(file);
        throw;
    }
    std::filesystem::remove(file);
}

void loadOtherMachines(GridDynSimulation& simulation)
{
    const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14_gencls.dyr";
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_renewable_other_" + std::to_string(simulation.getID()) + ".dyr");
    {
        std::ifstream input(source);
        std::ofstream output(file);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.starts_with("2 'GENCLS'")) {
                output << line << '\n';
            }
        }
    }
    try {
        loadDyr(&simulation, file.string(), BasicReaderInfo{});
    }
    catch (...) {
        std::filesystem::remove(file);
        throw;
    }
    std::filesystem::remove(file);
}
}  // namespace

TEST(RenewableModels, FactoryAndRoleReplacement)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> hostObject(factory->createObject("generator", "renewable_dynamic"));
    std::unique_ptr<CoreObject> modelObject(factory->createObject("renewable_model", "regca1"));
    ASSERT_NE(dynamic_cast<RenewableGenerator*>(hostObject.get()), nullptr);
    ASSERT_NE(dynamic_cast<REGCA1*>(modelObject.get()), nullptr);

    RenewableGenerator host;
    EXPECT_THROW(host.dynInitializeA(0.0, 0), InvalidParameterValue);
    auto* first = new REGCA1("first");
    host.add(first);
    EXPECT_EQ(host.find("electrical"), first);
    auto* second = new REGCA1("second");
    host.add(second);
    EXPECT_EQ(host.find("electrical"), second);
    EXPECT_EQ(host.getSubObject("renewable_component", 0), second);
    std::unique_ptr<CoreObject> copy(host.clone());
    auto* copiedHost = dynamic_cast<RenewableGenerator*>(copy.get());
    ASSERT_NE(copiedHost, nullptr);
    auto* copiedConverter = dynamic_cast<REGCA1*>(copiedHost->find("electrical"));
    ASSERT_NE(copiedConverter, nullptr);
    EXPECT_NE(copiedConverter, second);
    host.remove(second);
    EXPECT_EQ(host.find("electrical"), nullptr);
}

TEST(RenewableModels, REGCA1SteadyInitializationAndHostSign)
{
    RenewableGenerator host;
    auto* converter = new REGCA1;
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    ASSERT_EQ(converter->getStates().size(), 5U);
    EXPECT_NEAR(converter->getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(converter->getStates()[1], 0.1, 1e-12);
    EXPECT_NEAR(converter->getStates()[2], 0.8, 1e-12);
    EXPECT_NEAR(converter->getStates()[3], 0.1, 1e-12);
    EXPECT_NEAR(converter->getStates()[4], 1.0, 1e-12);
    const auto output = host.getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode);
    ASSERT_EQ(output.size(), 2U);
    EXPECT_NEAR(output[0], -0.8, 1e-12);
    EXPECT_NEAR(output[1], -0.1, 1e-12);
    double residual[5]{};
    converter->residual({1.0, kNullVal, kNullVal}, emptyStateData, residual, cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> daeState(host.stateSize(cDaeSolverMode));
    std::vector<double> daeRate(daeState.size());
    ASSERT_EQ(daeState.size(), 5U);
    host.guessState(0.0, daeState.data(), daeRate.data(), cDaeSolverMode);
    StateData stateData(0.0, daeState.data(), daeRate.data());
    stateData.stateSize = static_cast<count_t>(daeState.size());
    std::vector<double> daeResidual(daeState.size());
    host.residual({1.0, 0.0}, stateData, daeResidual.data(), cDaeSolverMode);
    for (double value : daeResidual) {
        EXPECT_NEAR(value, 0.0, 1e-12);
    }
    const auto daeOutput = host.getOutputs({1.0, 0.0}, stateData, cDaeSolverMode);
    EXPECT_NEAR(daeOutput[0], -0.8, 1e-12);
    EXPECT_NEAR(daeOutput[1], -0.1, 1e-12);
}

TEST(RenewableModels, REGCA1AcceptsCaseAccelerationAndValidatesRange)
{
    REGCA1 converter;
    converter.set("Accel", 0.8);
    EXPECT_DOUBLE_EQ(converter.get("Accel"), 0.8);
    converter.dynInitializeA(0.0, 0);
    converter.set("Accel", 1.1);
    EXPECT_THROW(converter.dynInitializeA(0.0, 0), InvalidParameterValue);
    converter.set("Accel", 0.8);
    converter.set("Tg", 0.0);
    EXPECT_THROW(converter.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, REGCA1ZeroReactiveRecoveryLimitsAreDirectional)
{
    REGCA1 converter;
    converter.set("iqrmin", -1.0);
    converter.set("iqrmax", 0.0);
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    converter.dynInitializeB({1.0, 0.0, 0.0}, {0.8, 0.1}, fields);
    std::vector<double> rate(converter.getStates().size());
    converter.derivative({1.0, 0.8, -0.4}, emptyStateData, rate.data(), cLocalSolverMode);
    EXPECT_NEAR(rate[3], 3.0, 1e-12);
    converter.set("iqrmax", 2.0);
    converter.derivative({1.0, 0.8, -0.4}, emptyStateData, rate.data(), cLocalSolverMode);
    EXPECT_NEAR(rate[3], 2.0, 1e-12);
}

TEST(RenewableModels, REECA1ZeroCurvesAndZeroOrderingLag)
{
    REECA1 controller;
    controller.set("tpord", 0.0);
    controller.set("imax", 1.3);
    for (int index = 1; index <= 4; ++index) {
        const auto suffix = std::to_string(index);
        controller.set("vq" + suffix, 0.0);
        controller.set("iq" + suffix, 0.0);
        controller.set("vp" + suffix, 0.0);
        controller.set("ip" + suffix, 0.0);
    }
    controller.dynInitializeA(0.0, 0);
    IOdata fields;
    controller.dynInitializeB({1.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(fields[0], 0.8, 1e-12);
    EXPECT_NEAR(fields[1], -0.1, 1e-12);
    EXPECT_NEAR(controller.getOutputs({1.0}, emptyStateData, cLocalSolverMode)[2], 0.8, 1e-12);
    EXPECT_DOUBLE_EQ(controller.get("vq4"), 0.0);
    double update[6]{};
    EXPECT_THROW(controller.algebraicUpdate({0.7}, emptyStateData, update, cLocalSolverMode, 1.0),
                 InvalidParameterValue);
    controller.set("thld2", 0.5);
    EXPECT_DOUBLE_EQ(controller.get("thld2"), 0.5);
    EXPECT_NO_THROW(controller.dynInitializeA(0.0, 0));
    EXPECT_EQ(controller.rootSize(cDaeSolverMode), 2U);
}

TEST(RenewableModels, ACTIVSg25kDyrProfileLoadsRecoveryHold)
{
    auto simulation = renewableDyrSimulation();
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_activsg25k_profile_" + std::to_string(simulation->getID()) + ".dyr");
    {
        std::ofstream output(file);
        output << "101 'REGCA1' '1' 1 0.02 10 0.9 0.5 1.22 1.2 0.8 0.4 "
                  "-1.3 0.02 0.7 0 0 0.8 /\n";
        output << "101 'REECA1' '1' 0 0 0 0 0 0 0.85 1.15 0.033333 "
                  "0 0 5 1.1 -1.1 0 0 0 0.5 0 0.436 -0.436 1.1 0.9 "
                  "0 0.1 18 5 0 0.033333 99 -99 1 0 1.3 0 "
                  "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 /\n";
    }
    EXPECT_NO_THROW(loadDyr(simulation.get(), file.string(), BasicReaderInfo{}));
    std::filesystem::remove(file);
    auto* generator = dynamic_cast<RenewableGenerator*>(simulation->getBus(0)->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* converter = dynamic_cast<REGCA1*>(generator->find("electrical"));
    ASSERT_NE(converter, nullptr);
    EXPECT_DOUBLE_EQ(converter->get("accel"), 0.8);
    auto* electrical = dynamic_cast<REECA1*>(generator->find("electrical_control"));
    ASSERT_NE(electrical, nullptr);
    EXPECT_DOUBLE_EQ(electrical->get("thld2"), 0.5);
    EXPECT_DOUBLE_EQ(electrical->get("tpord"), 0.0);
    EXPECT_EQ(electrical->rootSize(cDaeSolverMode), 2U);
}

TEST(RenewableModels, REECA1HoldsDipLimitAfterRecovery)
{
    REECA1 controller;
    controller.set("thld2", 0.5);
    controller.set("tpord", 0.0);
    controller.set("pqflag", 0.0);
    controller.set("imax", 0.85);
    controller.set("qmax", 1.0);
    controller.set("qmin", -1.0);
    for (int index = 1; index <= 4; ++index) {
        const auto suffix = std::to_string(index);
        for (auto prefix : {"vq", "iq", "vp", "ip"}) {
            controller.set(std::string{prefix} + suffix, 0.0);
        }
    }
    controller.dynInitializeA(0.0, 0);
    IOdata fields;
    controller.dynInitializeB({1.0}, {0.8, 0.1}, fields);
    controller.setRootOffset(0, cLocalSolverMode);
    double roots[2]{};
    controller.rootTest({1.0}, StateData(0.0), roots, cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    controller.rootTest({0.7}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    controller.timestep(0.1, {1.0, 0.0, 0.0, 0.8}, cLocalSolverMode);
    controller.rootTrigger(0.1, {0.7}, {-1, 0}, cLocalSolverMode);
    double update[6]{};
    EXPECT_NO_THROW(
        controller.algebraicUpdate({0.7}, emptyStateData, update, cLocalSolverMode, 1.0));
    for (int step = 11; step <= 19; ++step) {
        controller.timestep(step * 0.01, {0.7, 0.0, 0.0, 0.8}, cLocalSolverMode);
    }
    EXPECT_NEAR(controller.getOutputs({0.7}, emptyStateData, cLocalSolverMode)[2], 0.8, 1e-12);
    EXPECT_LT(controller.getOutputs({0.7}, emptyStateData, cLocalSolverMode)[0], 0.79);

    controller.rootTrigger(0.2, {1.0}, {1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(0.2), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.5, 1e-12);
    controller.timestep(0.21, {1.0, 0.0, 0.0, 0.8}, cLocalSolverMode);
    const double heldCurrent = controller.getOutputs({1.0}, emptyStateData, cLocalSolverMode)[0];
    EXPECT_LT(heldCurrent, 0.79);
    controller.rootTest({1.0}, StateData(0.7), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.0, 1e-9);
    controller.rootTrigger(0.7, {1.0}, {0, -1}, cLocalSolverMode);
    EXPECT_GT(controller.getOutputs({1.0}, emptyStateData, cLocalSolverMode)[0],
              heldCurrent + 0.02);
}

TEST(RenewableModels, REECA1RestartsHoldOnSecondDipAndHandlesHighVoltage)
{
    REECA1 controller;
    controller.set("thld2", 0.5);
    controller.dynInitializeA(0.0, 0);
    IOdata fields;
    controller.dynInitializeB({1.0}, {0.8, 0.1}, fields);
    controller.setRootOffset(0, cLocalSolverMode);
    double roots[2]{};

    controller.rootTest({1.3}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    controller.rootTrigger(0.1, {1.3}, {-1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(0.2), roots, cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    controller.rootTrigger(0.2, {1.0}, {1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(0.6), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.1, 1e-12);

    controller.rootTrigger(0.6, {0.7}, {-1, 0}, cLocalSolverMode);
    controller.rootTest({0.7}, StateData(0.8), roots, cLocalSolverMode);
    EXPECT_GT(roots[1], 0.0);
    controller.rootTrigger(0.8, {1.0}, {1, 0}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(1.0), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.3, 1e-12);
    controller.rootTrigger(1.3, {1.0}, {0, -1}, cLocalSolverMode);
    controller.rootTest({1.0}, StateData(1.4), roots, cLocalSolverMode);
    EXPECT_GT(roots[1], 0.0);
}

TEST(RenewableModels, RenewableGeneratorForwardsREECA1VoltageRoots)
{
    RenewableGenerator host;
    auto* electrical = new REECA1;
    electrical->set("thld2", 0.5);
    electrical->set("tpord", 0.0);
    auto* converter = new REGCA1;
    host.add(electrical);
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    ASSERT_EQ(host.rootSize(cLocalSolverMode), 2U);
    EXPECT_EQ(electrical->rootSize(cLocalSolverMode), 2U);
    host.setRootOffset(0, cLocalSolverMode);
    double roots[2]{};
    host.rootTest({0.7, 0.0}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    host.rootTrigger(0.1, {0.7, 0.0}, {-1, 0}, cLocalSolverMode);
    electrical->rootTest({0.7}, StateData(0.1), roots, cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    host.rootTrigger(0.2, {1.0, 0.0}, {1, 0}, cLocalSolverMode);
    host.rootTest({1.0, 0.0}, StateData(0.2), roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.5, 1e-12);
}

TEST(RenewableModels, REGCA1VoltageStepAndMachineBase)
{
    RenewableGenerator host;
    host.set("mbase", 50.0, units::MVAR);
    auto* converter = new REGCA1;
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.4, 0.05}, fields);
    EXPECT_NEAR(converter->getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.4, 1e-12);
    host.timestep(0.1, {0.6, 0.0}, cLocalSolverMode);
    EXPECT_NEAR(converter->getStates()[4], 0.6, 1e-12);
    EXPECT_NEAR(converter->getStates()[0], 0.24, 1e-12);
    EXPECT_NEAR(host.getOutputs({0.6, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.12, 1e-12);
}

TEST(RenewableModels, REGCA1DaeJacobianMatchesResidual)
{
    REGCA1 converter;
    converter.dynInitializeA(0.0, 0);
    IOdata fields;
    const IOdata inputs{1.0, kNullVal, kNullVal};
    converter.dynInitializeB(inputs, {0.8, 0.1}, fields);
    converter.setOffset(0, cDaeSolverMode);
    const auto state = converter.getStates();
    std::vector<double> dstate(state.size(), 0.0);
    StateData stateData(0.0, state.data(), dstate.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    stateData.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    converter.jacobianElements(
        inputs, stateData, jacobian, {6, kNullLocation, kNullLocation}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& values,
                                const std::vector<double>& rates) {
        StateData trial(0.0, values.data(), rates.data());
        trial.stateSize = static_cast<count_t>(values.size());
        std::vector<double> residual(values.size());
        converter.residual(inputs, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = residualAt(state, dstate);
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto shifted = state;
        auto rates = dstate;
        shifted[column] += step;
        rates[column] += step;
        const auto residual = residualAt(shifted, rates);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (residual[row] - base[row]) / step,
                        1e-5)
                << "row " << row << " column " << column;
        }
    }
}

TEST(RenewableModels, IndependentElectricalAndPlantControls)
{
    RenewableGenerator host;
    auto* converter = new REGCA1;
    auto* electrical = new REECA1;
    auto* plant = new REPCA1;
    plant->set("dbd2", 0.0);
    host.add(plant);
    host.add(electrical);
    host.add(converter);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    EXPECT_NEAR(electrical->getStates()[0], 0.8, 1e-12);
    EXPECT_NEAR(electrical->getStates()[1], -0.1, 1e-12);
    EXPECT_NEAR(plant->getStates()[0], 0.0, 1e-12);
    EXPECT_NEAR(plant->getStates()[1], 0.0, 1e-12);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size(), 0.0);
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    std::vector<double> residual(state.size(), 0.0);
    host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-10);
    }

    plant->set("vref", 1.05);
    host.timestep(0.01, {1.0, 0.0}, cLocalSolverMode);
    EXPECT_GT(plant->getStates()[1], 0.0);
    EXPECT_GT(electrical->getStates()[5], 0.1);
    EXPECT_NEAR(host.getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.8, 1e-3);
}

TEST(RenewableModels, REECA1UnsupportedModesFail)
{
    REECA1 electrical;
    EXPECT_THROW(electrical.set("PFFLAG", 0.5), InvalidParameterValue);
    electrical.set("PFFLAG", 1.0);
    EXPECT_THROW(electrical.dynInitializeA(0.0, 0), InvalidParameterValue);
    REPCA1 plant;
    EXPECT_THROW(plant.set("RefFlag", 0.5), InvalidParameterValue);
    plant.set("Fflag", 1.0);
    EXPECT_THROW(plant.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, DyrAssemblesIndependentModelsInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECA1", "REPCA1"},
          std::vector<std::string_view>{"REPCA1", "REECA1", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* generator = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(generator, nullptr);
        EXPECT_EQ(generator->getName(), "bus101_Gen_1");
        EXPECT_EQ(generator->getUserID(), 77);
        EXPECT_NEAR(generator->get("mbase", units::MVAR), 50.0, 1e-12);
        auto* converter = dynamic_cast<REGCA1*>(generator->find("electrical"));
        auto* electrical = dynamic_cast<REECA1*>(generator->find("electrical_control"));
        auto* plant = dynamic_cast<REPCA1*>(generator->find("plant_control"));
        ASSERT_NE(converter, nullptr);
        ASSERT_NE(electrical, nullptr);
        ASSERT_NE(plant, nullptr);
        EXPECT_DOUBLE_EQ(converter->get("tg"), 0.1);
        EXPECT_DOUBLE_EQ(electrical->get("pqflag"), 1.0);
        EXPECT_DOUBLE_EQ(plant->get("refflag"), 1.0);
        generator->dynInitializeA(0.0, 0);
        IOdata fields;
        generator->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        const auto output = generator->getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode);
        EXPECT_NEAR(output[0], -0.8, 1e-12);
        EXPECT_NEAR(output[1], -0.1, 1e-12);
    }
}

TEST(RenewableModels, REECB1LoadsAsIndependentElectricalControl)
{
    std::unique_ptr<CoreObject> factoryObject(
        CoreObjectFactory::instance()->createObject("renewable_model", "reecb1"));
    ASSERT_NE(dynamic_cast<REECB1*>(factoryObject.get()), nullptr);
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECB1"},
          std::vector<std::string_view>{"REECB1", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        auto* control = dynamic_cast<REECB1*>(host->find("electrical_control"));
        ASSERT_NE(control, nullptr);
        EXPECT_DOUBLE_EQ(control->get("imax"), 999.0);
        EXPECT_THROW(control->set("ip1", 2.0), InvalidParameterValue);
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        EXPECT_NEAR(host->getOutputs({1.0, 0.0}, emptyStateData, cLocalSolverMode)[0], -0.8, 1e-12);
        std::unique_ptr<CoreObject> copy(control->clone());
        EXPECT_NE(dynamic_cast<REECB1*>(copy.get()), nullptr);
    }
}

TEST(RenewableModels, DyrRejectsDuplicateAndUnsupportedRecords)
{
    auto duplicate = renewableDyrSimulation();
    EXPECT_THROW(loadRenewableRecords(*duplicate, {"REGCA1", "REGCA1"}), InvalidParameterValue);
    auto missingConverter = renewableDyrSimulation();
    loadRenewableRecords(*missingConverter, {"REECA1"});
    auto* bus = dynamic_cast<GridBus*>(missingConverter->findByUserID("bus", 101));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    EXPECT_THROW(generator->dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, ACTIVSg25kType3WindDoesNotAliasNewerWindModels)
{
    auto simulation = renewableDyrSimulation();
    const auto file = std::filesystem::temp_directory_path() /
        ("griddyn_activsg25k_wt3_" + std::to_string(simulation->getID()) + ".dyr");
    {
        std::ofstream output(file);
        output << "101 'WT3G1' '1' 3 0.8 30 0 0.1 2.75 /\n";
        output << "101 'WT3E1' '1' 0 1 1 0 0 0 0.15 18 5 0 0.05 "
                  "3 0.6 1.12 0.04 0.436 -0.436 1.7 0.02 0.45 -0.45 "
                  "60 0.1 0.9 1.1 40 0.5 1.45 0.05 0.05 1 "
                  "0.69 0.78 0.98 1.12 0.74 1.2 /\n";
        output << "101 'WT3T1' '1' 1.25 4.95 0 0.007 21.98 0 1.8 2.3 /\n";
        output << "101 'WT3P1' '1' 0.3 150 25 3 30 0 27 10 1 /\n";
    }
    try {
        loadDyr(simulation.get(), file.string(), BasicReaderInfo{});
        FAIL() << "WT3 models require their own equations";
    }
    catch (const InvalidParameterValue& error) {
        const std::string message = error.what();
        for (auto name : {"WT3G1", "WT3E1", "WT3T1", "WT3P1"}) {
            EXPECT_NE(message.find(name), std::string::npos);
        }
    }
    std::filesystem::remove(file);
    EXPECT_EQ(dynamic_cast<RenewableGenerator*>(simulation->getBus(0)->getGen(0)), nullptr);

    auto dyd = file;
    dyd.replace_extension(".dyd");
    {
        std::ofstream output(dyd);
        output << "wt3e 101 \"bus101\" 13.8 \"1\" : #9 0 1 1 0 /\n";
    }
    try {
        loadDyd(simulation.get(), dyd.string(), BasicReaderInfo{});
        FAIL() << "WT3 DYD must remain unsupported";
    }
    catch (const InvalidParameterValue& error) {
        EXPECT_NE(std::string{error.what()}.find("WT3E"), std::string::npos);
    }
    std::filesystem::remove(dyd);
}

TEST(RenewableModels, ThreeModelDaeJacobianIncludesSignalConnections)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    host.add(new REPCA1);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    host.jacobianElements({1.0, 0.0}, data, jacobian, {20, 21}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& values,
                                const std::vector<double>& rates) {
        StateData trial(0.0, values.data(), rates.data());
        trial.stateSize = static_cast<count_t>(values.size());
        std::vector<double> residual(values.size());
        host.residual({1.0, 0.0}, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = residualAt(state, rate);
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto shifted = state;
        auto shiftedRate = rate;
        shifted[column] += step;
        shiftedRate[column] += step;
        const auto residual = residualAt(shifted, shiftedRate);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (residual[row] - base[row]) / step,
                        2e-4)
                << "row " << row << " column " << column;
        }
    }
}

TEST(RenewableModels, WindDyrAssemblesConnectedChainInEitherOrder)
{
    for (const std::vector<std::string_view>& order :
         {std::vector<std::string_view>{"REGCA1", "REECA1", "WTDTA1", "WTARA1", "WTPTA1", "WTTQA1"},
          std::vector<std::string_view>{
              "WTTQA1", "WTPTA1", "WTARA1", "WTDTA1", "REECA1", "REGCA1"}}) {
        auto simulation = renewableDyrSimulation();
        loadRenewableRecords(*simulation, order);
        auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 101));
        ASSERT_NE(bus, nullptr);
        auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
        ASSERT_NE(host, nullptr);
        auto* drivetrain = dynamic_cast<WTDTA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::driveTrain)));
        auto* aero = dynamic_cast<WTARA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::aerodynamics)));
        auto* pitch = dynamic_cast<WTPTA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::pitchControl)));
        auto* torque = dynamic_cast<WTTQA1*>(
            host->getSubObject("renewable_component",
                               static_cast<index_t>(RenewableRole::torqueControl)));
        ASSERT_NE(drivetrain, nullptr);
        ASSERT_NE(aero, nullptr);
        ASSERT_NE(pitch, nullptr);
        ASSERT_NE(torque, nullptr);
        EXPECT_DOUBLE_EQ(drivetrain->get("htfrac"), 0.5);
        EXPECT_DOUBLE_EQ(pitch->get("rtetamin"), -5.0);
        EXPECT_DOUBLE_EQ(torque->get("spd4"), 1.0);
        host->dynInitializeA(0.0, 0);
        IOdata fields;
        host->dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
        EXPECT_NEAR(drivetrain->getOutput(0), 1.0, 1e-12);
        EXPECT_NEAR(drivetrain->getOutput(1), 1.0, 1e-12);
        EXPECT_NEAR(aero->getOutput(0), 1.6, 1e-12);
        EXPECT_NEAR(pitch->getOutput(0), 0.0, 1e-12);
        EXPECT_NEAR(torque->getOutput(0), 1.6, 1e-12);
        host->setOffset(0, cDaeSolverMode);
        std::vector<double> state(host->stateSize(cDaeSolverMode));
        std::vector<double> rate(state.size());
        host->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
        StateData data(0.0, state.data(), rate.data());
        data.stateSize = static_cast<count_t>(state.size());
        std::vector<double> residual(state.size());
        host->residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
        for (double value : residual) {
            EXPECT_NEAR(value, 0.0, 1e-8);
        }
    }
}

TEST(RenewableModels, WindAssemblyRejectsMissingLinksAndInvalidParameters)
{
    RenewableGenerator missingTorque;
    missingTorque.add(new REGCA1);
    missingTorque.add(new REECA1);
    missingTorque.add(new WTDTA1);
    EXPECT_THROW(missingTorque.dynInitializeA(0.0, 0), InvalidParameterValue);
    WTDTA1 drivetrain;
    drivetrain.set("htfrac", 0.0);
    EXPECT_THROW(drivetrain.dynInitializeA(0.0, 0), InvalidParameterValue);
    WTPTA1 pitch;
    pitch.set("rtetamax", 0.0);
    EXPECT_THROW(pitch.dynInitializeA(0.0, 0), InvalidParameterValue);
    WTTQA1 torque;
    EXPECT_THROW(torque.set("Tflag", 0.5), InvalidParameterValue);
    torque.set("p2", 0.2);
    EXPECT_THROW(torque.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(RenewableModels, WindChainDaeJacobianIncludesMechanicalFeedback)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    auto* drivetrain = new WTDTA1;
    auto* aero = new WTARA1;
    auto* pitch = new WTPTA1;
    auto* torque = new WTTQA1;
    torque->set("temax", 3.0);
    host.add(drivetrain);
    host.add(aero);
    host.add(pitch);
    host.add(torque);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.8, 0.1}, fields);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    const auto theta = pitch->getOutputLoc(cDaeSolverMode, 0);
    const auto windGenerator = drivetrain->getOutputLoc(cDaeSolverMode, 0);
    const auto wref = torque->getOutputLoc(cDaeSolverMode, 1);
    state[theta] = 2.0;
    state[theta + 1] = 1.0;
    state[theta + 2] = 1.0;
    state[windGenerator] = 1.02;
    state[windGenerator + 1] = 1.01;
    state[wref - 1] = 0.7;
    state[wref] = 0.95;
    state[wref + 1] = 0.8;
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 1.0;
    MatrixDataSparse<double> jacobian;
    host.jacobianElements({1.0, 0.0}, data, jacobian, {30, 31}, cDaeSolverMode);
    const auto residualAt = [&](const std::vector<double>& values,
                                const std::vector<double>& rates) {
        StateData trial(0.0, values.data(), rates.data());
        trial.stateSize = static_cast<count_t>(values.size());
        std::vector<double> residual(values.size());
        host.residual({1.0, 0.0}, trial, residual.data(), cDaeSolverMode);
        return residual;
    };
    const auto base = residualAt(state, rate);
    constexpr double step = 1e-7;
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto shifted = state;
        auto shiftedRate = rate;
        shifted[column] += step;
        shiftedRate[column] += step;
        const auto residual = residualAt(shifted, shiftedRate);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (residual[row] - base[row]) / step,
                        3e-4)
                << "row " << row << " column " << column;
        }
    }
}

TEST(RenewableModels, PitchChangesMechanicalPower)
{
    WTPTA1 pitch;
    pitch.set("kpw", 10.0);
    pitch.dynInitializeA(0.0, 0);
    IOdata fields;
    pitch.dynInitializeB({1.0, 0.8, 0.8, 1.0, 0.0}, {}, fields);
    pitch.timestep(0.1, {1.05, 0.8, 0.8, 1.0, 0.0}, cLocalSolverMode);
    EXPECT_GT(pitch.getOutput(0), 0.0);
    WTARA1 aero;
    aero.dynInitializeA(0.0, 0);
    aero.dynInitializeB({0.0}, {0.8}, fields);
    aero.timestep(0.1, {pitch.getOutput(0)}, cLocalSolverMode);
    EXPECT_LT(aero.getOutput(0), 0.8);
}

TEST(RenewableModels, WindTorqueSetsNonunitySteadySpeed)
{
    RenewableGenerator host;
    host.add(new REGCA1);
    host.add(new REECA1);
    auto* drivetrain = new WTDTA1;
    auto* torque = new WTTQA1;
    host.add(drivetrain);
    host.add(torque);
    host.dynInitializeA(0.0, 0);
    IOdata fields;
    host.dynInitializeB({1.0, 0.0}, {0.5, 0.1}, fields);
    EXPECT_NEAR(drivetrain->getOutput(0), 0.79, 1e-12);
    EXPECT_NEAR(drivetrain->getOutput(1), 0.79, 1e-12);
    EXPECT_NEAR(torque->getOutput(0), 0.5, 1e-12);
    host.setOffset(0, cDaeSolverMode);
    std::vector<double> state(host.stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size());
    host.guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    std::vector<double> residual(state.size());
    host.residual({1.0, 0.0}, data, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 1e-8);
    }
}

TEST(RenewableModels, WindDyrIntegratesInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    auto simulation = std::make_unique<GridDynSimulation>();
    loadFile(simulation.get(), raw.string());
    loadRenewableRecords(*simulation,
                         {"REGCA1", "REECA1", "WTDTA1", "WTARA1", "WTPTA1", "WTTQA1"},
                         2);
    loadOtherMachines(*simulation);
    auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(dynamic_cast<RenewableGenerator*>(bus->getGen(0)), nullptr);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.1), 0);
    for (double value : simulation->getState()) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

TEST(RenewableModels, SolarDyrIntegratesInNetwork)
{
    const auto raw = std::filesystem::path(__FILE__).parent_path().parent_path() / "test_files" /
        "comparison_tests" / "ieee14.raw";
    for (auto electricalControl : {"REECA1", "REECB1"}) {
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation.get(), raw.string());
        loadRenewableRecords(*simulation, {"REGCA1", electricalControl}, 2);
        if (std::string_view{electricalControl} == "REECA1") {
            auto* bus = dynamic_cast<GridBus*>(simulation->findByUserID("bus", 2));
            ASSERT_NE(bus, nullptr);
            auto* host = dynamic_cast<RenewableGenerator*>(bus->getGen(0));
            ASSERT_NE(host, nullptr);
            auto* electrical = dynamic_cast<REECA1*>(host->find("electrical_control"));
            ASSERT_NE(electrical, nullptr);
            electrical->set("thld2", 0.5);
            electrical->set("tpord", 0.0);
        }
        loadOtherMachines(*simulation);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        if (std::string_view{electricalControl} == "REECA1") {
            EXPECT_GT(simulation->rootSize(cDaeSolverMode), 0U);
        }
        EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
        ASSERT_EQ(simulation->run(0.1), 0);
    }
}

TEST(RenewableModels, TwoBusRenewableFaultMatchesAndesReference)
{
    for (int profile = 0; profile < 4; ++profile) {
        const bool includePlant = profile == 1;
        const bool includeWind = profile == 2;
        const bool useReecb = profile == 3;
        SCOPED_TRACE(includeWind      ? "wind assembly" :
                         includePlant ? "REPCA1" :
                         useReecb     ? "REECB1" :
                                        "REGCA1+REECA1");
        const auto xml = std::filesystem::path(__FILE__).parent_path().parent_path() / "reference" /
            "renewable_fault" / "two_bus.xml";
        auto simulation = std::make_unique<GridDynSimulation>();
        loadFile(simulation.get(), xml.string());
        auto* bus = dynamic_cast<GridBus*>(simulation->find("solar_bus"));
        ASSERT_NE(bus, nullptr);
        auto* host = new RenewableGenerator("solar");
        host->set("p", 0.6);
        host->set("mbase", 100.0, units::MVAR);
        auto* reg = new REGCA1;
        reg->set("tg", 0.02);
        reg->set("tfltr", 0.02);
        reg->set("iqrmax", 999.0);
        reg->set("iqrmin", -999.0);
        reg->set("lvplsw", 0.0);
        auto* ree = useReecb ? static_cast<REECA1*>(new REECB1) : new REECA1;
        ree->set("vflag", 0.0);
        ree->set("pqflag", 0.0);
        if (!useReecb) {
            ree->set("thld2", 0.5);
        }
        ree->set("tpord", useReecb ? 0.02 : 0.0);
        ree->set("imax", 1.3);
        ree->set("qmax", 1.0);
        ree->set("qmin", -1.0);
        if (!useReecb) {
            for (int k = 1; k <= 4; ++k) {
                auto suffix = std::to_string(k);
                ree->set("iq" + suffix, 1.3);
                ree->set("ip" + suffix, 1.3);
            }
        }
        host->add(reg);
        host->add(ree);
        if (includePlant) {
            auto* plant = new REPCA1;
            plant->set("vcflag", 0.0);
            plant->set("refflag", 1.0);
            plant->set("fflag", 0.0);
            plant->set("plflag", 0.0);
            host->add(plant);
        }
        WTDTA1* shaft = nullptr;
        WTARA1* aerodynamic = nullptr;
        WTPTA1* pitch = nullptr;
        if (includeWind) {
            shaft = new WTDTA1;
            shaft->set("h", 6.0);
            aerodynamic = new WTARA1;
            pitch = new WTPTA1;
            host->add(shaft);
            host->add(aerodynamic);
            host->add(pitch);
            host->add(new WTTQA1);
        }
        bus->add(host);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        std::string referenceName = "andes_reference.csv";
        if (includePlant) {
            referenceName = "andes_plant_reference.csv";
        }
        if (includeWind) {
            referenceName = "andes_wind_reference.csv";
        }
        if (useReecb) {
            referenceName = "andes_reecb_reference.csv";
        }
        const auto reference = xml.parent_path() / referenceName;
        std::ifstream stream(reference);
        ASSERT_TRUE(stream.is_open());
        std::string line;
        ASSERT_TRUE(static_cast<bool>(std::getline(stream, line)));
        const std::string header =
            "time,voltage,converter_p,converter_q,electrical_ip,electrical_iq";
        EXPECT_EQ(line,
                  includeWind ?
                      header + ",generator_speed,turbine_speed,pitch_angle,mechanical_power" :
                      header);
        int samples = 0;
        while (std::getline(stream, line)) {
            if (line.empty()) {
                continue;
            }
            std::istringstream row(line);
            std::vector<double> expected(includeWind ? 10 : 6);
            for (auto& value : expected) {
                std::string field;
                ASSERT_TRUE(static_cast<bool>(std::getline(row, field, ',')));
                value = std::stod(field);
            }
            const double timeValue = expected[0];
            ASSERT_EQ(simulation->run(timeValue), 0) << timeValue;
            EXPECT_NEAR(bus->getVoltage(), expected[1], 0.012) << timeValue;
            EXPECT_NEAR(reg->getStates()[0], expected[2], 0.012) << timeValue;
            EXPECT_NEAR(reg->getStates()[1], expected[3], 0.012) << timeValue;
            EXPECT_NEAR(ree->getStates()[0], expected[4], 0.012) << timeValue;
            EXPECT_NEAR(ree->getStates()[1], expected[5], 0.012) << timeValue;
            if (includeWind) {
                EXPECT_NEAR(shaft->getStates()[0], expected[6], 0.001) << timeValue;
                EXPECT_NEAR(shaft->getStates()[1], expected[7], 0.001) << timeValue;
                EXPECT_NEAR(pitch->getStates()[0], expected[8], 0.0001) << timeValue;
                EXPECT_NEAR(aerodynamic->getStates()[0], expected[9], 0.002) << timeValue;
            }
            ++samples;
        }
        EXPECT_EQ(samples, 8);
    }
}
