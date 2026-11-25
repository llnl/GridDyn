/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "formatInterpreters/tomlReaderElement.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace {
constexpr char fixture[] = GRIDDYN_TEST_DIRECTORY "/element_reader_tests/tomlElementReader_test.toml";
}  // namespace

TEST(TomlElementReaderTests, LoadsFileAndPreservesAttributeSourceOrder)
{
    TomlReaderElement reader(fixture);
    ASSERT_TRUE(reader.isValid());
    EXPECT_TRUE(reader.isDocument());
    EXPECT_EQ(reader.getName(), fixture);
    EXPECT_EQ(reader.getValue(), readerNullVal);
    EXPECT_TRUE(reader.getText().empty());
    EXPECT_FALSE(reader.hasAttribute("network"));
    EXPECT_TRUE(reader.hasElement("network"));

    auto network = reader.firstChild("network");
    ASSERT_TRUE(network->isValid());
    EXPECT_FALSE(network->isDocument());
    EXPECT_EQ(network->getAttributeText("zname"), "demo");
    EXPECT_EQ(network->getAttributeText("enabled"), "true");
    EXPECT_EQ(network->getAttributeText("disabled"), "false");
    EXPECT_DOUBLE_EQ(network->getAttributeValue("rating"), 12.5);
    EXPECT_DOUBLE_EQ(network->getAttributeValue("count"), 2.0);
    EXPECT_DOUBLE_EQ(network->getAttributeValue("numeric_text"), 230.0);
    EXPECT_EQ(network->getAttributeValue("bad_number"), readerNullVal);
    EXPECT_FALSE(network->hasElement("count"));
    EXPECT_FALSE(network->getAttribute("missing").isValid());
    EXPECT_TRUE(network->getAttributeText("missing").empty());
    EXPECT_EQ(network->getAttributeValue("missing"), readerNullVal);

    for (const auto* name :
         {"zname", "enabled", "disabled", "rating", "count", "numeric_text", "bad_number"}) {
        const auto attribute = (std::string{name} == "zname") ? network->getFirstAttribute() :
                                                                network->getNextAttribute();
        ASSERT_TRUE(attribute.isValid());
        EXPECT_EQ(attribute.getName(), name);
    }
    EXPECT_FALSE(network->getNextAttribute().isValid());
    EXPECT_FALSE(network->getNextAttribute().isValid());
}

TEST(TomlElementReaderTests, TraversesArraysAndTablesInSourceOrder)
{
    TomlReaderElement reader(fixture);
    auto network = reader.firstChild("network");
    ASSERT_TRUE(network->isValid());
    auto bus = network->firstChild();
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getName(), "zbuses");
    EXPECT_EQ(bus->getAttributeText("name"), "bus1");
    bus->moveToNextSibling();
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getAttributeText("name"), "bus2");
    auto afterBuses = bus->nextSibling("zbuses");
    EXPECT_FALSE(afterBuses->isValid());
    bus->moveToNextSibling();
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getName(), "labels");
    EXPECT_EQ(bus->getText(), "alpha");
    EXPECT_EQ(bus->getMultiText("|"), "alpha");
    EXPECT_EQ(bus->getValue(), readerNullVal);
    EXPECT_FALSE(bus->hasAttribute("name"));
    bus->moveToNextSibling("labels");
    EXPECT_EQ(bus->getText(), "beta");
    bus->moveToNextSibling();
    EXPECT_EQ(bus->getName(), "numbers");
    EXPECT_DOUBLE_EQ(bus->getValue(), 1.0);
    bus->moveToNextSibling();
    EXPECT_DOUBLE_EQ(bus->getValue(), 2.5);
    bus->moveToNextSibling();
    EXPECT_DOUBLE_EQ(bus->getValue(), 3.0);
    bus->moveToNextSibling();
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getName(), "achild");
    EXPECT_EQ(bus->getAttributeText("name"), "last");
    bus->moveToNextSibling();
    EXPECT_FALSE(bus->isValid());
    bus->moveToParent();
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getName(), "network");

    // Named navigation must also position subsequent unnamed sibling traversal.
    auto numbers = network->firstChild("numbers");
    ASSERT_TRUE(numbers->isValid());
    numbers->moveToNextSibling("achild");
    EXPECT_EQ(numbers->getName(), "achild");
    numbers->moveToNextSibling();
    EXPECT_FALSE(numbers->isValid());
}

TEST(TomlElementReaderTests, PreservesImplicitAndExplicitTableOrder)
{
    TomlReaderElement reader;
    ASSERT_TRUE(reader.parse("[zouter.zinner]\nvalue = 1\n"
                             "[alpha]\nvalue = 2\n"
                             "[zouter.ainner]\nvalue = 3\n"));
    auto outer = reader.firstChild();
    ASSERT_TRUE(outer->isValid());
    EXPECT_EQ(outer->getName(), "zouter");
    auto inner = outer->firstChild();
    EXPECT_EQ(inner->getName(), "zinner");
    inner->moveToNextSibling();
    EXPECT_EQ(inner->getName(), "ainner");
    EXPECT_DOUBLE_EQ(inner->getAttributeValue("value"), 3.0);
    outer->moveToNextSibling();
    EXPECT_EQ(outer->getName(), "alpha");
}

