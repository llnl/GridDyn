/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for CoreObject object

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "formatInterpreters/jsonReaderElement.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/links/VSCShunt.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include <array>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

static constexpr std::string_view elementReaderTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                             "/element_reader_tests/"};
static constexpr std::string_view andesTestDirectory{GRIDDYN_TEST_DIRECTORY "/andes_tests/"};

static std::string makeElementReaderTestPath(std::string_view fileName)
{
    return std::string{elementReaderTestDirectory} + std::string{fileName};
}

static std::string makeAndesTestPath(std::string_view fileName)
{
    return std::string{andesTestDirectory} + std::string{fileName};
}

TEST(JsonElementReaderTests, JsonElementReaderTest1)
{
    JsonReaderElement reader;
    ASSERT_TRUE(reader.loadFile(makeElementReaderTestPath("json_test1.json")));
    EXPECT_TRUE(reader.isValid());
    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), makeElementReaderTestPath("json_test1.json"));

    auto sibling = firstChild->nextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto att1 = firstChild->getFirstAttribute();
    EXPECT_EQ(att1.getName(), "age");
    EXPECT_EQ(att1.getValue(), 25);
    EXPECT_EQ(att1.getText(), "25");
    auto att2 = firstChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "firstName");
    EXPECT_EQ(att2.getValue(), readerNullVal);
    EXPECT_EQ(att2.getText(), "John");
    att2 = firstChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "isAlive");
    EXPECT_EQ(att2.getText(), "true");
    att2 = firstChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "lastName");
    EXPECT_EQ(att2.getText(), "Smith");

    att2 = firstChild->getNextAttribute();
    EXPECT_FALSE(att2.isValid());

    firstChild->moveToFirstChild();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "address");
    firstChild->moveToNextSibling();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "phoneNumbers");
    firstChild->moveToNextSibling();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "phoneNumbers");
    firstChild->moveToNextSibling();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "phoneNumbers");
    firstChild->moveToNextSibling();
    EXPECT_FALSE(firstChild->isValid());
    firstChild->moveToParent();
    EXPECT_EQ(firstChild->getName(), makeElementReaderTestPath("json_test1.json"));
}

TEST(JsonElementReaderTests, JsonElementReaderTest2)
{
    JsonReaderElement reader;
    // test a bad file
    reader.loadFile(makeElementReaderTestPath("xmlElementReader_missing_file.xml"));
    std::cout
        << "NOTE:: this should have a message about a missing file >>testing bad file input\n";
    EXPECT_FALSE(reader.isValid());
    reader.loadFile(makeElementReaderTestPath("json_test2.json"));
    EXPECT_TRUE(reader.isValid());
    auto firstChild = reader.clone();

    auto sibling = firstChild->firstChild();
    EXPECT_EQ(sibling->getName(), "bus");
    auto cChild = sibling->nextSibling();
    EXPECT_FALSE(cChild->isValid());
    sibling->moveToNextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto busElement = firstChild->firstChild();

    EXPECT_EQ(busElement->getAttributeText("type"), "SLK");

    // Go through the children
    auto att1 = busElement->getFirstAttribute();
    EXPECT_EQ(att1.getName(), "angle");
    EXPECT_EQ(att1.getValue(), 0.0);
    att1 = busElement->getNextAttribute();
    EXPECT_EQ(att1.getName(), "name");
    EXPECT_EQ(att1.getText(), "bus1");
    att1 = busElement->getNextAttribute();
    att1 = busElement->getNextAttribute();
    EXPECT_EQ(att1.getName(), "voltage");
    EXPECT_EQ(att1.getText(), "1.04");
    EXPECT_NEAR(att1.getValue(), 1.04, 1e-6);

    auto busChild = busElement->firstChild();
    EXPECT_EQ(busChild->getName(), "generator");
    EXPECT_TRUE(busChild->getText().empty());
    auto att2 = busChild->getFirstAttribute();

    EXPECT_EQ(att2.getName(), "name");
    EXPECT_EQ(att2.getText(), "gen1");
    att2 = busChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "p");
    EXPECT_NEAR(att2.getValue(), 0.7160, 1e-5);

    // move busChild to the parent to make sure they are the same
    busChild->moveToParent();
    EXPECT_EQ(busChild->getName(), busElement->getName());

    // Now go back to the first child to do a few checks on attributes
    att1 = firstChild->getAttribute("name");
    EXPECT_EQ(att1.getName(), "name");
    EXPECT_EQ(att1.getText(), "test1");
}

TEST(JsonElementReaderTests, JsonElementReaderTest3)
{
    JsonReaderElement reader(makeElementReaderTestPath("xmlElementReader_test2.xml"));
    std::cout
        << "NOTE:: this should have a message indicating format error >>testing bad file input\n";
    EXPECT_FALSE(reader.isValid());
    reader.loadFile(makeElementReaderTestPath("json_test3.json"));
    EXPECT_TRUE(reader.isValid());
    // test traversal using move commands
    auto main = reader.clone();
    // bookmark the top level
    main->bookmark();
    main->moveToFirstChild("bus");
    main->moveToFirstChild();
    // traverse to the generator
    EXPECT_EQ(main->getName(), "generator");
    main->restore();
    // restore to the root
    EXPECT_TRUE(main->isDocument());
    // traverse to the second bus and check name
    main->moveToFirstChild("bus");
    main->moveToNextSibling("bus");
    EXPECT_TRUE(main->isValid());
    EXPECT_EQ(main->getAttributeText("name"), "bus2");
    main->moveToParent();
    main->moveToFirstChild();
    main->moveToFirstChild();
    // check we are in the generator now
    EXPECT_EQ(main->getAttributeText("name"), "gen1");
    // make a bookmark
    main->bookmark();
    // traverse to the second bus
    main->moveToParent();
    main->moveToNextSibling("bus");
    EXPECT_NEAR(main->getAttributeValue("voltage"), 1.01, 1e-7);
    // traverse to the parent
    main->moveToParent();
    // restore and check if we are in the generator again
    main->restore();
    EXPECT_NEAR(main->getAttributeValue("p"), 0.7160, 1e-7);
}

