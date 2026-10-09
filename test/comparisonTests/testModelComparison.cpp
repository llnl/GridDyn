/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridComponentHelperClasses.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/GridSubModel.h"
#include "griddyn/events/Event.h"
#include "griddyn/exciters/ExciterAC7B.h"
#include "griddyn/exciters/ExciterAC8B.h"
#include "griddyn/exciters/ExciterDC1A.h"
#include "griddyn/exciters/ExciterDC2A.h"
#include "griddyn/exciters/ExciterESAC1A.h"
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
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModelCSVGN1.h"
#include "griddyn/genmodels/GenModelClassical.h"
#include "griddyn/genmodels/GenModelGENROE.h"
#include "griddyn/genmodels/GenModelGENROU.h"
#include "griddyn/genmodels/GenModelGENSAE.h"
#include "griddyn/genmodels/GenModelGENSAL.h"
#include "griddyn/genmodels/GenModelGENTPJ.h"
#include "griddyn/governors/GovernorGast.h"
#include "griddyn/governors/GovernorGgov1.h"
#include "griddyn/governors/GovernorHygov.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/governors/GovernorReheat.h"
#include "griddyn/governors/GovernorTgov1.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/CompositeLoad.h"
#include "griddyn/loads/ElectronicLoad.h"
#include "griddyn/loads/IEELLoad.h"
#include "griddyn/loads/MotorDLoad.h"
#include "griddyn/loads/WECCMotor3.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/stabilizers/StabilizerIEEEST.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
constexpr std::string_view comparisonTestDirectory{GRIDDYN_TEST_DIRECTORY "/comparison_tests/"};

std::string makeComparisonTestPath(std::string_view fileName)
{
    return std::string{comparisonTestDirectory} + std::string{fileName};
}

std::unique_ptr<griddyn::GridDynSimulation>
    loadComparisonDynamicCase(std::string_view machineDyrFile,
                              const std::vector<std::string_view>& controllerDyrFiles)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    if (!machineDyrFile.empty()) {
        griddyn::loadFile(simulation.get(), makeComparisonTestPath(machineDyrFile));
    }
    for (const auto dyrFile : controllerDyrFiles) {
        griddyn::loadFile(simulation.get(), makeComparisonTestPath(dyrFile));
    }
    return simulation;
}

std::vector<double>
    runGeneratorSetpointStepCase(const std::vector<std::string_view>& dyrFiles,
                                 double setpoint = 0.8,
                                 std::string_view machineDyrFile = "ieee14_genrou.dyr",
                                 index_t targetBusId = 1,
                                 double minimumControllerChange = 1.0e-9)
{
    auto simulation = loadComparisonDynamicCase(machineDyrFile, dyrFiles);

    auto* targetBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", targetBusId));
    EXPECT_NE(targetBus, nullptr);
    if (targetBus == nullptr) {
        return {};
    }
    auto* targetGenerator = dynamic_cast<griddyn::DynamicGenerator*>(targetBus->getGen(0));
    EXPECT_NE(targetGenerator, nullptr);
    if (targetGenerator == nullptr) {
        return {};
    }
    auto event = std::make_shared<griddyn::Event>(0.5);
    EXPECT_TRUE(event->setTarget(targetGenerator, "pset"));
    if (!event->isArmed()) {
        return {};
    }
    event->setValue(setpoint);
    simulation->add(event);

    EXPECT_EQ(simulation->dynInitialize(), 0);
    const auto initialState = simulation->getState();
    EXPECT_FALSE(initialState.empty());
    std::vector<std::pair<griddyn::GridSubModel*, std::vector<double>>> controllerStates;
    for (const auto busId : {1, 2, 3, 6, 8}) {
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", busId));
        if (bus == nullptr) {
            continue;
        }
        auto* generator = bus->getGen(0);
        if (generator == nullptr) {
            continue;
        }
        for (const auto controllerName : {"governor", "exciter", "pss"}) {
            auto* controller =
                dynamic_cast<griddyn::GridSubModel*>(generator->find(controllerName));
            if (controller != nullptr) {
                controllerStates.emplace_back(controller, controller->getStates());
            }
        }
    }
    EXPECT_FALSE(controllerStates.empty());
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    simulation->run(2.0);
    EXPECT_EQ(simulation->getSimulationTime(), 2.0);
    EXPECT_FALSE(event->isArmed());

    const auto finalState = simulation->getState();
    EXPECT_EQ(finalState.size(), initialState.size());
    double maximumChange = 0.0;
    for (std::size_t index = 0; index < finalState.size(); ++index) {
        EXPECT_TRUE(std::isfinite(finalState[index]));
        maximumChange =
            (std::max)(maximumChange, std::abs(finalState[index] - initialState[index]));
    }
    EXPECT_GT(maximumChange, 1.0e-7);
    double maximumControllerChange = 0.0;
    for (const auto& [controller, initialControllerState] : controllerStates) {
        const auto& finalControllerState = controller->getStates();
        EXPECT_EQ(finalControllerState.size(), initialControllerState.size());
        if (finalControllerState.size() != initialControllerState.size()) {
            continue;
        }
        for (std::size_t index = 0; index < finalControllerState.size(); ++index) {
            EXPECT_TRUE(std::isfinite(finalControllerState[index]));
            maximumControllerChange =
                (std::max)(maximumControllerChange,
                           std::abs(finalControllerState[index] - initialControllerState[index]));
        }
    }
    EXPECT_GT(maximumControllerChange, minimumControllerChange);
    return finalState;
}
}  // namespace

TEST(DyrReaderComparisonTests, LoadsGenrouAndMatchesIeee14Initialization)
{
    std::ifstream input(makeComparisonTestPath("ieee14_genrou_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));

    const auto tolerance = reference["tolerance"].get<double>();
    for (std::size_t index = 0; index < reference["generator_bus_ids"].size(); ++index) {
        const auto busId = reference["generator_bus_ids"][index].get<index_t>();
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", busId));
        ASSERT_NE(bus, nullptr) << "bus " << busId;
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr) << "generator at bus " << busId;
        auto* model =
            dynamic_cast<griddyn::genmodels::GenModelGENROU*>(generator->find("genmodel"));
        ASSERT_NE(model, nullptr) << "GENROU at bus " << busId;

        model->dynInitializeA(0.0, 0);
        griddyn::IOdata inputs(4, 0.0);
        inputs[griddyn::VOLTAGE_IN_LOCATION] = reference["terminal_voltage"][index].get<double>();
        inputs[griddyn::ANGLE_IN_LOCATION] = reference["terminal_angle"][index].get<double>();
        griddyn::IOdata desiredOutput(2, 0.0);
        desiredOutput[griddyn::POUT_LOCATION] =
            reference["terminal_real_power"][index].get<double>();
        desiredOutput[griddyn::QOUT_LOCATION] =
            reference["terminal_reactive_power"][index].get<double>();
        griddyn::IOdata fieldSet(4, 0.0);
        model->dynInitializeB(inputs, desiredOutput, fieldSet);

        const auto& states = model->getStates();
        const auto& expectedStates = reference["genrou_state"][index];
        ASSERT_EQ(states.size(), expectedStates.size()) << "GENROU at bus " << busId;
        for (std::size_t stateIndex = 0; stateIndex < states.size(); ++stateIndex) {
            EXPECT_NEAR(states[stateIndex], expectedStates[stateIndex].get<double>(), tolerance)
                << "GENROU at bus " << busId << ", state "
                << reference["grid_dyn_state_order"][stateIndex].get<std::string>();
        }
        EXPECT_NEAR(fieldSet[griddyn::genModelEftInLocation],
                    reference["field_voltage"][index].get<double>(),
                    tolerance)
            << "GENROU field voltage at bus " << busId;
        EXPECT_NEAR(fieldSet[griddyn::genModelPmechInLocation],
                    reference["mechanical_power"][index].get<double>(),
                    tolerance)
            << "GENROU mechanical power at bus " << busId;
    }
}

TEST(DyrReaderComparisonTests, LoadsGentpjAndInitializesIeee14)
{
    auto simulation = loadComparisonDynamicCase("ieee14_gentpj.dyr", {});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* model = dynamic_cast<griddyn::genmodels::GenModelGENTPJ*>(generator->find("genmodel"));
    ASSERT_NE(model, nullptr);
    EXPECT_DOUBLE_EQ(model->get("kis"), 0.03);
    EXPECT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    simulation->run(0.1);
    EXPECT_EQ(simulation->getSimulationTime(), 0.1);
}

TEST(DyrReaderComparisonTests, MapsCsvgn1ParametersAndRunsAsMachineModel)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 14));
    ASSERT_NE(bus, nullptr);

    // Use a zero-real-power machine record like the Australian SVC records,
    // while leaving the IEEE 14 generator at the slack bus untouched.
    auto svcGenerator = std::make_unique<griddyn::DynamicGenerator>(bus->getName() + "_Gen_2");
    svcGenerator->set("q", 20.0, units::MVAR);
    bus->add(svcGenerator.get());
    auto* svcGeneratorPtr = svcGenerator.release();
    ASSERT_NE(svcGeneratorPtr, nullptr);
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_gentpj.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_csvgn1.dyr"));

    auto* svcBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 14));
    ASSERT_NE(svcBus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(svcBus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* model = dynamic_cast<griddyn::genmodels::GenModelCSVGN1*>(generator->find("genmodel"));
    ASSERT_NE(model, nullptr);

    EXPECT_DOUBLE_EQ(model->get("k"), 23.5);
    EXPECT_DOUBLE_EQ(model->get("t1"), 0.02);
    EXPECT_DOUBLE_EQ(model->get("t2"), 0.04);
    EXPECT_DOUBLE_EQ(model->get("t3"), 0.07);
    EXPECT_DOUBLE_EQ(model->get("t4"), 0.09);
    EXPECT_DOUBLE_EQ(model->get("t5"), 0.03);
    EXPECT_DOUBLE_EQ(model->get("rmin"), 1.5);
    EXPECT_DOUBLE_EQ(model->get("vmax"), 0.98);
    EXPECT_DOUBLE_EQ(model->get("vmin"), 0.02);
    EXPECT_DOUBLE_EQ(model->get("cbase"), 80.0);

    EXPECT_EQ(simulation->dynInitialize(), 0);
    EXPECT_NEAR(model->getOutput(griddyn::QOUT_LOCATION), -0.20, 1.0e-8);
    EXPECT_EQ(model->getStates().size(), 3U);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);

    // Check both one-sided branches of the regulator output clamp. The lower-limit
    // Jacobian uses the interior slope; the upper-limit Jacobian uses the saturated slope.
    const auto states = model->getStates();
    const double voltage = svcBus->getVoltage();
    const double error = voltage - (voltage - states[0]);
    const double firstOutput = ((model->get("t1") / model->get("t3")) * error) +
        ((1.0 - (model->get("t1") / model->get("t3"))) * states[0]);
    const double secondOutput = ((model->get("t2") / model->get("t4")) * firstOutput) +
        ((1.0 - (model->get("t2") / model->get("t4"))) * states[1]);
    const double regulatorOutput = model->get("k") * secondOutput;
    ASSERT_GT(regulatorOutput, model->get("vmin"));
    ASSERT_LT(regulatorOutput, model->get("vmax"));

    model->set("vmin", regulatorOutput);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0)
        << "CSVGN1 Jacobian at the lower regulator limit";

    model->set("vmin", 0.02);
    model->set("vmax", regulatorOutput);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0)
        << "CSVGN1 Jacobian at the upper regulator limit";

    simulation->run(0.1);
    EXPECT_EQ(simulation->getSimulationTime(), 0.1);
}

