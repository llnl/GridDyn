/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Tests for YAML element parsing and navigation.

#include "fileInput/fileInput.h"
#include "formatInterpreters/yamlReaderElement.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

static constexpr std::string_view yamlElementReaderTestDirectory{
    GRIDDYN_TEST_DIRECTORY "/element_reader_tests/"};
static constexpr std::string_view yamlReaderErrorTestDirectory{
    GRIDDYN_TEST_DIRECTORY "/reader_error_tests/"};

static std::string makeYamlElementReaderTestPath(std::string_view directory,
                                                std::string_view fileName)
{
    return std::string{directory} + std::string{fileName};
}

TEST(YamlElementReaderTests, LoadsAndNavigatesYamlMappingsAndSequences)
{
    const auto fixture = makeYamlElementReaderTestPath(yamlElementReaderTestDirectory,
                                                       "yamlElementReader_test.yaml");
    YamlReaderElement reader(fixture);
    ASSERT_TRUE(reader.isValid());
    EXPECT_TRUE(reader.isDocument());
    EXPECT_EQ(reader.getName(), fixture);
    EXPECT_EQ(reader.getValue(), readerNullVal);
    EXPECT_TRUE(reader.getText().empty());
    EXPECT_TRUE(reader.getMultiText().empty());
    EXPECT_FALSE(reader.hasAttribute("griddyn"));
    EXPECT_TRUE(reader.hasElement("griddyn"));
    EXPECT_FALSE(reader.hasElement("missing"));

    auto root = reader.firstChild("griddyn");
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(root->isValid());
    EXPECT_FALSE(root->isDocument());
    EXPECT_EQ(root->getName(), "griddyn");
    EXPECT_EQ(root->getAttributeText("name"), "demo");
    const auto nameAttribute = root->getAttribute("name");
    EXPECT_EQ(nameAttribute.getName(), "name");
    EXPECT_EQ(nameAttribute.getText(), "demo");
    EXPECT_EQ(root->getAttributeValue("version"), readerNullVal);
    EXPECT_FALSE(root->hasAttribute("network"));
    EXPECT_TRUE(root->hasElement("network"));
    EXPECT_FALSE(root->hasElement("name"));
    auto scalarChild = root->firstChild("name");
    ASSERT_NE(scalarChild, nullptr);
    EXPECT_FALSE(scalarChild->isValid());
    scalarChild->moveToParent();
    EXPECT_EQ(scalarChild->getName(), "griddyn");

    auto rootAttribute = root->getFirstAttribute();
    ASSERT_TRUE(rootAttribute.isValid());
    EXPECT_EQ(rootAttribute.getName(), "name");
    rootAttribute = root->getNextAttribute();
    ASSERT_TRUE(rootAttribute.isValid());
    EXPECT_EQ(rootAttribute.getName(), "version");
    EXPECT_FALSE(root->getNextAttribute().isValid());
    EXPECT_FALSE(root->getAttribute("missing").isValid());
    EXPECT_TRUE(root->getAttributeText("missing").empty());
    EXPECT_EQ(root->getAttributeValue("missing"), readerNullVal);

    auto network = root->firstChild();
    ASSERT_TRUE(network->isValid());
    EXPECT_EQ(network->getName(), "network");
    EXPECT_EQ(network->getAttributeText("name"), "sample_grid");
    EXPECT_EQ(network->getAttributeValue("enabled"), 1.0);
    EXPECT_NEAR(network->getAttributeValue("rating"), 12.5, 1e-12);
    EXPECT_EQ(network->getAttributeValue("bad_number"), readerNullVal);
    EXPECT_FALSE(network->hasElement("missing"));

    auto networkAttribute = network->getFirstAttribute();
    ASSERT_TRUE(networkAttribute.isValid());
    EXPECT_EQ(networkAttribute.getName(), "name");
    EXPECT_EQ(networkAttribute.getText(), "sample_grid");
    networkAttribute = network->getNextAttribute();
    ASSERT_TRUE(networkAttribute.isValid());
    EXPECT_EQ(networkAttribute.getName(), "enabled");
    networkAttribute = network->getNextAttribute();
    ASSERT_TRUE(networkAttribute.isValid());
    EXPECT_EQ(networkAttribute.getName(), "rating");
    EXPECT_DOUBLE_EQ(networkAttribute.getValue(), 12.5);
    networkAttribute = network->getNextAttribute();
    ASSERT_TRUE(networkAttribute.isValid());
    EXPECT_EQ(networkAttribute.getName(), "bad_number");
    EXPECT_FALSE(network->getNextAttribute().isValid());

    auto buses = network->firstChild();
    ASSERT_TRUE(buses->isValid());
    EXPECT_EQ(buses->getName(), "buses");
    EXPECT_TRUE(buses->getText().empty());
    EXPECT_TRUE(buses->getMultiText("|").empty());
    EXPECT_FALSE(buses->hasAttribute("buses"));
    EXPECT_TRUE(buses->hasAttribute("name"));
    EXPECT_EQ(buses->getAttributeText("name"), "bus1");
    EXPECT_NEAR(buses->getAttributeValue("basekv"), 230.0, 1e-12);

    auto secondBus = buses->nextSibling();
    ASSERT_TRUE(secondBus->isValid());
    EXPECT_EQ(secondBus->getName(), "buses");
    EXPECT_EQ(secondBus->getAttributeText("name"), "bus2");
    EXPECT_NEAR(secondBus->getAttributeValue("basekv"), 115.0, 1e-12);
    auto nextMappingChild = secondBus->nextSibling();
    ASSERT_TRUE(nextMappingChild->isValid());
    EXPECT_EQ(nextMappingChild->getName(), "labels");
    auto afterBuses = secondBus->nextSibling("buses");
    EXPECT_FALSE(afterBuses->isValid());

    // A sequence whose entries are mappings is exposed as a child but has no scalar text.
    EXPECT_FALSE(buses->firstChild()->isValid());
    buses->moveToFirstChild();
    EXPECT_FALSE(buses->isValid());
    buses->moveToParent();
    ASSERT_TRUE(buses->isValid());
    EXPECT_EQ(buses->getAttributeText("name"), "bus1");

    auto labels = buses->nextSibling("labels");
    ASSERT_TRUE(labels->isValid());
    EXPECT_EQ(labels->getName(), "labels");
    EXPECT_EQ(labels->getText(), "alpha");
    EXPECT_EQ(labels->getValue(), readerNullVal);
    EXPECT_EQ(labels->getMultiText(" | "), "alpha | beta | gamma");
    EXPECT_FALSE(labels->hasAttribute("name"));
    auto beta = labels->nextSibling("labels");
    ASSERT_TRUE(beta->isValid());
    EXPECT_EQ(beta->getText(), "beta");
    auto gamma = beta->nextSibling();
    ASSERT_TRUE(gamma->isValid());
    EXPECT_EQ(gamma->getText(), "gamma");
    EXPECT_FALSE(gamma->nextSibling("labels")->isValid());

    auto child = labels->nextSibling("child");
    ASSERT_TRUE(child->isValid());
    EXPECT_EQ(child->getName(), "child");
    EXPECT_EQ(child->getValue(), readerNullVal);
    auto words = child->firstChild("words");
    ASSERT_TRUE(words->isValid());
    EXPECT_EQ(words->getMultiText(", "), "part1, part2, part3");

    network->bookmark();
    network->moveToFirstChild("child");
    ASSERT_TRUE(network->isValid());
    EXPECT_EQ(network->getName(), "child");
    network->restore();
    EXPECT_EQ(network->getName(), "network");
    network->restore();  // Restoring without a bookmark is a no-op.
    EXPECT_EQ(network->getName(), "network");

    auto missingChild = network->firstChild("absent");
    ASSERT_NE(missingChild, nullptr);
    EXPECT_FALSE(missingChild->isValid());
    missingChild->moveToParent();
    EXPECT_TRUE(missingChild->isValid());
    EXPECT_EQ(missingChild->getName(), "network");

    // Successful parsing from a string supports a scalar root as well as file mappings.
    YamlReaderElement stringReader;
    ASSERT_TRUE(stringReader.parse("labels: [one, two]"));
    ASSERT_TRUE(stringReader.isValid());
    EXPECT_TRUE(stringReader.isDocument());
    EXPECT_EQ(stringReader.getName(), "string");
    auto stringLabels = stringReader.firstChild("labels");
    ASSERT_TRUE(stringLabels->isValid());
    EXPECT_EQ(stringLabels->getMultiText("/"), "one/two");
    ASSERT_TRUE(stringReader.parse("standalone"));
    EXPECT_EQ(stringReader.getText(), "standalone");
    EXPECT_EQ(stringReader.getValue(), readerNullVal);
    ASSERT_TRUE(stringReader.parse("12.5"));
    EXPECT_DOUBLE_EQ(stringReader.getValue(), 12.5);
}