TEST(TomlElementReaderTests, TraversesArrayOfTablesAndNestedChildren)
{
    TomlReaderElement reader;
    ASSERT_TRUE(reader.parse("[[buses]]\nname = 'first'\n"
                             "[buses.load]\npower = 12.5\n"
                             "[[buses]]\nname = 'second'\n"
                             "[buses.load]\npower = 7\n"));
    auto bus = reader.firstChild("buses");
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getAttributeText("name"), "first");
    bus->moveToFirstChild("load");
    ASSERT_TRUE(bus->isValid());
    EXPECT_DOUBLE_EQ(bus->getAttributeValue("power"), 12.5);
    bus->moveToParent();
    bus->moveToNextSibling("buses");
    ASSERT_TRUE(bus->isValid());
    EXPECT_EQ(bus->getAttributeText("name"), "second");
    auto load = bus->firstChild("load");
    ASSERT_TRUE(load->isValid());
    EXPECT_DOUBLE_EQ(load->getAttributeValue("power"), 7.0);
    bus->moveToNextSibling();
    EXPECT_FALSE(bus->isValid());
    bus->moveToParent();
    EXPECT_TRUE(bus->isDocument());
}

TEST(TomlElementReaderTests, KeepsCloneAndBookmarkDocumentsAliveAcrossReloads)
{
    std::shared_ptr<ReaderElement> clone;
    {
        TomlReaderElement reader(fixture);
        reader.moveToFirstChild("network");
        EXPECT_EQ(reader.getFirstAttribute().getName(), "zname");
        clone = reader.clone();
        reader.bookmark();
        reader.moveToFirstChild("zbuses");
        ASSERT_TRUE(reader.parse("fresh = 42"));
        EXPECT_TRUE(reader.isDocument());
        EXPECT_DOUBLE_EQ(reader.getAttributeValue("fresh"), 42.0);
        reader.restore();
        ASSERT_TRUE(reader.isValid());
        EXPECT_FALSE(reader.isDocument());
        EXPECT_EQ(reader.getName(), "network");
        EXPECT_EQ(reader.getNextAttribute().getName(), "enabled");
        reader.moveToFirstChild("zbuses");
        reader.moveToNextSibling();
        EXPECT_EQ(reader.getAttributeText("name"), "bus2");
    }
    ASSERT_TRUE(clone->isValid());
    EXPECT_EQ(clone->getNextAttribute().getName(), "enabled");
    auto bus = clone->firstChild("zbuses");
    EXPECT_EQ(bus->getAttributeText("name"), "bus1");
    bus->moveToNextSibling();
    EXPECT_EQ(bus->getAttributeText("name"), "bus2");
    EXPECT_EQ(clone->getName(), "network");
}

TEST(TomlElementReaderTests, MissingChildrenAndEmptyContainersPreserveParent)
{
    TomlReaderElement reader;
    ASSERT_TRUE(reader.parse("empty = []\nitems = [{}, { name = 'present' }, {}]\n"
                             "[empty_table]\n"));
    EXPECT_FALSE(reader.hasElement("empty"));
    EXPECT_FALSE(reader.hasElement("empty_table"));
    auto missing = reader.firstChild("missing");
    EXPECT_FALSE(missing->isValid());
    missing->moveToParent();
    ASSERT_TRUE(missing->isValid());
    EXPECT_TRUE(missing->isDocument());
    auto items = missing->firstChild("items");
    ASSERT_TRUE(items->isValid());
    EXPECT_EQ(items->getAttributeText("name"), "present");
    items->moveToFirstChild();
    EXPECT_FALSE(items->isValid());
    items->moveToParent();
    ASSERT_TRUE(items->isValid());
    EXPECT_EQ(items->getAttributeText("name"), "present");
    items->moveToNextSibling();
    EXPECT_FALSE(items->isValid());
}

TEST(TomlElementReaderTests, ReportsParseAndFileErrorsAndCanRecover)
{
    TomlReaderElement reader;
    EXPECT_FALSE(reader.isValid());
    EXPECT_TRUE(reader.getName().empty());
    EXPECT_FALSE(reader.clone()->isValid());
    EXPECT_FALSE(reader.parse("[broken"));
    EXPECT_FALSE(reader.isValid());
    EXPECT_FALSE(reader.isDocument());
    ASSERT_TRUE(reader.loadFile(fixture));
    reader.moveToFirstChild("network");
    EXPECT_FALSE(reader.parse("duplicate = 1\nduplicate = 2"));
    EXPECT_FALSE(reader.isValid());
    reader.moveToParent();
    EXPECT_FALSE(reader.isValid());
    EXPECT_FALSE(reader.loadFile(std::string{fixture} + ".missing"));
    EXPECT_FALSE(reader.loadFile(std::string{GRIDDYN_TEST_DIRECTORY} +
                                 "/element_reader_tests/tomlElementReader_invalid.toml"));
    ASSERT_TRUE(reader.parse("value = 0xFF\ntext = '''two\nlines'''"));
    EXPECT_DOUBLE_EQ(reader.getAttributeValue("value"), 255.0);
    EXPECT_EQ(reader.getAttributeText("text"), "two\nlines");
}