TEST(DyrReaderComparisonTests, CouplesGentpjToIeeet2Exciter)
{
    auto simulation = loadComparisonDynamicCase("ieee14_gentpj.dyr", {"ieee14_ieeet2.dyr"});
    EXPECT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    simulation->run(0.1);
    EXPECT_EQ(simulation->getSimulationTime(), 0.1);
}

TEST(DyrReaderComparisonTests, SkipsDyrHeaderCommentsBeforeRecordAccumulation)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou_with_comments.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* model = dynamic_cast<griddyn::genmodels::GenModelGENROU*>(generator->find("genmodel"));
    ASSERT_NE(model, nullptr);
}

TEST(DyrReaderComparisonTests, LoadsGenclsInPsseAndesFieldOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_gencls.dyr"));

    struct GenclsParameters {
        index_t busId;
        double h;
        double d;
    };
    constexpr GenclsParameters expected[]{{.busId = 1, .h = 2.8756, .d = 1.0},
                                          {.busId = 2, .h = 3.0, .d = 2.0},
                                          {.busId = 3, .h = 4.0, .d = 0.0},
                                          {.busId = 6, .h = 5.0, .d = 0.5},
                                          {.busId = 8, .h = 0.0, .d = 0.0}};

    for (const auto& entry : expected) {
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", entry.busId));
        ASSERT_NE(bus, nullptr) << "bus " << entry.busId;
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr) << "generator at bus " << entry.busId;
        auto* model =
            dynamic_cast<griddyn::genmodels::GenModelClassical*>(generator->find("genmodel"));
        ASSERT_NE(model, nullptr) << "GENCLS at bus " << entry.busId;
        EXPECT_DOUBLE_EQ(model->get("h"), entry.h);
        EXPECT_DOUBLE_EQ(model->get("m"), 2.0 * entry.h);
        EXPECT_DOUBLE_EQ(model->get("d"), entry.d);
    }

    auto* bus1 = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus1, nullptr);
    auto* model =
        dynamic_cast<griddyn::genmodels::GenModelClassical*>(bus1->getGen(0)->find("genmodel"));
    ASSERT_NE(model, nullptr);
    // GENCLS obtains R_a and x'd from the PSS/E RAW ZSOURCE fields.
    EXPECT_DOUBLE_EQ(model->get("r"), 0.0);
    EXPECT_DOUBLE_EQ(model->get("x"), 0.23);
}

TEST(DyrReaderComparisonTests, LoadsAndesKundurGenclsCase)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("kundur_vsc_pflow.json"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("kundur_gencls.dyr"));

    constexpr std::array<double, 4> expectedInertia{13.0, 13.0, 12.35, 12.35};
    for (index_t index = 0; std::cmp_less(index, expectedInertia.size()); ++index) {
        const auto busId = index + 1;
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", busId));
        ASSERT_NE(bus, nullptr) << "bus " << busId;
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr) << "generator at bus " << busId;
        auto* model =
            dynamic_cast<griddyn::genmodels::GenModelClassical*>(generator->find("genmodel"));
        ASSERT_NE(model, nullptr) << "GENCLS at bus " << busId;
        EXPECT_DOUBLE_EQ(model->get("h"), expectedInertia[index]);
        EXPECT_DOUBLE_EQ(model->get("d"), 0.0);
    }
}

TEST(DyrReaderComparisonTests, MapsGensalParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_gensal.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* model = dynamic_cast<griddyn::genmodels::GenModelGENSAL*>(generator->find("genmodel"));
    ASSERT_NE(model, nullptr);
    const std::pair<std::string_view, double> expected[]{{"tdop", 5.1},
                                                         {"tdopp", 0.052},
                                                         {"tqopp", 0.103},
                                                         {"h", 4.4},
                                                         {"d", 0.04},
                                                         {"xd", 1.41},
                                                         {"xq", 1.35},
                                                         {"xdp", 0.30},
                                                         {"xdpp", 0.20},
                                                         {"xqpp", 0.20},
                                                         {"xl", 0.12},
                                                         {"s10", 0.10},
                                                         {"s12", 0.50}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(model->get(name), value) << name;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsGenroeAndIeeex1ParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genroe_ieeex1.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* machine = dynamic_cast<griddyn::genmodels::GenModelGENROE*>(generator->find("genmodel"));
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterIEEEX1*>(generator->find("exciter"));
    ASSERT_NE(machine, nullptr);
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expectedMachine[]{{"tdop", 6.0},
                                                                {"tdopp", 0.03},
                                                                {"tqop", 0.50},
                                                                {"tqopp", 0.05},
                                                                {"h", 4.0},
                                                                {"d", 0.0},
                                                                {"xd", 1.80},
                                                                {"xq", 1.70},
                                                                {"xdp", 0.30},
                                                                {"xqp", 0.55},
                                                                {"xdpp", 0.25},
                                                                {"xqpp", 0.25},
                                                                {"xl", 0.06},
                                                                {"s10", 0.10},
                                                                {"s12", 0.50}};
    for (const auto& [name, value] : expectedMachine) {
        EXPECT_DOUBLE_EQ(machine->get(name), value) << name;
    }
    const std::pair<std::string_view, double> expectedExciter[]{{"tr", 0.02},
                                                                {"ka", 50.0},
                                                                {"ta", 0.05},
                                                                {"tb", 0.0},
                                                                {"tc", 0.0},
                                                                {"vrmax", 5.0},
                                                                {"vrmin", -5.0},
                                                                {"ke", 1.0},
                                                                {"te", 0.50},
                                                                {"kf", 0.05},
                                                                {"tf", 1.0},
                                                                {"switch", 0.0},
                                                                {"e1", 2.0},
                                                                {"se1", 0.03},
                                                                {"e2", 5.0},
                                                                {"se2", 0.50}};
    for (const auto& [name, value] : expectedExciter) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }
}

TEST(DyrReaderComparisonTests, InitializesGenroeAndIeeex1WithConsistentEquations)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genroe_ieeex1.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* machine = dynamic_cast<griddyn::genmodels::GenModelGENROE*>(generator->find("genmodel"));
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterIEEEX1*>(generator->find("exciter"));
    ASSERT_NE(machine, nullptr);
    ASSERT_NE(exciter, nullptr);
    EXPECT_EQ(exciter->localStateNames(), (griddyn::stringVec{"ef", "vr", "rf", "vmeas"}));
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, RejectsMalformedGenroeAndUnsupportedIeeex1Switch)
{
    for (const auto record : {"ieee14_genroe_bad_fields.dyr", "ieee14_ieeex1_nonzero_switch.dyr"}) {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        EXPECT_THROW(griddyn::loadFile(simulation.get(), makeComparisonTestPath(record)),
                     griddyn::InvalidParameterValue)
            << record;
    }
}

TEST(DyrReaderComparisonTests, MapsCmpldwMotorComponentsAndChecksCompositeDynamics)
{
    auto simulation = loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_cmpldw_motors.dyr"});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    ASSERT_EQ(composite->componentCount(), 3U);

    auto* motor3 = dynamic_cast<griddyn::loads::WECCMotor3*>(composite->component(0));
    auto* motorD = dynamic_cast<griddyn::loads::MotorDLoad*>(composite->component(1));
    ASSERT_NE(motor3, nullptr);
    ASSERT_NE(motorD, nullptr);
    EXPECT_NE(composite->component(2), nullptr);
    EXPECT_DOUBLE_EQ(composite->get("fraction1"), 0.45);
    EXPECT_DOUBLE_EQ(composite->get("fraction2"), 0.35);
    EXPECT_DOUBLE_EQ(composite->get("fraction3"), 0.20);
    EXPECT_DOUBLE_EQ(motor3->get("r"), 0.03);
    EXPECT_DOUBLE_EQ(motor3->get("ls"), 2.5);
    EXPECT_DOUBLE_EQ(motor3->get("etrq"), 2.0);
    EXPECT_DOUBLE_EQ(motor3->get("ftr1"), 0.25);
    EXPECT_DOUBLE_EQ(motorD->get("lfm"), 0.85);
    EXPECT_DOUBLE_EQ(motorD->get("tv"), 0.025);
    EXPECT_DOUBLE_EQ(motorD->get("tstall"), 0.033);
    EXPECT_TRUE(motorD->checkFlag(griddyn::USES_BUS_FREQUENCY));

    // The CMPLDW motor data can change the reactive characteristic from the
    // original static load, so settle the imported composite case before DAE
    // initialization, as for any changed power-flow load model.
    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_TRUE(bus->isConnected());
    EXPECT_NEAR(motor3->get("pmot"), motor3->get("p"), 1e-12);
    EXPECT_NEAR(motor3->get("scale"), motor3->get("p") / motor3->get("lfm"), 1e-12);
    EXPECT_NEAR(motor3->get("base"),
                100.0 * composite->get("p") * composite->get("fraction1") / motor3->get("lfm"),
                1e-10);
    EXPECT_GT(motor3->getRealPower(), 0.0);
    EXPECT_GT(motor3->getReactivePower(), 0.0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, InitializesSingleCageCmpldwMotorWithZeroTppo)
{
    auto simulation =
        loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_cmpldw_single_cage.dyr"});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    auto* motor = dynamic_cast<griddyn::loads::WECCMotor3*>(composite->component(0));
    ASSERT_NE(motor, nullptr);
    EXPECT_DOUBLE_EQ(motor->get("tppo"), 0.0);
    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_TRUE(bus->isConnected());
    EXPECT_NEAR(motor->get("pmot"), motor->get("p"), 1e-12);
    EXPECT_NEAR(motor->get("scale"), motor->get("p") / motor->get("lfm"), 1e-12);
    EXPECT_GT(motor->getRealPower(), 0.0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.05), 0);
}

