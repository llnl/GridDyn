/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/exciters/ExciterESST3A.h"
#include "griddyn/exciters/ExciterEXST1.h"
#include "griddyn/genmodels/GenModelGENROU.h"
#include "griddyn/governors/GovernorTgov1.h"
#include <cstddef>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view andesTestDirectory{GRIDDYN_TEST_DIRECTORY "/andes_tests/"};

std::string makeAndesTestPath(std::string_view fileName)
{
    return std::string{andesTestDirectory} + std::string{fileName};
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
