/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/GridSubModel.h"
#include "griddyn/events/Event.h"
#include "griddyn/exciters/ExciterDC1A.h"
#include "griddyn/exciters/ExciterDC2A.h"
#include "griddyn/exciters/ExciterESAC1A.h"
#include "griddyn/exciters/ExciterESAC6A.h"
#include "griddyn/exciters/ExciterESST1A.h"
#include "griddyn/exciters/ExciterESST3A.h"
#include "griddyn/exciters/ExciterESST4B.h"
#include "griddyn/exciters/ExciterEXAC1.h"
#include "griddyn/exciters/ExciterEXAC2.h"
#include "griddyn/exciters/ExciterEXAC4.h"
#include "griddyn/exciters/ExciterEXPIC1.h"
#include "griddyn/exciters/ExciterEXST1.h"
#include "griddyn/exciters/ExciterIEEEX1.h"
#include "griddyn/exciters/ExciterIEEEtype1.h"
#include "griddyn/exciters/ExciterSCRX.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModelClassical.h"
#include "griddyn/genmodels/GenModelGENROE.h"
#include "griddyn/genmodels/GenModelGENROU.h"
#include "griddyn/genmodels/GenModelGENSAE.h"
#include "griddyn/genmodels/GenModelGENSAL.h"
#include "griddyn/governors/GovernorGast.h"
#include "griddyn/governors/GovernorGgov1.h"
#include "griddyn/governors/GovernorHygov.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/governors/GovernorReheat.h"
#include "griddyn/governors/GovernorTgov1.h"
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
constexpr std::string_view andesTestDirectory{GRIDDYN_TEST_DIRECTORY "/andes_tests/"};

std::string makeAndesTestPath(std::string_view fileName)
{
    return std::string{andesTestDirectory} + std::string{fileName};
}

std::vector<double> runGeneratorSetpointStepCase(const std::vector<std::string_view>& dyrFiles,
                                                 double setpoint = 0.8)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    for (const auto dyrFile : dyrFiles) {
        griddyn::loadFile(simulation.get(), makeAndesTestPath(dyrFile));
    }

    auto* targetBus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
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
    EXPECT_GT(maximumControllerChange, 1.0e-9);
    return finalState;
}
}  // namespace