TEST(DyrReaderComparisonTests, SingleCageTppoAndEqualReactanceFormsAgree)
{
    auto zeroTppo =
        loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_cmpldw_single_cage.dyr"});
    auto equalReactance =
        loadComparisonDynamicCase("ieee14_genrou.dyr",
                                  {"ieee14_cmpldw_single_cage_equal_reactance.dyr"});
    auto getMotor = [](griddyn::GridDynSimulation* simulation) {
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
        auto* composite =
            bus ? dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0)) : nullptr;
        return composite ? dynamic_cast<griddyn::loads::WECCMotor3*>(composite->component(0)) :
                           nullptr;
    };
    auto* zeroMotor = getMotor(zeroTppo.get());
    auto* equalMotor = getMotor(equalReactance.get());
    ASSERT_NE(zeroMotor, nullptr);
    ASSERT_NE(equalMotor, nullptr);
    ASSERT_EQ(zeroTppo->powerflow(), 0);
    ASSERT_EQ(equalReactance->powerflow(), 0);
    EXPECT_GT(zeroMotor->getRealPower(), 0.0);
    EXPECT_GT(zeroMotor->getReactivePower(), 0.0);
    EXPECT_NEAR(zeroMotor->getRealPower(), equalMotor->getRealPower(), 1e-8);
    EXPECT_NEAR(zeroMotor->getReactivePower(), equalMotor->getReactivePower(), 1e-8);
    EXPECT_NEAR(zeroMotor->rotorSpeed(), equalMotor->rotorSpeed(), 1e-8);
    ASSERT_EQ(zeroTppo->dynInitialize(), 0);
    ASSERT_EQ(equalReactance->dynInitialize(), 0);
    ASSERT_EQ(zeroTppo->run(0.05), 0);
    ASSERT_EQ(equalReactance->run(0.05), 0);
    EXPECT_NEAR(zeroMotor->getRealPower(), equalMotor->getRealPower(), 1e-6);
    EXPECT_NEAR(zeroMotor->getReactivePower(), equalMotor->getReactivePower(), 1e-6);
}

TEST(DyrReaderComparisonTests, MapsCmpldwElectronicLoadAndChecksOptionalFrequencyJacobian)
{
    auto simulation =
        loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_cmpldw_electronic.dyr"});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    ASSERT_EQ(composite->componentCount(), 2U);
    auto* electronic = dynamic_cast<griddyn::loads::ElectronicLoad*>(composite->component(0));
    ASSERT_NE(electronic, nullptr);
    EXPECT_DOUBLE_EQ(composite->get("fraction1"), 0.1);
    EXPECT_DOUBLE_EQ(composite->get("fraction2"), 0.9);
    EXPECT_DOUBLE_EQ(electronic->get("pfel"), 0.98);
    EXPECT_DOUBLE_EQ(electronic->get("vd1"), 0.9);
    EXPECT_DOUBLE_EQ(electronic->get("vd2"), 0.7);
    EXPECT_DOUBLE_EQ(electronic->get("frcel"), 0.8);
    EXPECT_DOUBLE_EQ(electronic->get("a3"), 1.0);
    EXPECT_DOUBLE_EQ(electronic->get("a6"), 1.0);
    EXPECT_DOUBLE_EQ(electronic->get("pfrq"), 0.0);
    EXPECT_DOUBLE_EQ(electronic->get("qfrq"), 0.0);
    EXPECT_FALSE(electronic->checkFlag(griddyn::USES_BUS_FREQUENCY));

    // The generic extensions are separate from CMLDBLU1 input and can be enabled
    // for other electronic-load studies without changing the CMPLDW defaults.
    // Activate them before power flow so the frequency-dependent operating point
    // and dynamic initialization are consistent.
    electronic->set("a1", 0.4);
    electronic->set("a3", 0.6);
    electronic->set("n1", 2.0);
    electronic->set("a4", 0.25);
    electronic->set("a6", 0.75);
    electronic->set("n4", 1.0);
    electronic->set("pfrq", 0.15);
    electronic->set("qfrq", -0.25);
    EXPECT_TRUE(electronic->checkFlag(griddyn::USES_BUS_FREQUENCY));

    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.05), 0);
}