TEST(JsonElementReaderTests, JsonElementReaderTest4)
{
    /*auto reader = std::make_shared<JsonReaderElement>(xmlTestDirectory +
    "xmlElementReader_test3.xml"); EXPECT_EQ(reader->getName(), "main_element");

    auto main = reader->clone();
    reader = nullptr;
    EXPECT_EQ(main->getName(), "main_element");
    main->bookmark();
    main->moveToFirstChild();
    auto tstr = main->getMultiText(", ");
    EXPECT_EQ(tstr, "part1, part2, part3");
    main->moveToFirstChild();
    //att1 is 23t"  should return as not a value
    double val = main->getAttributeValue("att1");
    EXPECT_EQ(val, kNullVal);
    main->moveToFirstChild();

    val = main->getValue();
    EXPECT_EQ(val, kNullVal);
    EXPECT_EQ(main->getText(), "45.3echo");
    main->restore();
    EXPECT_EQ(main->getName(), "main_element");
    */
}

TEST(AndesDcReaderTests, ImportsAllAndesDcComponents)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("andes_dc_components.json"));

    EXPECT_NE(dynamic_cast<griddyn::DcBus*>(simulation->find("ground_node")), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::DcBus*>(simulation->find("node1")), nullptr);
    for (const auto* name : {"r", "l", "c", "rls", "rcp", "rlcp", "rcs", "rlcs"}) {
        EXPECT_NE(dynamic_cast<griddyn::links::DcLink*>(simulation->find(name)), nullptr) << name;
    }
    auto* vsc = dynamic_cast<griddyn::links::VSCShunt*>(simulation->find("vsc"));
    ASSERT_NE(vsc, nullptr);
    EXPECT_EQ(vsc->terminalCount(), 3U);
    ASSERT_NE(vsc->getBus(3), nullptr);
    EXPECT_EQ(vsc->getBus(3)->getName(), "ground_node");
}

TEST(AndesVSCShuntTests, MatchesAndesPqReferencePoint)
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
    converter->set("andes_current_balance", true);
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

TEST(AndesVSCShuntTests, MatchesKundurVsc2OperatingPoint)
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
    converter->set("andes_current_balance", true);
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

TEST(AndesPowerFlowTests, MatchesCapturedKundurVscReference)
{
    std::ifstream input(makeAndesTestPath("andes_kundur_vsc_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("andes_kundur_vsc_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    for (index_t index = 0; index < reference.at("bus_voltage").size(); ++index) {
        auto* bus =
            dynamic_cast<griddyn::AcBus*>(simulation->find("Bus_" + std::to_string(index + 1)));
        ASSERT_NE(bus, nullptr);
        EXPECT_NEAR(bus->get("voltage"), reference["bus_voltage"][index].get<double>(), tolerance);
        EXPECT_NEAR(bus->get("angle"), reference["bus_angle"][index].get<double>(), tolerance);
    }

    for (index_t index = 0; index < reference.at("node_voltage").size(); ++index) {
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

TEST(AndesPowerFlowTests, MatchesCapturedTwoBusReference)
{
    std::ifstream input(makeAndesTestPath("andes_two_bus_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("andes_two_bus_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    const std::array<std::string, 2> busNames{"slack_bus", "load_bus"};
    for (index_t index = 0; index < busNames.size(); ++index) {
        auto* bus = dynamic_cast<griddyn::AcBus*>(simulation->find(busNames[index]));
        ASSERT_NE(bus, nullptr);
        EXPECT_NEAR(bus->get("voltage"), reference["bus_voltage"][index].get<double>(), tolerance);
        EXPECT_NEAR(bus->get("angle"), reference["bus_angle"][index].get<double>(), tolerance);
    }
}

TEST(AndesPowerFlowTests, MatchesCapturedVscResistorReference)
{
    std::ifstream input(makeAndesTestPath("andes_vsc_resistor_pflow_reference.json"));
    ASSERT_TRUE(input.is_open());
    nlohmann::json reference;
    input >> reference;
    const auto tolerance = reference.at("tolerance").get<double>();

    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeAndesTestPath("andes_vsc_resistor_pflow.json"));
    ASSERT_EQ(simulation->powerflow(), 0);

    auto* acBus = dynamic_cast<griddyn::AcBus*>(simulation->find("ac_slack"));
    ASSERT_NE(acBus, nullptr);
    EXPECT_NEAR(acBus->get("voltage"), reference["bus_voltage"][0].get<double>(), tolerance);
    EXPECT_NEAR(acBus->get("angle"), reference["bus_angle"][0].get<double>(), tolerance);

    const std::array<std::string, 2> nodeNames{"dc_ground", "dc_node"};
    for (index_t index = 0; index < nodeNames.size(); ++index) {
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
