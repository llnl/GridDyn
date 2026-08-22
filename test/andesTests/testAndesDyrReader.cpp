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
    constexpr Tgov1Parameters expected[]{{1, 0.05, 0.05, 1.05, 0.3, 1.0, 2.1, 0.0},
                                         {6, 0.04, 0.06, 1.10, 0.25, 1.2, 2.3, 0.02},
                                         {8, 0.03, 0.07, 1.15, 0.2, 1.4, 2.5, 0.04}};

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