TEST(DyrReaderComparisonTests, NormalizesOverallocatedCmpldwMotorAndElectronicFractions)
{
    auto simulation =
        loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_cmpldw_overallocated.dyr"});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    ASSERT_EQ(composite->componentCount(), 2U);
    EXPECT_NE(dynamic_cast<griddyn::loads::MotorDLoad*>(composite->component(0)), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::loads::ElectronicLoad*>(composite->component(1)), nullptr);
    EXPECT_NEAR(composite->get("fraction1"), 5.0 / 12.0, 1e-12);
    EXPECT_NEAR(composite->get("fraction2"), 7.0 / 12.0, 1e-12);
    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsCmpldwStaticPolynomialToIeelAndChecksFrequencyJacobian)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    const double referenceVoltage = bus->getVoltage();
    const double initialP = bus->getLoad(0)->getRealPower();
    const double initialQ = bus->getLoad(0)->getReactivePower();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(),
                      makeComparisonTestPath("ieee14_cmpldw_static_polynomial.dyr"));
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    ASSERT_EQ(composite->componentCount(), 3U);
    auto* staticLoad = dynamic_cast<griddyn::loads::IEELLoad*>(composite->component(2));
    ASSERT_NE(staticLoad, nullptr);
    EXPECT_TRUE(staticLoad->checkFlag(griddyn::USES_BUS_FREQUENCY));
    const double staticFraction = composite->get("fraction3");
    EXPECT_DOUBLE_EQ(staticFraction, 0.20);

    constexpr double p1c = 0.3;
    constexpr double p1e = 2.0;
    constexpr double p2c = 0.7;
    constexpr double p2e = 1.0;
    constexpr double pfrq = 0.2;
    constexpr double q1c = -0.5;
    constexpr double q1e = 2.0;
    constexpr double q2c = 1.5;
    constexpr double q2e = 1.0;
    constexpr double qfrq = -1.0;
    constexpr double staticPF = 0.97878;
    const double pCurve = (p1c * std::pow(referenceVoltage, p1e)) +
        (p2c * std::pow(referenceVoltage, p2e)) + (1.0 - p1c - p2c);
    const double qCurve = (q1c * std::pow(referenceVoltage, q1e)) +
        (q2c * std::pow(referenceVoltage, q2e)) + (1.0 - q1c - q2c);
    const double staticP0 = (initialP * staticFraction) / pCurve;
    const double staticQ0 = staticP0 * std::tan(std::acos(staticPF));
    const double staticQBase = initialQ * staticFraction;
    const double qCoefficientScale = staticQ0 / staticQBase;
    EXPECT_NEAR(staticLoad->get("a1"), p1c / pCurve, 1.0e-12);
    EXPECT_NEAR(staticLoad->get("a2"), p2c / pCurve, 1.0e-12);
    EXPECT_NEAR(staticLoad->get("a3"), (1.0 - p1c - p2c) / pCurve, 1.0e-12);
    EXPECT_NEAR(staticLoad->get("a4"), q1c * qCoefficientScale, 1.0e-12);
    EXPECT_NEAR(staticLoad->get("a5"), q2c * qCoefficientScale, 1.0e-12);
    EXPECT_NEAR(staticLoad->get("a6"), (1.0 - q1c - q2c) * qCoefficientScale, 1.0e-12);
    EXPECT_DOUBLE_EQ(staticLoad->get("a7"), pfrq);
    EXPECT_DOUBLE_EQ(staticLoad->get("a8"), qfrq);
    EXPECT_DOUBLE_EQ(staticLoad->get("n1"), p1e);
    EXPECT_DOUBLE_EQ(staticLoad->get("n2"), p2e);
    EXPECT_DOUBLE_EQ(staticLoad->get("n3"), 0.0);
    EXPECT_DOUBLE_EQ(staticLoad->get("n4"), q1e);
    EXPECT_DOUBLE_EQ(staticLoad->get("n5"), q2e);
    EXPECT_DOUBLE_EQ(staticLoad->get("n6"), 0.0);

    ASSERT_EQ(simulation->powerflow(), 0);
    const double initialFrequency = bus->getFreq();
    EXPECT_NEAR(staticLoad->getRealPower(referenceVoltage),
                staticP0 * pCurve * (1.0 + (pfrq * (initialFrequency - 1.0))),
                1.0e-8);
    EXPECT_NEAR(staticLoad->getReactivePower(referenceVoltage),
                staticQ0 * qCurve * (1.0 + (qfrq * (initialFrequency - 1.0))),
                1.0e-8);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsStaticPowerFactorWhenOriginalLoadHasZeroReactivePower)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* original = bus->getLoad(0);
    ASSERT_NE(original, nullptr);
    const double referenceVoltage = bus->getVoltage();
    const double originalP = original->getRealPower();
    original->set("q", 0.0);
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    ASSERT_NO_THROW(
        griddyn::loadFile(simulation.get(),
                          makeComparisonTestPath("ieee14_cmpldw_static_polynomial.dyr")));
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    auto* staticLoad = dynamic_cast<griddyn::loads::IEELLoad*>(composite->component(2));
    ASSERT_NE(staticLoad, nullptr);
    ASSERT_EQ(simulation->powerflow(), 0);
    const double pCurve = (0.3 * referenceVoltage * referenceVoltage) + (0.7 * referenceVoltage);
    const double qCurve = (-0.5 * referenceVoltage * referenceVoltage) + (1.5 * referenceVoltage);
    const double staticP0 = (0.2 * originalP) / pCurve;
    const double expectedQ = staticP0 * std::tan(std::acos(0.97878)) * qCurve;
    EXPECT_NEAR(staticLoad->getReactivePower(referenceVoltage), expectedQ, 1e-8);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsCmpldwType1MotorABCAndChecksCompositeDaeJacobian)
{
    auto simulation =
        loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_cmpldw_type1_abc.dyr"});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(bus->getLoad(0));
    ASSERT_NE(composite, nullptr);
    ASSERT_EQ(composite->componentCount(), 5U);

    auto* motorA = dynamic_cast<griddyn::loads::MotorDLoad*>(composite->component(0));
    auto* motorB = dynamic_cast<griddyn::loads::MotorDLoad*>(composite->component(1));
    auto* motorC = dynamic_cast<griddyn::loads::MotorDLoad*>(composite->component(2));
    auto* motorD = dynamic_cast<griddyn::loads::MotorDLoad*>(composite->component(3));
    ASSERT_NE(motorA, nullptr);
    ASSERT_NE(motorB, nullptr);
    ASSERT_NE(motorC, nullptr);
    ASSERT_NE(motorD, nullptr);
    EXPECT_NE(composite->component(4), nullptr);
    EXPECT_DOUBLE_EQ(composite->get("fraction1"), 0.15);
    EXPECT_DOUBLE_EQ(composite->get("fraction2"), 0.15);
    EXPECT_DOUBLE_EQ(composite->get("fraction3"), 0.15);
    EXPECT_DOUBLE_EQ(composite->get("fraction4"), 0.35);
    EXPECT_DOUBLE_EQ(composite->get("fraction5"), 0.20);

    EXPECT_DOUBLE_EQ(motorA->get("lfm"), 0.85);
    EXPECT_DOUBLE_EQ(motorA->get("comppf"), 0.98);
    EXPECT_DOUBLE_EQ(motorA->get("vstall"), 0.65);
    EXPECT_DOUBLE_EQ(motorA->get("frst"), 0.25);
    EXPECT_DOUBLE_EQ(motorA->get("fuvr"), 0.1);
    EXPECT_DOUBLE_EQ(motorA->get("vtr1"), 0.6);
    EXPECT_DOUBLE_EQ(motorA->get("ttr1"), 0.02);
    EXPECT_DOUBLE_EQ(motorA->get("ttr2"), 9999.0);
    EXPECT_DOUBLE_EQ(motorA->get("vc1off"), 0.6);
    EXPECT_DOUBLE_EQ(motorA->get("vc2off"), 0.4);
    EXPECT_DOUBLE_EQ(motorA->get("tth"), 15.0);
    EXPECT_DOUBLE_EQ(motorB->get("lfm"), 0.8);
    EXPECT_DOUBLE_EQ(motorB->get("comppf"), 0.97);
    EXPECT_DOUBLE_EQ(motorB->get("tstall"), 0.05);
    EXPECT_DOUBLE_EQ(motorB->get("frst"), 0.4);
    EXPECT_DOUBLE_EQ(motorB->get("tth"), 10.0);
    EXPECT_DOUBLE_EQ(motorC->get("lfm"), 0.9);
    EXPECT_DOUBLE_EQ(motorC->get("comppf"), 0.99);
    EXPECT_DOUBLE_EQ(motorC->get("vstall"), 0.6);
    EXPECT_DOUBLE_EQ(motorC->get("tth"), 12.0);
    for (const auto* motor : {motorA, motorB, motorC}) {
        EXPECT_DOUBLE_EQ(motor->get("th1t"), 0.7);
        EXPECT_DOUBLE_EQ(motor->get("th2t"), 1.9);
        EXPECT_DOUBLE_EQ(motor->get("tv"), 0.025);
        EXPECT_TRUE(motor->checkFlag(griddyn::USES_BUS_FREQUENCY));
        EXPECT_EQ(motor->localStateNames(),
                  (griddyn::stringVec{"voltage_measurement", "thermal_a", "thermal_b"}));
    }

    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, BuildsCmpldwTransformerFeederAndChecksDaeJacobian)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    auto* originalBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(originalBus, nullptr);
    const double originalP = originalBus->getLoad(0)->getRealPower();
    const double originalQ = originalBus->getLoad(0)->getReactivePower();
    const double originalVoltage = originalBus->getVoltage();
    griddyn::loadFile(simulation.get(),
                      makeComparisonTestPath("ieee14_cmpldw_internal_network.dyr"));

    auto* systemBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(systemBus, nullptr);
    auto* slackBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(slackBus, nullptr);
    EXPECT_EQ(slackBus->getType(), griddyn::GridBus::BusType::SLK);
    EXPECT_EQ(systemBus->getLoad(0), nullptr);
    auto* area = dynamic_cast<griddyn::GridArea*>(systemBus->getParent());
    ASSERT_NE(area, nullptr);
    auto* lowSide = dynamic_cast<griddyn::GridBus*>(area->find("cmpldw_4_1_low_side"));
    auto* loadEnd = dynamic_cast<griddyn::GridBus*>(area->find("cmpldw_4_1_load_end"));
    ASSERT_NE(lowSide, nullptr);
    ASSERT_NE(loadEnd, nullptr);

    auto* transformer = dynamic_cast<griddyn::AcLine*>(area->find("cmpldw_4_1_transformer"));
    auto* feeder = dynamic_cast<griddyn::AcLine*>(area->find("cmpldw_4_1_feeder"));
    ASSERT_NE(transformer, nullptr);
    ASSERT_NE(feeder, nullptr);
    EXPECT_TRUE(transformer->isConnected());
    EXPECT_TRUE(feeder->isConnected());
    EXPECT_NEAR(transformer->get("x"), 0.08 * 100.0 / 59.75, 1.0e-10);
    EXPECT_NEAR(feeder->get("r"), 0.00625 * 100.0 / 59.75, 1.0e-10);
    EXPECT_NEAR(feeder->get("x"), 0.0125 * 100.0 / 59.75, 1.0e-10);
    EXPECT_DOUBLE_EQ(feeder->get("b1"), 0.0);
    EXPECT_GT(feeder->get("b2"), 0.0);
    const double sourceCurrentSquared =
        ((originalP * originalP) + (originalQ * originalQ)) / (originalVoltage * originalVoltage);
    const double expectedCompensation =
        (sourceCurrentSquared * (transformer->get("x") + feeder->get("x"))) -
        ((0.01 * 100.0 / 59.75) * std::pow(lowSide->getVoltage(), 2.0));
    EXPECT_NEAR(feeder->get("b2"), expectedCompensation, 1e-10);

    auto* substationShunt = dynamic_cast<griddyn::ZipLoad*>(lowSide->getLoad(0));
    ASSERT_NE(substationShunt, nullptr);
    EXPECT_LT(substationShunt->get("yq"), 0.0);
    auto* composite = dynamic_cast<griddyn::loads::CompositeLoad*>(loadEnd->getLoad(0));
    ASSERT_NE(composite, nullptr);

    ASSERT_EQ(simulation->powerflow(), 0);
    EXPECT_TRUE(systemBus->isConnected());
    EXPECT_TRUE(lowSide->isConnected());
    EXPECT_TRUE(loadEnd->isConnected());
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, OmitsCmpldwFeederWhenFeederReactanceIsZero)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_cmpldw_no_feeder.dyr"));

    auto* systemBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(systemBus, nullptr);
    auto* area = dynamic_cast<griddyn::GridArea*>(systemBus->getParent());
    ASSERT_NE(area, nullptr);
    auto* lowSide = dynamic_cast<griddyn::GridBus*>(area->find("cmpldw_4_1_low_side"));
    ASSERT_NE(lowSide, nullptr);
    EXPECT_EQ(area->find("cmpldw_4_1_feeder"), nullptr);
    EXPECT_EQ(area->find("cmpldw_4_1_load_end"), nullptr);
    auto* feederCompensation = dynamic_cast<griddyn::ZipLoad*>(lowSide->getLoad(1));
    ASSERT_NE(feederCompensation, nullptr);
    EXPECT_GT(std::abs(feederCompensation->get("yq")), 1.0e-12);
    EXPECT_NE(dynamic_cast<griddyn::loads::CompositeLoad*>(lowSide->getLoad(2)), nullptr);

    ASSERT_EQ(simulation->powerflow(), 0);
    EXPECT_TRUE(systemBus->isConnected());
    EXPECT_TRUE(lowSide->isConnected());
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, RejectsMalformedCmpldwRecords)
{
    for (const auto record : {"ieee14_cmpldw_bad_count.dyr"}) {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        EXPECT_THROW(griddyn::loadFile(simulation.get(), makeComparisonTestPath(record)),
                     griddyn::InvalidParameterValue)
            << record;
    }
}

TEST(DyrReaderComparisonTests, RejectsDynamicCmpldwLtcUntilDelayedTapControlIsAvailable)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    EXPECT_THROW(griddyn::loadFile(simulation.get(),
                                   makeComparisonTestPath("ieee14_cmpldw_dynamic_ltc.dyr")),
                 griddyn::InvalidParameterValue);
}

TEST(DyrReaderComparisonTests, InitializesCmpldwLtcInPowerFlowOnlyMode)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_cmpldw_init_ltc.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 4));
    ASSERT_NE(bus, nullptr);
    auto* area = dynamic_cast<griddyn::GridArea*>(bus->getParent());
    ASSERT_NE(area, nullptr);
    auto* transformer =
        dynamic_cast<griddyn::links::AdjustableTransformer*>(area->find("cmpldw_4_1_transformer"));
    ASSERT_NE(transformer, nullptr);

    ASSERT_EQ(simulation->powerflow(), 0);
    EXPECT_TRUE(bus->isConnected());
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, RejectsUnsupportedModelWithRecordLocation)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    try {
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_unsupported_model.dyr"));
        FAIL() << "unsupported DYR model was accepted";
    }
    catch (const griddyn::InvalidParameterValue& error) {
        const std::string message{error.what()};
        EXPECT_NE(message.find("unsupported DYR models:"), std::string::npos);
        EXPECT_NE(message.find("NOTAMODEL: 2 record(s)"), std::string::npos);
        EXPECT_NE(message.find("ANOTHER: 1 record(s)"), std::string::npos);
        EXPECT_NE(message.find("first at line 1, bus 1 machine '1'"), std::string::npos);
    }
}

