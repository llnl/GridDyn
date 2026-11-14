/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "griddyn/links/VSCShunt.h"
#include "griddyn/links/ZBreaker.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace {

std::string makeComparisonTestPath(std::string_view fileName)
{
    return std::string{GRIDDYN_TEST_DIRECTORY "/comparison_tests/"} + std::string{fileName};
}

}  // namespace

TEST(VSCShuntComparisonTests, MatchesAndesPqReferencePoint)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    auto* acBus = new griddyn::AcBus("ac");
    acBus->set("type", "swing");
    acBus->set("voltage", 1.0);
    acBus->set("angle", 0.0);
    auto* dcBus = new griddyn::DcBus("dc_positive");
    dcBus->set("type", "swing");
    dcBus->set("voltage", 1.0);
    auto* dcReference = new griddyn::DcBus("dc_reference");
    dcReference->set("type", "swing");
    dcReference->set("voltage", 0.0);
    auto* converter = new griddyn::links::VSCShunt("vsc");
    converter->set("r", 0.0025);
    converter->set("x", 0.06);
    converter->set("control", 0.0);  // ANDES PQ mode
    converter->set("p0", -0.1);
    converter->set("q0", 0.0);
    converter->set("andes_current_balance", 1.0);
    converter->updateBus(acBus, 1);
    converter->updateBus(dcBus, 2);
    converter->updateBus(dcReference, 3);
    simulation->add(acBus);
    simulation->add(dcBus);
    simulation->add(dcReference);
    simulation->add(converter);

    EXPECT_EQ(simulation->powerflow(), 0);
    // ANDES VSCShunt with rsh=0.0025, xsh=0.06, p0=-0.1 and q0=0
    // yields pdc=-0.100025 at Vdc=1.  The DC terminals see -pdc/Vdc
    // and pdc/Vdc, respectively.  These reference values are evaluated
    // from Andes' solved VSCShunt equations.
    EXPECT_NEAR(converter->getRealPower(2), 0.100025, 1e-7);
    EXPECT_NEAR(converter->getRealPower(3), -0.100025, 1e-7);
    EXPECT_NEAR(converter->getRealPower(1), 0.1, 1e-9);
    EXPECT_NEAR(converter->getReactivePower(1), 0.0, 1e-9);
}

TEST(VSCShuntComparisonTests, MatchesKundurVsc2OperatingPoint)
{
    // Boundary values and expected current were obtained from a one-worker
    // Andes PFlow run of andes/cases/kundur/kundur_vsc.json.  Pinning the
    // external AC and DC voltages makes this a component-level comparison
    // while the AC network importer is still being completed.
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    auto* acBus = new griddyn::AcBus("ac");
    acBus->set("type", "swing");
    acBus->set("voltage", 0.968718231728735);
    acBus->set("angle", 0.2893279182804385);
    auto* dcBus = new griddyn::DcBus("dc_positive");
    dcBus->set("type", "swing");
    dcBus->set("voltage", 0.9989989369366359);
    auto* dcReference = new griddyn::DcBus("dc_reference");
    dcReference->set("type", "swing");
    dcReference->set("voltage", 0.0);
    auto* converter = new griddyn::links::VSCShunt("vsc");
    // Andes scales z=True parameters from VSC_2's 110 kV base to the
    // 230 kV AC-bus base before evaluating its VSC equations.
    converter->set("r", 0.0005718336483931947);
    converter->set("x", 0.013724007561436672);
    converter->set("control", 0.0);  // Andes VSC_2, PQ mode
    converter->set("p0", -0.1);
    converter->set("q0", 0.0);
    converter->set("andes_current_balance", 1.0);
    converter->updateBus(acBus, 1);
    converter->updateBus(dcBus, 2);
    converter->updateBus(dcReference, 3);
    simulation->add(acBus);
    simulation->add(dcBus);
    simulation->add(dcReference);
    simulation->add(converter);

    ASSERT_EQ(simulation->powerflow(), 0);
    // Andes: pdc=-0.10000609361072836, so -pdc / Vdc = 0.100106309...
    EXPECT_NEAR(converter->getRealPower(2), 0.100106309, 1e-7);
    EXPECT_NEAR(converter->getRealPower(3), -0.100106309, 1e-7);
    EXPECT_NEAR(converter->getRealPower(1), 0.1, 1e-9);
    EXPECT_NEAR(converter->getReactivePower(1), 0.0, 1e-9);
}