TEST(YamlElementReaderErrorTests, RejectsMalformedAndMissingYamlFiles)
{
    YamlReaderElement reader;
    EXPECT_FALSE(reader.isValid());
    EXPECT_FALSE(reader.isDocument());
    EXPECT_EQ(reader.getValue(), readerNullVal);
    EXPECT_TRUE(reader.getText().empty());
    EXPECT_FALSE(reader.hasAttribute("name"));
    EXPECT_FALSE(reader.getFirstAttribute().isValid());
    EXPECT_FALSE(reader.getNextAttribute().isValid());

    ASSERT_TRUE(reader.parse("valid: [1, 2]"));
    const auto malformed = makeYamlElementReaderTestPath(yamlReaderErrorTestDirectory,
                                                         "malformed.yaml");
    ASSERT_TRUE(std::filesystem::is_regular_file(malformed)) << malformed;
    EXPECT_FALSE(reader.loadFile(malformed));
    EXPECT_FALSE(reader.isValid());
    EXPECT_FALSE(reader.isDocument());

    ASSERT_TRUE(reader.parse("valid: [1, 2]"));
    EXPECT_FALSE(reader.loadFile(makeYamlElementReaderTestPath(yamlReaderErrorTestDirectory,
                                                               "missing.yaml")));
    EXPECT_FALSE(reader.isValid());
    EXPECT_TRUE(reader.getText().empty());
    EXPECT_FALSE(reader.getFirstAttribute().isValid());

    ASSERT_TRUE(reader.parse("valid: [1, 2]"));
    EXPECT_FALSE(reader.parse("broken: [unterminated"));
    EXPECT_FALSE(reader.isValid());
    EXPECT_FALSE(reader.isDocument());
    EXPECT_EQ(reader.getValue(), readerNullVal);

    ASSERT_TRUE(reader.parse("recovered: {rating: 3}"));
    EXPECT_DOUBLE_EQ(reader.firstChild("recovered")->getAttributeValue("rating"), 3.0);
}