TEST(DyrReaderComparisonTests, MapsGensaeAndEsst1aParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_gensae_esst1a.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* machine = dynamic_cast<griddyn::genmodels::GenModelGENSAE*>(generator->find("genmodel"));
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterESST1A*>(generator->find("exciter"));
    ASSERT_NE(machine, nullptr);
    ASSERT_NE(exciter, nullptr);
    EXPECT_DOUBLE_EQ(machine->get("tdop"), 5.0);
    EXPECT_DOUBLE_EQ(machine->get("tdopp"), 0.07);
    EXPECT_DOUBLE_EQ(machine->get("tqopp"), 0.09);
    EXPECT_DOUBLE_EQ(machine->get("s10"), 0.11);
    EXPECT_DOUBLE_EQ(machine->get("s12"), 0.39);
    EXPECT_DOUBLE_EQ(exciter->get("uel"), 1.0);
    EXPECT_DOUBLE_EQ(exciter->get("vos"), 2.0);
    EXPECT_DOUBLE_EQ(exciter->get("tr"), 0.10);
    EXPECT_DOUBLE_EQ(exciter->get("vimax"), 0.30);
    EXPECT_DOUBLE_EQ(exciter->get("vimin"), -0.30);
    EXPECT_DOUBLE_EQ(exciter->get("tc"), 0.05);
    EXPECT_DOUBLE_EQ(exciter->get("tb"), 0.20);
    EXPECT_DOUBLE_EQ(exciter->get("tc1"), 0.02);
    EXPECT_DOUBLE_EQ(exciter->get("tb1"), 0.10);
    EXPECT_DOUBLE_EQ(exciter->get("ka"), 20.0);
    EXPECT_DOUBLE_EQ(exciter->get("ta"), 0.25);
    EXPECT_DOUBLE_EQ(exciter->get("vamax"), 7.0);
    EXPECT_DOUBLE_EQ(exciter->get("vamin"), -7.0);
    EXPECT_DOUBLE_EQ(exciter->get("vrmax"), 5.0);
    EXPECT_DOUBLE_EQ(exciter->get("vrmin"), -5.0);
    EXPECT_DOUBLE_EQ(exciter->get("kc"), 0.20);
    EXPECT_DOUBLE_EQ(exciter->get("kf"), 0.10);
    EXPECT_DOUBLE_EQ(exciter->get("tf"), 0.50);
    EXPECT_DOUBLE_EQ(exciter->get("klr"), 2.0);
    EXPECT_DOUBLE_EQ(exciter->get("ilr"), 0.30);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsNewExciterFocusedChunkInDyrOrder)
{
    {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ieeet3.dyr"));
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
        ASSERT_NE(bus, nullptr);
        auto* exciter =
            dynamic_cast<griddyn::exciters::ExciterIEEET3*>(bus->getGen(0)->find("exciter"));
        ASSERT_NE(exciter, nullptr);
        const std::pair<std::string_view, double> expected[]{{"tr", 0.011},
                                                             {"ka", 5.1},
                                                             {"ta", 0.041},
                                                             {"vrmax", 20.1},
                                                             {"vrmin", -20.2},
                                                             {"vbmax", 18.3},
                                                             {"ke", 1.04},
                                                             {"te", 1.05},
                                                             {"kf", 0.106},
                                                             {"tf", 1.07},
                                                             {"kp", 4.08},
                                                             {"ki", 0.109}};
        for (const auto& [name, value] : expected) {
            EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
        }
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    }
    {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ac8b.dyr"));
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
        ASSERT_NE(bus, nullptr);
        auto* exciter =
            dynamic_cast<griddyn::exciters::ExciterAC8B*>(bus->getGen(0)->find("exciter"));
        ASSERT_NE(exciter, nullptr);
        const std::pair<std::string_view, double> expected[]{
            {"tr", 0.011},   {"kpr", 10.1},    {"kir", 11.2},   {"kdr", 12.3},    {"tdr", 0.24},
            {"vpmax", 99.5}, {"vpmin", -99.6}, {"vrmax", 99.7}, {"vrmin", -99.8}, {"vfemax", 99.9},
            {"vemin", -9.1}, {"ta", 0.42},     {"ka", 40.3},    {"te", 0.84},     {"kc", 0.015},
            {"kd", 0.016},   {"ke", 1.07},     {"e1", 3.8},     {"se1", 0.18},    {"e2", 2.6},
            {"se2", 0.05}};
        for (const auto& [name, value] : expected) {
            EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
        }
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    }
    {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ac7b.dyr"));
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
        ASSERT_NE(bus, nullptr);
        auto* exciter =
            dynamic_cast<griddyn::exciters::ExciterAC7B*>(bus->getGen(0)->find("exciter"));
        ASSERT_NE(exciter, nullptr);
        const std::pair<std::string_view, double> expected[]{
            {"tr", 0.011},    {"kpr", 4.1},     {"kir", 4.2},     {"kdr", 0.13}, {"tdr", 0.14},
            {"vrmax", 20.1},  {"vrmin", -20.2}, {"kpa", 2.1},     {"kia", 1.1},  {"vamax", 20.3},
            {"vamin", -20.4}, {"kp", 1.2},      {"kl", 0.3},      {"te", 1.3},   {"kc", 0.01},
            {"kd", 0.02},     {"ke", 1.01},     {"kf1", 0.2},     {"kf2", 0.03}, {"kf3", 0.04},
            {"tf3", 0.5},     {"vemin", -5.1},  {"vfemax", 20.5}, {"e1", 3.1},   {"se1", 0.1},
            {"e2", 2.1},      {"se2", 0.02}};
        for (const auto& [name, value] : expected) {
            EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
        }
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    }
    {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_esst2a.dyr"));
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
        ASSERT_NE(bus, nullptr);
        auto* exciter =
            dynamic_cast<griddyn::exciters::ExciterESST2A*>(bus->getGen(0)->find("exciter"));
        ASSERT_NE(exciter, nullptr);
        const std::pair<std::string_view, double> expected[]{{"tr", 0.011},
                                                             {"ka", 40.2},
                                                             {"ta", 0.053},
                                                             {"vrmax", 99.4},
                                                             {"vrmin", -99.5},
                                                             {"kp", 0.71},
                                                             {"ki", 1.02},
                                                             {"kc", 0.033},
                                                             {"kf", 0.054},
                                                             {"tf", 0.75},
                                                             {"ke", 1.06},
                                                             {"te", 0.57},
                                                             {"efdmax", 9.8}};
        for (const auto& [name, value] : expected) {
            EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
        }
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    }
}

TEST(DyrReaderComparisonTests, MapsExpic1ParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_expic1.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterEXPIC1*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expected[]{
        {"tr", 0.1},  {"ka", 20.0},  {"ta1", 0.02}, {"vr1", 4.0},    {"vr2", -3.0},
        {"ta2", 0.2}, {"ta3", 0.03}, {"ta4", 0.4},  {"vrmax", 5.0},  {"vrmin", -5.0},
        {"kf", 0.1},  {"tf1", 0.5},  {"tf2", 0.6},  {"efdmax", 5.0}, {"efdmin", -4.0},
        {"ke", 1.0},  {"te", 0.5},   {"e1", 0.0},   {"se1", 0.0},    {"e2", 1.0},
        {"se2", 0.0}, {"kp", 1.0},   {"ki", 0.1},   {"kc", 0.05}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsScrxParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_scrx.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterSCRX*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expected[]{{"tatb", 0.2},
                                                         {"tb", 0.5},
                                                         {"k", 10.0},
                                                         {"te", 0.25},
                                                         {"emin", -10.0},
                                                         {"emax", 10.0},
                                                         {"cswitch", 0.0},
                                                         {"rcrfd", 3.0}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsEsac5aParametersInPsseDyrOrder)
{
    auto simulation = loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_esac5a.dyr"});
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterESAC5A*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expected[]{{"tr", 0.05},
                                                         {"ka", 20.0},
                                                         {"ta", 0.1},
                                                         {"vrmax", 100.0},
                                                         {"vrmin", -100.0},
                                                         {"ke", 1.0},
                                                         {"te", 0.5},
                                                         {"kf", 0.03},
                                                         {"tf1", 1.0},
                                                         {"tf2", 0.8},
                                                         {"tf3", 1.0},
                                                         {"e1", 0.0},
                                                         {"se1", 0.0},
                                                         {"e2", 1.0},
                                                         {"se2", 0.0}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsEsac6aParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_esac6a.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterESAC6A*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expected[]{
        {"tr", 0.05}, {"ka", 20.0},     {"ta", 0.1},       {"tk", 0.02},     {"tb", 0.4},
        {"tc", 0.1},  {"vamax", 100.0}, {"vamin", -100.0}, {"vrmax", 100.0}, {"vrmin", -100.0},
        {"te", 0.5},  {"vfelim", 0.0},  {"kh", 0.1},       {"vhmax", 100.0}, {"th", 0.5},
        {"tj", 0.1},  {"kc", 0.0},      {"kd", 0.0},       {"ke", 1.0},      {"e1", 0.0},
        {"se1", 0.0}, {"e2", 1.0},      {"se2", 0.0}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsCanonicalDcAndTypeOneExciters)
{
    const std::array<std::pair<std::string_view, std::string_view>, 6> records{{
        {"ieee14_esdc1a.dyr", "esdc1a"},
        {"ieee14_esdc1a_no_tb.dyr", "ieeet1"},
        {"ieee14_esdc2a.dyr", "esdc2a"},
        {"ieee14_exdc2.dyr", "exdc2"},
        {"ieee14_ieeet1.dyr", "ieeet1"},
        {"ieee14_esac1a.dyr", "esac1a"},
    }};
    for (const auto& [record, expectedName] : records) {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath(record));
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
        ASSERT_NE(bus, nullptr);
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr);
        auto* exciter = generator->find("exciter");
        ASSERT_NE(exciter, nullptr) << record;
        if (expectedName == "esdc1a") {
            EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterDC1A*>(exciter), nullptr);
        } else if (expectedName == "ieeet1") {
            EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterIEEEtype1*>(exciter), nullptr);
        } else if (expectedName == "esac1a") {
            EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterESAC1A*>(exciter), nullptr);
        } else {
            EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterDC2A*>(exciter), nullptr);
        }
        const std::array<std::pair<std::string_view, double>, 5> expectedParameters{
            {{"tr", 0.1}, {"e1", 1.0}, {"se1", 0.1}, {"e2", 2.0}, {"se2", 0.2}}};
        for (const auto& [parameter, value] : expectedParameters) {
            if (expectedName != "esac1a") {
                EXPECT_DOUBLE_EQ(exciter->get(parameter), value) << record << ' ' << parameter;
            }
        }
        if (expectedName == "esac1a") {
            EXPECT_DOUBLE_EQ(exciter->get("vamax"), 6.0);
            EXPECT_DOUBLE_EQ(exciter->get("vamin"), -5.0);
            EXPECT_DOUBLE_EQ(exciter->get("ka"), 10.0);
            EXPECT_DOUBLE_EQ(exciter->get("ta"), 0.1);
            EXPECT_DOUBLE_EQ(exciter->get("vrmax"), 5.0);
            EXPECT_DOUBLE_EQ(exciter->get("vrmin"), -4.0);
            EXPECT_DOUBLE_EQ(exciter->get("te"), 0.5);
            EXPECT_DOUBLE_EQ(exciter->get("kf"), 0.03);
            EXPECT_DOUBLE_EQ(exciter->get("tf"), 1.0);
            EXPECT_DOUBLE_EQ(exciter->get("kc"), 0.2);
            EXPECT_DOUBLE_EQ(exciter->get("kd"), 0.3);
        }
    }
}