TEST(AndesDyrReaderTests, LoadsGenrouAndMatchesIeee14Initialization)
{
    std::ifstream input(makeAndesTestPath("andes_ieee14_genrou_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));

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

TEST(AndesDyrReaderTests, SkipsDyrHeaderCommentsBeforeRecordAccumulation)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou_with_comments.dyr"));

    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = bus->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* model = dynamic_cast<griddyn::genmodels::GenModelGENROU*>(generator->find("genmodel"));
    ASSERT_NE(model, nullptr);
}

TEST(AndesDyrReaderTests, LoadsGenclsInPsseAndesFieldOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_gencls.dyr"));

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

TEST(AndesDyrReaderTests, LoadsAndesKundurGenclsCase)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("andes_kundur_vsc_pflow.json"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("andes_kundur_gencls.dyr"));

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

TEST(AndesDyrReaderTests, MapsGensalParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_gensal.dyr"));

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

TEST(AndesDyrReaderTests, MapsGenroeAndIeeex1ParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genroe_ieeex1.dyr"));
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

TEST(AndesDyrReaderTests, InitializesGenroeAndIeeex1WithConsistentEquations)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genroe_ieeex1.dyr"));
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

TEST(AndesDyrReaderTests, RejectsMalformedGenroeAndUnsupportedIeeex1Switch)
{
    for (const auto record : {"ieee14_genroe_bad_fields.dyr", "ieee14_ieeex1_nonzero_switch.dyr"}) {
        auto simulation = std::make_unique<griddyn::GridDynSimulation>();
        griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
        EXPECT_THROW(griddyn::loadFile(simulation.get(), makeAndesTestPath(record)),
                     griddyn::InvalidParameterValue)
            << record;
    }
}

TEST(AndesDyrReaderTests, MapsGensaeAndEsst1aParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_gensae_esst1a.dyr"));
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
    EXPECT_DOUBLE_EQ(exciter->get("vos"), 1.0);
    EXPECT_DOUBLE_EQ(exciter->get("tb1"), 0.10);
    EXPECT_DOUBLE_EQ(exciter->get("tc1"), 0.02);
    EXPECT_DOUBLE_EQ(exciter->get("ilr"), 0.3);
    EXPECT_DOUBLE_EQ(exciter->get("klr"), 2.0);
    EXPECT_DOUBLE_EQ(exciter->get("kc"), 0.2);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, griddyn::cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, griddyn::cDaeSolverMode, false), 0);
}

TEST(AndesDyrReaderTests, MapsExpic1ParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_expic1.dyr"));
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

TEST(AndesDyrReaderTests, MapsScrxParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_scrx.dyr"));

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

TEST(AndesDyrReaderTests, MapsEsac6aParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_esac6a.dyr"));

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

TEST(AndesDyrReaderTests, MapsCanonicalDcAndTypeOneExciters)
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
        griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeAndesTestPath(record));
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
            EXPECT_DOUBLE_EQ(exciter->get("kc"), 0.2);
            EXPECT_DOUBLE_EQ(exciter->get("kd"), 0.3);
        }
    }
}

TEST(AndesDyrReaderTests, MapsEsst4bParametersAndCouplesToGensal)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_gensal.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_esst4b.dyr"));

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

TEST(AndesDyrReaderTests, MapsGastParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_gast.dyr"));

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

TEST(AndesDyrReaderTests, MapsGgov1ParametersInPsseDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_ggov1.dyr"));

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

TEST(AndesDyrReaderTests, MapsTgov1ParametersInAndesDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_tgov1.dyr"));

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

TEST(AndesDyrReaderTests, MapsIeesgoParametersAndInitializes)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_ieesgo.dyr"));

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

TEST(AndesDyrReaderTests, MapsHygovParametersInAndesDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_hygov.dyr"));

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

TEST(AndesDyrReaderTests, MapsIeeeG1ParametersInFrozenAndesDyrOrder)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_ieeeg1.dyr"));

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

TEST(AndesDyrReaderTests, ConnectsIeeeG1LowPressureOutputToSecondGenerator)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_ieeeg1_cross.dyr"));

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

TEST(AndesDyrReaderTests, ResolvesAlphanumericMachineIdsAcrossModelFamilies)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    generator->setName(bus->getName() + "_Gen_G1");

    EXPECT_NO_THROW(
        griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_alphanumeric_ids.dyr")));
    EXPECT_NE(dynamic_cast<griddyn::genmodels::GenModelGENROU*>(generator->find("genmodel")),
              nullptr);
    EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterSCRX*>(generator->find("exciter")), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::governors::GovernorTgov1*>(generator->find("governor")),
              nullptr);
    EXPECT_NE(dynamic_cast<griddyn::stabilizers::StabilizerST2CUT*>(generator->find("pss")),
              nullptr);
}

TEST(AndesDyrReaderTests, PrefersExactNumericMachineIdOverLegacyPosition)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* firstGenerator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(firstGenerator, nullptr);
    firstGenerator->setName(bus->getName() + "_Gen_G1");

    auto* exactIdGenerator = new griddyn::DynamicGenerator();
    exactIdGenerator->setName(bus->getName() + "_Gen_1");
    bus->add(exactIdGenerator);

    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_scrx.dyr"));
    EXPECT_EQ(firstGenerator->find("exciter"), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterSCRX*>(exactIdGenerator->find("exciter")),
              nullptr);
}

TEST(AndesDyrReaderTests, RetainsLegacyNumericMachinePositionFallback)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    auto* bus = dynamic_cast<griddyn::GridBus*>(simulation->findByUserID("bus", 1));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<griddyn::DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    generator->setName(bus->getName() + "_Gen_G1");

    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_scrx.dyr"));
    EXPECT_NE(dynamic_cast<griddyn::exciters::ExciterSCRX*>(generator->find("exciter")), nullptr);
}

TEST(AndesDyrReaderTests, ResolvesAlphanumericIeeeG1PrimaryAndSecondaryIds)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
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

    EXPECT_NO_THROW(griddyn::loadFile(simulation.get(),
                                      makeAndesTestPath("ieee14_ieeeg1_alphanumeric_ids.dyr")));
    auto* governor = dynamic_cast<griddyn::governors::GovernorIeeeG1*>(primary->find("governor"));
    ASSERT_NE(governor, nullptr);
    EXPECT_EQ(primary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerOutput(), griddyn::governors::GovernorIeeeG1::lpOutput);
}

TEST(AndesDyrReaderTests, LoadsEsst3aWithGenrouSignals)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_esst3a.dyr"));

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

TEST(AndesDyrReaderTests, MapsExst1ParametersAndCouplesToGenrou)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_exst1.dyr"));

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

TEST(AndesDyrReaderTests, MapsExacParameterRecordsAndCouplesToGenrou)
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
        griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
        griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
        griddyn::loadFile(simulation.get(), makeAndesTestPath(record));
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

TEST(AndesDyrReaderTests, LoadsExac1WithZeroTr)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_exac1_tr0.dyr"));

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

TEST(AndesDyrReaderTests, MapsSt2cutParametersAndCouplesToExciters)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_esst3a.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_exst1.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_st2cut.dyr"));

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

TEST(AndesDyrReaderTests, MapsIeeestParametersAndCouplesToExciter)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_esst3a.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_ieeest.dyr"));

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

TEST(AndesDynamicTests, Tgov1RespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_tgov1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, IeeeG1RespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_ieeeg1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, Esst3aRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_esst3a.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, Exst1RespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_exst1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, ExacExcitersRespondToGeneratorSetpointStep)
{
    for (const auto record :
         {"ieee14_exac1.dyr", "ieee14_exac2.dyr", "ieee14_exac4.dyr", "ieee14_esac1a_genrou.dyr"}) {
        const auto finalState = runGeneratorSetpointStepCase({record});
        EXPECT_FALSE(finalState.empty()) << record;
    }
}

TEST(AndesDynamicTests, ScrxRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_scrx.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, Esac6aRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase({"ieee14_esac6a.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, St2cutRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_esst3a.dyr", "ieee14_exst1.dyr", "ieee14_st2cut.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, IeeestRespondsToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_esst3a.dyr", "ieee14_exst1.dyr", "ieee14_ieeest.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, CombinedControllersRespondToGeneratorSetpointStep)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_tgov1.dyr", "ieee14_ieeeg1.dyr", "ieee14_esst3a.dyr", "ieee14_exst1.dyr"});
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, CombinedControllersRemainStableWithElevatedGeneratorSetpoint)
{
    const auto finalState = runGeneratorSetpointStepCase(
        {"ieee14_tgov1.dyr", "ieee14_ieeeg1.dyr", "ieee14_esst3a.dyr", "ieee14_exst1.dyr"}, 0.9);
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, ExciterControllersRemainStableWithElevatedGeneratorSetpoint)
{
    const auto finalState =
        runGeneratorSetpointStepCase({"ieee14_esst3a.dyr", "ieee14_exst1.dyr"}, 0.9);
    EXPECT_FALSE(finalState.empty());
}

TEST(AndesDynamicTests, Tgov1TrajectoryMatchesAndesReference)
{
    std::ifstream input(makeAndesTestPath("andes_ieee14_tgov1_trajectory_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_tgov1.dyr"));

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

TEST(AndesDynamicTests, Tgov1DownwardTrajectoryMatchesAndesReference)
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
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14.raw"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_genrou.dyr"));
    griddyn::loadFile(simulation.get(), makeAndesTestPath("ieee14_tgov1.dyr"));
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