TEST(YamlElementReaderTests, ImportsNamedBusesLoadAndLinkThroughLoadFile)
{
    const auto fixture = std::filesystem::path{GRIDDYN_TEST_DIRECTORY} / "input_tests" /
        "yaml_two_bus.yaml";
    ASSERT_TRUE(std::filesystem::is_regular_file(fixture)) << fixture;
    griddyn::GridDynSimulation simulation;
    ASSERT_NO_THROW(griddyn::loadFile(&simulation, fixture.string()));

    EXPECT_EQ(simulation.getName(), "yaml_two_bus");
    ASSERT_EQ(simulation.getInt("totalbuscount"), 2);
    ASSERT_EQ(simulation.getInt("totallinkcount"), 1);
    const auto* source = dynamic_cast<const griddyn::GridBus*>(simulation.find("source"));
    const auto* demand = dynamic_cast<const griddyn::GridBus*>(simulation.find("demand"));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(demand, nullptr);
    EXPECT_DOUBLE_EQ(source->get("basevoltage"), 230.0);
    EXPECT_DOUBLE_EQ(demand->get("basevoltage"), 230.0);
    const auto* load = demand->getLoad(0);
    ASSERT_NE(load, nullptr);
    EXPECT_EQ(load->getName(), "demand_load");
    EXPECT_DOUBLE_EQ(load->get("p"), 0.25);
    EXPECT_DOUBLE_EQ(load->get("q"), 0.05);
    const auto* line = simulation.getLink(0);
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->getBus(1), source);
    EXPECT_EQ(line->getBus(2), demand);
    EXPECT_DOUBLE_EQ(line->get("x"), 0.08);
}