TEST(DyrReaderComparisonTests, MapsIeeet2ParametersInOpenIpslDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ieeet2.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* exciter =
        dynamic_cast<griddyn::exciters::ExciterIEEEtype2*>(bus->getGen(0)->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expected[]{{"tr", 0.0},
                                                         {"ka", 729.0},
                                                         {"ta", 0.04},
                                                         {"vrmax", 5.32},
                                                         {"vrmin", -4.05},
                                                         {"ke", 1.0},
                                                         {"te", 0.44},
                                                         {"kf", 0.0667},
                                                         {"tf1", 2.0},
                                                         {"tf2", 0.44},
                                                         {"e1", 6.5},
                                                         {"se1", 0.054},
                                                         {"e2", 8.0},
                                                         {"se2", 0.202}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsEsst4bParametersAndCouplesToGensal)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_gensal.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_esst4b.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterESST4B*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    const std::pair<std::string_view, double> expected[]{{"tr", 0.021},
                                                         {"kpr", 10.2},
                                                         {"kir", 2.3},
                                                         {"vrmax", 99.0},
                                                         {"vrmin", -99.0},
                                                         {"ta", 0.104},
                                                         {"kpm", 2.5},
                                                         {"kim", 3.6},
                                                         {"vmmax", 99.0},
                                                         {"vmmin", -99.0},
                                                         {"kg", 0.107},
                                                         {"kp", 3.68},
                                                         {"ki", 0.439},
                                                         {"vbmax", 20.0},
                                                         {"kc", 0.011},
                                                         {"xl", 0.0099},
                                                         {"thetap", 3.34}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(exciter->get(name), value) << name;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsGastParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_gast.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* governor = dynamic_cast<griddyn::governors::GovernorGast*>(generator->find("governor"));
    ASSERT_NE(governor, nullptr);
    const std::pair<std::string_view, double> expected[]{{"r", 0.051},
                                                         {"t1", 0.41},
                                                         {"t2", 0.12},
                                                         {"t3", 3.2},
                                                         {"at", 1.21},
                                                         {"kt", 2.3},
                                                         {"vmax", 1.4},
                                                         {"vmin", -0.04},
                                                         {"dt", 0.031}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(governor->get(name), value) << name;
    }
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsGgov1ParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ggov1.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* governor = dynamic_cast<griddyn::governors::GovernorGgov1*>(generator->find("governor"));
    ASSERT_NE(governor, nullptr);
    const std::pair<std::string_view, double> expected[]{
        {"rselect", 1.0},  {"fswitch", 0.0}, {"r", 0.041},   {"tpelec", 0.22}, {"maxerr", 0.08},
        {"minerr", -0.07}, {"kpgov", 9.3},   {"kigov", 1.7}, {"kdgov", 0.12},  {"tdgov", 0.31},
        {"vmax", 2.0},     {"vmin", 0.1},    {"tact", 0.42}, {"kturb", 1.5},   {"wfnl", 0.2},
        {"tb", 0.13},      {"tc", 0.02},     {"teng", 0.0},  {"tfload", 3.2},  {"kpload", 2.1},
        {"kiload", 0.68},  {"ldref", 0.4},   {"dm", 0.03},   {"ropen", 0.15},  {"rclose", -0.16},
        {"kimw", 0.04},    {"aset", 0.11},   {"ka", 10.2},   {"ta", 0.12},     {"trate", 45.0},
        {"db", 0.001},     {"tsa", 4.1},     {"tsb", 5.2},   {"rup", 98.0},    {"rdown", -97.0}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(governor->get(name), value) << name;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsTgov1ParametersInAndesDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_tgov1.dyr"));

    struct Tgov1Parameters {
        index_t busId;
        double r;
        double t1;
        double pmax;
        double pmin;
        double t2;
        double t3;
        double dt;
    };
    constexpr Tgov1Parameters expected[]{{.busId = 1,
                                          .r = 0.05,
                                          .t1 = 0.05,
                                          .pmax = 1.05,
                                          .pmin = 0.3,
                                          .t2 = 1.0,
                                          .t3 = 2.1,
                                          .dt = 0.0},
                                         {.busId = 6,
                                          .r = 0.04,
                                          .t1 = 0.06,
                                          .pmax = 1.10,
                                          .pmin = 0.25,
                                          .t2 = 1.2,
                                          .t3 = 2.3,
                                          .dt = 0.02},
                                         {.busId = 8,
                                          .r = 0.03,
                                          .t1 = 0.07,
                                          .pmax = 1.15,
                                          .pmin = 0.2,
                                          .t2 = 1.4,
                                          .t3 = 2.5,
                                          .dt = 0.04}};

    for (const auto& entry : expected) {
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", entry.busId));
        ASSERT_NE(bus, nullptr) << "bus " << entry.busId;
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr) << "generator at bus " << entry.busId;
        auto* governor =
            dynamic_cast<griddyn::governors::GovernorTgov1*>(generator->find("governor"));
        ASSERT_NE(governor, nullptr) << "TGOV1 at bus " << entry.busId;

        EXPECT_DOUBLE_EQ(governor->get("r"), entry.r);
        EXPECT_DOUBLE_EQ(governor->get("t1"), entry.t1);
        EXPECT_DOUBLE_EQ(governor->get("pmax"), entry.pmax);
        EXPECT_DOUBLE_EQ(governor->get("pmin"), entry.pmin);
        EXPECT_DOUBLE_EQ(governor->get("t2"), entry.t2);
        EXPECT_DOUBLE_EQ(governor->get("t3"), entry.t3);
        EXPECT_DOUBLE_EQ(governor->get("dt"), entry.dt);
    }
}

TEST(DyrReaderComparisonTests, MapsIeesgoParametersAndInitializes)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ieesgo.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* governor = dynamic_cast<griddyn::governors::GovernorReheat*>(generator->find("governor"));
    ASSERT_NE(governor, nullptr);

    const std::pair<std::string_view, double> expected[]{{"t1", 0.12},
                                                         {"t2", 0.03},
                                                         {"t3", 0.22},
                                                         {"t4", 0.40},
                                                         {"t5", 7.0},
                                                         {"t6", 0.35},
                                                         {"k1", 20.0},
                                                         {"k2", 0.71},
                                                         {"k3", 0.425},
                                                         {"pmax", 1.5},
                                                         {"pmin", 0.1}};
    for (const auto& [name, value] : expected) {
        EXPECT_DOUBLE_EQ(governor->get(name), value) << name;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsHygovParametersInAndesDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_hygov.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* governor = dynamic_cast<griddyn::governors::GovernorHygov*>(generator->find("governor"));
    ASSERT_NE(governor, nullptr);

    const std::pair<std::string_view, double> expected[]{{"r", 0.05},
                                                         {"temporarydroop", 0.3},
                                                         {"tr", 5.0},
                                                         {"tf", 0.05},
                                                         {"tg", 0.5},
                                                         {"velm", 0.02},
                                                         {"gmax", 0.9},
                                                         {"gmin", 0.0},
                                                         {"tw", 1.25},
                                                         {"at", 1.2},
                                                         {"dturb", 0.2},
                                                         {"qnl", 0.08}};
    for (const auto& parameter : expected) {
        EXPECT_DOUBLE_EQ(governor->get(parameter.first), parameter.second) << parameter.first;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsGovernorVariants)
{
    struct Case {
        std::string_view file;
        std::string_view factory;
        std::string_view parameter;
        double value;
    };
    for (const Case testCase :
         {Case{.file = "ieee14_tg2.dyr", .factory = "tg2", .parameter = "t2", .value = 10.0},
          Case{.file = "ieee14_tgov1db.dyr",
               .factory = "tgov1db",
               .parameter = "dbu",
               .value = 0.001},
          Case{.file = "ieee14_tgov1n.dyr", .factory = "tgov1n", .parameter = "t3", .value = 2.0},
          Case{.file = "ieee14_tgov1ndb.dyr",
               .factory = "tgov1ndb",
               .parameter = "dbl",
               .value = -0.001},
          Case{.file = "ieee14_hygovdb.dyr",
               .factory = "hygovdb",
               .parameter = "dbu",
               .value = 0.001},
          Case{.file = "ieee14_hygov4.dyr",
               .factory = "hygov4",
               .parameter = "hdam",
               .value = 1.0}}) {
        SCOPED_TRACE(std::string(testCase.file));
        auto simulation = loadComparisonDynamicCase("ieee14_genrou.dyr", {testCase.file});
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
        ASSERT_NE(bus, nullptr);
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr);
        auto* governor = dynamic_cast<griddyn::Governor*>(generator->find("governor"));
        ASSERT_NE(governor, nullptr);
        EXPECT_DOUBLE_EQ(governor->get(testCase.parameter), testCase.value);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    }
}

TEST(DynamicComparisonTests, GovernorVariantsRespondToGeneratorStep)
{
    for (const auto file : {"ieee14_tg2.dyr",
                            "ieee14_tgov1db.dyr",
                            "ieee14_tgov1n.dyr",
                            "ieee14_tgov1ndb.dyr",
                            "ieee14_hygov4.dyr"}) {
        SCOPED_TRACE(file);
        const auto finalState = runGeneratorSetpointStepCase({file});
        EXPECT_FALSE(finalState.empty());
    }
}

TEST(DynamicComparisonTests, HygovDbRemainsAtEquilibriumInsideSpeedDeadband)
{
    auto simulation = loadComparisonDynamicCase("ieee14_genrou.dyr", {"ieee14_hygovdb.dyr"});
    ASSERT_EQ(simulation->dynInitialize(), 0);
    const auto initial = simulation->getState();
    ASSERT_EQ(simulation->run(2.0), 0);
    const auto final = simulation->getState();
    ASSERT_EQ(final.size(), initial.size());
    for (std::size_t i = 0; i < final.size(); ++i) {
        EXPECT_TRUE(std::isfinite(final[i]));
        EXPECT_NEAR(final[i], initial[i], 1e-5);
    }
}

TEST(DyrReaderComparisonTests, MapsIeeeG1ParametersInFrozenAndesDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ieeeg1.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* governor = dynamic_cast<griddyn::governors::GovernorIeeeG1*>(generator->find("governor"));
    ASSERT_NE(governor, nullptr);

    const std::pair<std::string_view, double> expected[]{
        {"k", 18.0},   {"t1", 0.12},   {"t2", 0.03},   {"t3", 0.22}, {"uo", 0.42},
        {"uc", -0.31}, {"pmax", 1.25}, {"pmin", 0.15}, {"t4", 0.14}, {"k1", 0.20},
        {"k2", 0.0},   {"t5", 0.25},   {"k3", 0.30},   {"k4", 0.0},  {"t6", 0.36},
        {"k5", 0.10},  {"k6", 0.0},    {"t7", 0.47},   {"k7", 0.40}, {"k8", 0.0}};
    for (const auto& parameter : expected) {
        EXPECT_DOUBLE_EQ(governor->get(parameter.first), parameter.second) << parameter.first;
    }
    EXPECT_EQ(generator->getMechanicalPowerSource(), governor);
    EXPECT_EQ(generator->getMechanicalPowerOutput(), griddyn::governors::GovernorIeeeG1::hpOutput);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, ConnectsIeeeG1LowPressureOutputToSecondGenerator)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ieeeg1_cross.dyr"));

    auto* primaryBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    auto* secondaryBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(primaryBus, nullptr);
    ASSERT_NE(secondaryBus, nullptr);
    auto* primary = dynamic_cast<griddyn::DynamicGenerator*>(primaryBus->getGen(0));
    auto* secondary = dynamic_cast<griddyn::DynamicGenerator*>(secondaryBus->getGen(0));
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);
    auto* governor = dynamic_cast<griddyn::governors::GovernorIeeeG1*>(primary->find("governor"));
    ASSERT_NE(governor, nullptr);

    EXPECT_EQ(primary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(primary->getMechanicalPowerOutput(), griddyn::governors::GovernorIeeeG1::hpOutput);
    EXPECT_EQ(secondary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerOutput(), griddyn::governors::GovernorIeeeG1::lpOutput);
}

TEST(DyrReaderComparisonTests, ResolvesAlphanumericMachineIdsAcrossModelFamilies)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    generator->setName(bus->getName() + "_Gen_G1");

    EXPECT_NO_THROW(
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_alphanumeric_ids.dyr")));
    EXPECT_NE(dynamic_cast<griddyn::genmodels::GenModelGENROU*>(generator->find("genmodel")),
              nullptr);
    EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterSCRX*>(generator->find("exciter")), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::governors::GovernorTgov1*>(generator->find("governor")),
              nullptr);
    EXPECT_NE(dynamic_cast<griddyn::stabilizers::StabilizerST2CUT*>(generator->find("pss")),
              nullptr);
}