TEST(PowerFlowComparisonTests, MatchesCapturedKundurVscReference)
{
    std::ifstream input(makeComparisonTestPath("kundur_vsc_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("kundur_vsc_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    for (std::size_t index = 0; index < reference.at("bus_voltage").size(); ++index) {
        auto* bus =
            dynamic_cast<griddyn::AcBus*>(simulation->find("Bus_" + std::to_string(index + 1)));
        ASSERT_NE(bus, nullptr);
        EXPECT_NEAR(bus->get("voltage"), reference["bus_voltage"][index].get<double>(), tolerance);
        EXPECT_NEAR(bus->get("angle"), reference["bus_angle"][index].get<double>(), tolerance);
    }

    for (std::size_t index = 0; index < reference.at("node_voltage").size(); ++index) {
        auto* node =
            dynamic_cast<griddyn::DcBus*>(simulation->find("Node_" + std::to_string(index)));
        ASSERT_NE(node, nullptr);
        EXPECT_NEAR(node->get("voltage"),
                    reference["node_voltage"][index].get<double>(),
                    tolerance);
    }

    for (index_t index = 0; index < 2; ++index) {
        auto* converter = dynamic_cast<griddyn::links::VSCShunt*>(
            simulation->find("VSC_" + std::to_string(index + 1)));
        ASSERT_NE(converter, nullptr);
        EXPECT_NEAR(converter->getRealPower(1),
                    -reference["vsc_psh"][index].get<double>(),
                    tolerance);
        EXPECT_NEAR(converter->getReactivePower(1),
                    -reference["vsc_qsh"][index].get<double>(),
                    tolerance);
        const auto dcVoltage = reference["node_voltage"][index + 1].get<double>();
        EXPECT_NEAR(converter->getRealPower(2),
                    -reference["vsc_pdc"][index].get<double>() / dcVoltage,
                    tolerance);
    }
}

TEST(PowerFlowComparisonTests, MatchesCapturedTwoBusReference)
{
    std::ifstream input(makeComparisonTestPath("two_bus_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("two_bus_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    const std::array<std::string, 2> busNames{"slack_bus", "load_bus"};
    for (std::size_t index = 0; index < busNames.size(); ++index) {
        auto* bus = dynamic_cast<griddyn::AcBus*>(simulation->find(busNames[index]));
        ASSERT_NE(bus, nullptr);
        EXPECT_NEAR(bus->get("voltage"), reference["bus_voltage"][index].get<double>(), tolerance);
        EXPECT_NEAR(bus->get("angle"), reference["bus_angle"][index].get<double>(), tolerance);
    }
}

TEST(PowerFlowComparisonTests, MatchesCapturedShuntReference)
{
    std::ifstream input(makeComparisonTestPath("shunt_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("shunt_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    const std::array<std::string, 2> busNames{"slack_bus", "shunt_bus"};
    for (std::size_t index = 0; index < busNames.size(); ++index) {
        auto* bus = dynamic_cast<griddyn::AcBus*>(simulation->find(busNames[index]));
        ASSERT_NE(bus, nullptr);
        EXPECT_NEAR(bus->get("voltage"), reference["bus_voltage"][index].get<double>(), tolerance);
        EXPECT_NEAR(bus->get("angle"), reference["bus_angle"][index].get<double>(), tolerance);
    }

    auto* shunt = dynamic_cast<griddyn::ZipLoad*>(simulation->find("shunt_bus::fixed_shunt"));
    ASSERT_NE(shunt, nullptr);
    EXPECT_NEAR(shunt->get("yp"), reference["shunt_yp"].get<double>(), tolerance);
    EXPECT_NEAR(shunt->get("yq"), reference["shunt_yq"].get<double>(), tolerance);
    EXPECT_NEAR(shunt->getRealPower(), reference["shunt_p"].get<double>(), tolerance);
    EXPECT_NEAR(shunt->getReactivePower(), reference["shunt_q"].get<double>(), tolerance);
}

TEST(PowerFlowComparisonTests, MatchesCapturedJumperReference)
{
    std::ifstream input(makeComparisonTestPath("jumper_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("jumper_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    const std::array<std::string, 3> busNames{"slack_bus", "jumper_bus", "load_bus"};
    for (std::size_t index = 0; index < busNames.size(); ++index) {
        auto* bus = dynamic_cast<griddyn::AcBus*>(simulation->find(busNames[index]));
        ASSERT_NE(bus, nullptr);
        EXPECT_NEAR(bus->get("voltage"), reference["bus_voltage"][index].get<double>(), tolerance);
        EXPECT_NEAR(bus->get("angle"), reference["bus_angle"][index].get<double>(), tolerance);
    }

    auto* active = dynamic_cast<griddyn::links::ZBreaker*>(simulation->find("active_jumper"));
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->isEnabled());
    ASSERT_NE(active->getBus(1), nullptr);
    ASSERT_NE(active->getBus(2), nullptr);
    EXPECT_EQ(active->getBus(1)->getName(), "slack_bus");
    EXPECT_EQ(active->getBus(2)->getName(), "jumper_bus");
    EXPECT_NEAR(active->getBus(1)->get("voltage"), active->getBus(2)->get("voltage"), tolerance);
    EXPECT_NEAR(active->getBus(1)->get("angle"), active->getBus(2)->get("angle"), tolerance);

    auto* inactive = dynamic_cast<griddyn::links::ZBreaker*>(simulation->find("inactive_jumper"));
    ASSERT_NE(inactive, nullptr);
    EXPECT_FALSE(inactive->isEnabled());
    EXPECT_GT(std::abs(reference["bus_voltage"][0].get<double>() -
                       reference["bus_voltage"][2].get<double>()),
              tolerance);
}

TEST(PowerFlowComparisonTests, MatchesCapturedVscResistorReference)
{
    std::ifstream input(makeComparisonTestPath("vsc_resistor_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("vsc_resistor_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    auto* acBus = dynamic_cast<griddyn::AcBus*>(simulation->find("ac_slack"));
    ASSERT_NE(acBus, nullptr);
    EXPECT_NEAR(acBus->get("voltage"), reference["bus_voltage"][0].get<double>(), tolerance);
    EXPECT_NEAR(acBus->get("angle"), reference["bus_angle"][0].get<double>(), tolerance);

    const std::array<std::string, 2> nodeNames{"dc_ground", "dc_node"};
    for (std::size_t index = 0; index < nodeNames.size(); ++index) {
        auto* node = dynamic_cast<griddyn::DcBus*>(simulation->find(nodeNames[index]));
        ASSERT_NE(node, nullptr);
        EXPECT_NEAR(node->get("voltage"),
                    reference["node_voltage"][index].get<double>(),
                    tolerance);
    }

    auto* converter = dynamic_cast<griddyn::links::VSCShunt*>(simulation->find("vsc"));
    ASSERT_NE(converter, nullptr);
    EXPECT_NEAR(converter->getRealPower(1), -reference["vsc_psh"][0].get<double>(), tolerance);
    EXPECT_NEAR(converter->getReactivePower(1), -reference["vsc_qsh"][0].get<double>(), tolerance);
    const auto dcVoltage = reference["node_voltage"][1].get<double>();
    EXPECT_NEAR(converter->getRealPower(2),
                -reference["vsc_pdc"][0].get<double>() / dcVoltage,
                tolerance);
}