TEST(DyrReaderComparisonTests, PrefersExactNumericMachineIdOverLegacyPosition)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* firstGenerator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(firstGenerator, nullptr);
    firstGenerator->setName(bus->getName() + "_Gen_G1");

    auto* exactIdGenerator = new griddyn::DynamicGenerator();
    exactIdGenerator->setName(bus->getName() + "_Gen_1");
    bus->add(exactIdGenerator);

    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_scrx.dyr"));
    EXPECT_EQ(firstGenerator->find("exciter"), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterSCRX*>(exactIdGenerator->find("exciter")),
              nullptr);
}

TEST(DyrReaderComparisonTests, RetainsLegacyNumericMachinePositionFallback)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    generator->setName(bus->getName() + "_Gen_G1");

    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_scrx.dyr"));
    EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterSCRX*>(generator->find("exciter")), nullptr);
}

TEST(DyrReaderComparisonTests, ResolvesAlphanumericIeeeG1PrimaryAndSecondaryIds)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    auto* primaryBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    auto* secondaryBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(primaryBus, nullptr);
    ASSERT_NE(secondaryBus, nullptr);
    auto* primary = dynamic_cast<griddyn::DynamicGenerator*>(primaryBus->getGen(0));
    auto* secondary = dynamic_cast<griddyn::DynamicGenerator*>(secondaryBus->getGen(0));
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);
    primary->setName(primaryBus->getName() + "_Gen_P1");
    secondary->setName(secondaryBus->getName() + "_Gen_S1");

    EXPECT_NO_THROW(
        griddyn::loadFile(simulation.get(),
                          makeComparisonTestPath("ieee14_ieeeg1_alphanumeric_ids.dyr")));
    auto* governor = dynamic_cast<griddyn::governors::GovernorIeeeG1*>(primary->find("governor"));
    ASSERT_NE(governor, nullptr);
    EXPECT_EQ(primary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerOutput(), griddyn::governors::GovernorIeeeG1::lpOutput);
}

TEST(DyrReaderComparisonTests, LoadsEsst3aWithGenrouSignals)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_esst3a.dyr"));

    for (const index_t busId : {1, 3, 6, 8}) {
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", busId));
        ASSERT_NE(bus, nullptr) << "bus " << busId;
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr) << "generator at bus " << busId;
        auto* exciter = dynamic_cast<griddyn::exciters::ExciterESST3A*>(generator->find("exciter"));
        ASSERT_NE(exciter, nullptr) << "ESST3A at bus " << busId;

        EXPECT_DOUBLE_EQ(exciter->get("tr"), 0.02);
        EXPECT_DOUBLE_EQ(exciter->get("vimax"), 0.2);
        EXPECT_DOUBLE_EQ(exciter->get("vimin"), -0.2);
        EXPECT_DOUBLE_EQ(exciter->get("km"), 8.0);
        EXPECT_DOUBLE_EQ(exciter->get("tc"), 1.0);
        EXPECT_DOUBLE_EQ(exciter->get("tb"), 5.0);
        EXPECT_DOUBLE_EQ(exciter->get("ka"), 20.0);
        EXPECT_DOUBLE_EQ(exciter->get("ta"), 0.0);
        EXPECT_DOUBLE_EQ(exciter->get("vrmax"), 99.0);
        EXPECT_DOUBLE_EQ(exciter->get("vrmin"), -99.0);
        EXPECT_DOUBLE_EQ(exciter->get("kg"), 1.0);
        EXPECT_DOUBLE_EQ(exciter->get("kp"), 3.67);
        EXPECT_DOUBLE_EQ(exciter->get("ki"), 0.435);
        EXPECT_DOUBLE_EQ(exciter->get("vbmax"), 5.48);
        EXPECT_DOUBLE_EQ(exciter->get("kc"), 0.01);
        EXPECT_DOUBLE_EQ(exciter->get("xl"), 0.0098);
        EXPECT_DOUBLE_EQ(exciter->get("vgmax"), 3.86);
        EXPECT_DOUBLE_EQ(exciter->get("thetap"), 3.33);
        EXPECT_DOUBLE_EQ(exciter->get("tm"), 0.4);
        EXPECT_DOUBLE_EQ(exciter->get("vmmax"), 99.0);
        EXPECT_DOUBLE_EQ(exciter->get("vmmin"), 0.0);
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsExst1ParametersAndCouplesToGenrou)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_exst1.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    ASSERT_NE(dynamic_cast<griddyn::genmodels::GenModelGENROU*>(generator->find("genmodel")),
              nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterEXST1*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);

    EXPECT_DOUBLE_EQ(exciter->get("tr"), 0.031);
    EXPECT_DOUBLE_EQ(exciter->get("vimax"), 0.41);
    EXPECT_DOUBLE_EQ(exciter->get("vimin"), -0.37);
    EXPECT_DOUBLE_EQ(exciter->get("tc"), 0.12);
    EXPECT_DOUBLE_EQ(exciter->get("tb"), 0.23);
    EXPECT_DOUBLE_EQ(exciter->get("ka"), 41.0);
    EXPECT_DOUBLE_EQ(exciter->get("ta"), 0.034);
    EXPECT_DOUBLE_EQ(exciter->get("vrmax"), 7.2);
    EXPECT_DOUBLE_EQ(exciter->get("vrmin"), -4.3);
    EXPECT_DOUBLE_EQ(exciter->get("kc"), 0.17);
    EXPECT_DOUBLE_EQ(exciter->get("kf"), 0.08);
    EXPECT_DOUBLE_EQ(exciter->get("tf"), 1.3);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsExacParameterRecordsAndCouplesToGenrou)
{
    const std::array<std::pair<std::string_view, std::string_view>, 5> records{{
        {"ieee14_exac1.dyr", "exac1"},
        {"ieee14_exac2.dyr", "exac2"},
        {"ieee14_exac4.dyr", "exac4"},
        {"ieee14_exac4_spaced_name.dyr", "exac4 with whitespace in model name"},
        {"ieee14_esac1a_genrou.dyr", "esac1a"},
    }};
    for (const auto& [record, model] : records) {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
        griddyn::loadFile(simulation.get(), makeComparisonTestPath(record));
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
        ASSERT_NE(bus, nullptr);
        auto* generator = bus->getGen(0);
        ASSERT_NE(generator, nullptr);
        auto* exciter = dynamic_cast<griddyn::Exciter*>(generator->find("exciter"));
        ASSERT_NE(exciter, nullptr);
        EXPECT_DOUBLE_EQ(exciter->get("tr"), 0.031) << model;
        EXPECT_DOUBLE_EQ(exciter->get("ka"), 41.0) << model;
        EXPECT_DOUBLE_EQ(exciter->get("ta"), 0.034) << model;
        EXPECT_DOUBLE_EQ(exciter->get("vrmax"), 7.2) << model;
        EXPECT_DOUBLE_EQ(exciter->get("vrmin"), -4.3) << model;
        ASSERT_EQ(simulation->dynInitialize(), 0) << model;
        EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0) << model;
        EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0) << model;
    }
}

TEST(DyrReaderComparisonTests, LoadsExac1WithZeroTr)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_exac1_tr0.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterEXAC1*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    EXPECT_DOUBLE_EQ(exciter->get("tr"), 0.0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(exciter->getStates().size(), 5U);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, LoadsExac1WithZeroTb)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_exac1_tb0.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 2));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* exciter = dynamic_cast<griddyn::exciters::ExciterEXAC1*>(generator->find("exciter"));
    ASSERT_NE(exciter, nullptr);
    EXPECT_DOUBLE_EQ(exciter->get("tb"), 0.0);
    EXPECT_DOUBLE_EQ(exciter->get("tc"), 0.0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(exciter->getStates().size(), 4U);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsSt2cutParametersAndCouplesToExciters)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_esst3a.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_exst1.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_st2cut.dyr"));

    for (const auto& [busId, expectedK1, expectedLsmax] :
         {std::tuple{1, 1.2, 0.05}, std::tuple{2, 1.1, 0.06}}) {
        auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", busId));
        ASSERT_NE(bus, nullptr);
        auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
        ASSERT_NE(generator, nullptr);
        auto* stabilizer =
            dynamic_cast<griddyn::stabilizers::StabilizerST2CUT*>(generator->find("pss"));
        ASSERT_NE(stabilizer, nullptr);
        EXPECT_DOUBLE_EQ(stabilizer->get("mode"), 1.0);
        EXPECT_DOUBLE_EQ(stabilizer->get("mode2"), 0.0);
        EXPECT_DOUBLE_EQ(stabilizer->get("k1"), expectedK1);
        EXPECT_DOUBLE_EQ(stabilizer->get("lsmax"), expectedLsmax);
    }
    EXPECT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DyrReaderComparisonTests, MapsIeeestParametersAndCouplesToExciter)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_esst3a.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_ieeest.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 3));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    ASSERT_NE(dynamic_cast<griddyn::exciters::ExciterESST3A*>(generator->find("exciter")), nullptr);
    auto* stabilizer =
        dynamic_cast<griddyn::stabilizers::StabilizerIEEEST*>(generator->find("pss"));
    ASSERT_NE(stabilizer, nullptr);

    const std::pair<std::string_view, double> expected[]{{"mode", 3.0},
                                                         {"busr", 0.0},
                                                         {"a1", 0.0},
                                                         {"a2", 0.0},
                                                         {"a3", 0.0},
                                                         {"a4", 0.0},
                                                         {"a5", 0.0},
                                                         {"a6", 0.0},
                                                         {"t1", 0.0},
                                                         {"t2", 0.0},
                                                         {"t3", 0.0},
                                                         {"t4", 0.75},
                                                         {"t5", 1.0},
                                                         {"t6", 4.2},
                                                         {"ks", -2.0},
                                                         {"lsmax", 0.1},
                                                         {"lsmin", -0.1},
                                                         {"vcu", 999.0},
                                                         {"vcl", -999.0}};
    for (const auto& parameter : expected) {
        EXPECT_DOUBLE_EQ(stabilizer->get(parameter.first), parameter.second) << parameter.first;
    }

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(DynamicComparisonTests, Tgov1RespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_tgov1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, IeeeG1RespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_ieeeg1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, ConventionalGovernorsRespondToGeneratorSetpointStep)
{
    struct GovernorStepCase {
        std::string_view record;
        index_t busId;
    };
    constexpr std::array<GovernorStepCase, 3> cases{{
        {.record = "ieee14_gast.dyr", .busId = 1},
        {.record = "ieee14_ggov1.dyr", .busId = 2},
        {.record = "ieee14_ieesgo.dyr", .busId = 1},
    }};

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.record);
        const auto finalState = runGeneratorSetpointStepCase({testCase.record},
                                                             0.8,
                                                             "ieee14_genrou.dyr",
                                                             testCase.busId);
        EXPECT_FALSE(finalState.empty());
    }
}

TEST(DynamicComparisonTests, Esst3aRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_esst3a.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, Exst1RespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_exst1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, ExacExcitersRespondToGeneratorSetpointStep)
{
    for (const auto record :
         {"ieee14_exac1.dyr", "ieee14_exac2.dyr", "ieee14_exac4.dyr", "ieee14_esac1a_genrou.dyr"}) {
        const auto finalState = runGeneratorSetpointStepCase({record});
        EXPECT_FALSE(finalState.empty()) << record;
    }
}

TEST(DynamicComparisonTests, ScrxRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_scrx.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, Esac6aRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_esac6a.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, Esac5aRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_esac5a.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, RecentExcitersRespondToGeneratorSetpointStep)
{
    for (const auto record :
         {"ieee14_esst2a.dyr", "ieee14_ieeet3.dyr", "ieee14_ac7b.dyr", "ieee14_ac8b.dyr"}) {
        SCOPED_TRACE(record);
        const auto finalState = runGeneratorSetpointStepCase({record});
        EXPECT_FALSE(finalState.empty());
    }
}

TEST(DynamicComparisonTests, GensaeEsst1aRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_gensae_esst1a.dyr"}, 0.8, "");
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, GenroeIeeex1RespondsToGeneratorSetpointStep)
{
    const auto finalState =
        runGeneratorSetpointStepCase({"ieee14_genroe_ieeex1.dyr"}, 0.8, "", 1, 1.0e-10);
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, GovernorAndRecentExciterPlantsRespondToGeneratorSetpointStep)
{
    for (const auto& records :
         {std::vector<std::string_view>{"ieee14_tgov1.dyr", "ieee14_esst2a.dyr"},
          std::vector<std::string_view>{"ieee14_gast.dyr", "ieee14_ieeet3.dyr"},
          std::vector<std::string_view>{"ieee14_ieesgo.dyr", "ieee14_ac7b.dyr"},
          std::vector<std::string_view>{"ieee14_tgov1.dyr", "ieee14_ac8b.dyr"}}) {
        SCOPED_TRACE(testing::Message() << records[0] << " + " << records[1]);
        const auto finalState = runGeneratorSetpointStepCase(records);
        EXPECT_FALSE(finalState.empty());
    }
}

TEST(DynamicComparisonTests, GensalHygovEsst4bPlantInitializesAndRuns)
{
    auto simulation =
        loadComparisonDynamicCase("ieee14_gensal.dyr", {"ieee14_hygov.dyr", "ieee14_esst4b.dyr"});

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    ASSERT_NE(dynamic_cast<griddyn::genmodels::GenModelGENSAL*>(generator->find("genmodel")),
              nullptr);
    ASSERT_NE(dynamic_cast<griddyn::governors::GovernorHygov*>(generator->find("governor")),
              nullptr);
    ASSERT_NE(dynamic_cast<griddyn::exciters::ExciterESST4B*>(generator->find("exciter")), nullptr);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    const auto initialState = simulation->getState();
    EXPECT_FALSE(initialState.empty());
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(2.0), 0);
    EXPECT_EQ(simulation->getSimulationTime(), 2.0);
    const auto finalState = simulation->getState();
    ASSERT_EQ(finalState.size(), initialState.size());
    for (const auto state : finalState) {
        EXPECT_TRUE(std::isfinite(state));
    }
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, St2cutRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_esst3a.dyr", "ieee14_exst1.dyr", "ieee14_st2cut.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, IeeestRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_esst3a.dyr", "ieee14_exst1.dyr", "ieee14_ieeest.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, CombinedControllersRespondToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_tgov1.dyr", "ieee14_ieeeg1.dyr", "ieee14_esst3a.dyr", "ieee14_exst1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, CombinedControllersRemainStableWithElevatedGeneratorSetpoint)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_tgov1.dyr", "ieee14_ieeeg1.dyr", "ieee14_esst3a.dyr", "ieee14_exst1.dyr"}, 0.9);
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, ExciterControllersRemainStableWithElevatedGeneratorSetpoint)
{
    const auto finalState =
        runGeneratorSetpointStepCase({"ieee14_esst3a.dyr", "ieee14_exst1.dyr"}, 0.9);
    EXPECT_FALSE(finalState.empty());
}

TEST(DynamicComparisonTests, Tgov1TrajectoryMatchesAndesReference)
{
    std::ifstream input(makeComparisonTestPath("ieee14_tgov1_trajectory_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_tgov1.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto event = std::make_shared<griddyn::Event>(1.0);
    ASSERT_TRUE(event->setTarget(generator, "pset"));
    event->setValue(0.8);
    simulation->add(event);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    const auto& times = reference["times"];
    const auto& busIds = reference["generator_bus_ids"];
    const auto& expectedOmega = reference["genrou_omega"];
    const auto omegaTolerance = reference["omega_absolute_tolerance"].get<double>();
    ASSERT_EQ(times.size(), expectedOmega.size());

    for (std::size_t timeIndex = 0; timeIndex < times.size(); ++timeIndex) {
        simulation->run(times[timeIndex].get<double>());
        for (std::size_t generatorIndex = 0; generatorIndex < busIds.size(); ++generatorIndex) {
            auto* sampleBus = dynamic_cast<griddyn::GridBus*>(
                simulation->findByUserID("bus", busIds[generatorIndex].get<index_t>()));
            ASSERT_NE(sampleBus, nullptr);
            auto* sampleGenerator = dynamic_cast<griddyn::DynamicGenerator*>(sampleBus->getGen(0));
            ASSERT_NE(sampleGenerator, nullptr);
            auto* genModel = dynamic_cast<griddyn::genmodels::GenModelGENROU*>(
                sampleGenerator->find("genmodel"));
            ASSERT_NE(genModel, nullptr);
            const auto& states = genModel->getStates();
            // GENROU stores its two algebraic currents before the differential
            // states; omega is therefore local state index 3.
            ASSERT_GT(states.size(), 3U);
            EXPECT_NEAR(states[3],
                        expectedOmega[timeIndex][generatorIndex].get<double>(),
                        omegaTolerance)
                << "time=" << times[timeIndex].get<double>()
                << " bus=" << busIds[generatorIndex].get<index_t>();
        }
    }
}

TEST(DynamicComparisonTests, Tgov1DownwardTrajectoryMatchesAndesReference)
{
    // ANDES 2.0.0 reference: TGOV1_1.pref0 is changed from its initialized
    // value to 0.6 at t=1.0 s.  This exercises the opposite direction of the
    // controller step while retaining the same shared RAW/DYR case.
    constexpr std::array<double, 5> times{0.0, 0.5, 1.0, 1.5, 2.0};
    constexpr std::array<std::array<double, 5>, 5> expectedOmega{{
        {{1.0, 1.0, 1.0, 1.0, 1.0}},
        {{1.0, 1.0, 1.0, 1.0, 1.0}},
        {{1.0, 1.0, 1.0, 1.0, 1.0}},
        {{0.9989737769949087,
          0.9989924996844121,
          0.9990010011199155,
          0.9991283354866937,
          0.99922546403493}},
        {{0.9978834683434779,
          0.9981224543226336,
          0.9981346723285689,
          0.9982087092060992,
          0.9982448696384979}},
    }};

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("ieee14_tgov1.dyr"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto event = std::make_shared<griddyn::Event>(1.0);
    ASSERT_TRUE(event->setTarget(generator, "pset"));
    event->setValue(0.6);
    simulation->add(event);
    ASSERT_EQ(simulation->dynInitialize(), 0);

    for (std::size_t timeIndex = 0; timeIndex < times.size(); ++timeIndex) {
        ASSERT_EQ(simulation->run(times[timeIndex]), 0);
        for (std::size_t generatorIndex = 0; generatorIndex < expectedOmega[timeIndex].size();
             ++generatorIndex) {
            auto* sampleBus = dynamic_cast<griddyn::GridBus*>(
                simulation->findByUserID("bus",
                                         std::array<index_t, 5>{1, 2, 3, 6, 8}[generatorIndex]));
            ASSERT_NE(sampleBus, nullptr);
            auto* sampleGenerator = dynamic_cast<griddyn::DynamicGenerator*>(sampleBus->getGen(0));
            ASSERT_NE(sampleGenerator, nullptr);
            auto* genModel = dynamic_cast<griddyn::genmodels::GenModelGENROU*>(
                sampleGenerator->find("genmodel"));
            ASSERT_NE(genModel, nullptr);
            ASSERT_GT(genModel->getStates().size(), 3U);
            EXPECT_NEAR(genModel->getStates()[3], expectedOmega[timeIndex][generatorIndex], 0.005)
                << "time=" << times[timeIndex] << " generator=" << generatorIndex;
        }
    }
}
