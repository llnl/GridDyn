/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for element readers

#include "../gtestHelper.h"
#include "formatInterpreters/XmlReaderElement.h"
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>

static const std::string elementReaderTestDirectory(GRIDDYN_TEST_DIRECTORY
                                                    "/element_reader_tests/");

TEST(ElementReaderTests, TinyxmlElementReaderTest1)
{
    XmlReaderElement reader;
    ASSERT_TRUE(reader.loadFile(elementReaderTestDirectory + "xmlElementReader_test.xml"));
    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), "griddyn");
    auto sibling = firstChild->nextSibling();
    EXPECT_EQ(sibling, nullptr);
    sibling = firstChild->firstChild();
    EXPECT_EQ(sibling->getName(), "bus");
    auto cChild = sibling->nextSibling();
    EXPECT_EQ(cChild, nullptr);
    sibling->moveToNextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto busElement = firstChild->firstChild();
    auto busChild = busElement->firstChild("type");
    ASSERT_NE(busChild, nullptr);
    EXPECT_EQ(busChild->getName(), "type");

    auto val = busChild->getText();
    EXPECT_EQ(val, "SLK");

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "angle");
    EXPECT_EQ(busChild->getValue(), 0.0);
    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "voltage");
    EXPECT_EQ(busChild->getText(), "1.04");
    EXPECT_NEAR(busChild->getValue(), 1.04, 1e-6);

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "generator");
    EXPECT_TRUE(busChild->getText().empty());
    auto genChild = busChild->firstChild();
    EXPECT_EQ(genChild->getName(), "P");
    EXPECT_NEAR(genChild->getValue(), 0.7160, 1e-5);

    auto att = busChild->getFirstAttribute();
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "gen1");
    busChild->moveToParent();
    EXPECT_EQ(busChild->getName(), busElement->getName());

    att = firstChild->getAttribute("name");
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "test1");
}

TEST(ElementReaderTests, TinyxmlElementReaderTest2)
{
    XmlReaderElement reader;
    reader.loadFile(elementReaderTestDirectory + "xmlElementReader_testbbad.xml");
    std::cout << "NOTE:: this should have a message testing bad xml input and not fault\n";
    EXPECT_FALSE(reader.isValid());
    reader.loadFile(elementReaderTestDirectory + "xmlElementReader_test2.xml");
    EXPECT_TRUE(reader.isValid());

    auto main = reader.clone();
    auto sub = main->firstChild();
    for (int kk = 0; kk < 8; ++kk) {
        sub->moveToNextSibling();
    }
    auto name = sub->getName();
    EXPECT_EQ(name, "elementWithAttributes");
    auto att = sub->getFirstAttribute();
    EXPECT_EQ(att.getText(), "A");
    att = sub->getNextAttribute();
    EXPECT_EQ(att.getValue(), 46);
    att = sub->getNextAttribute();
    EXPECT_NEAR(att.getValue(), 21.345, 1e-6);
    att = sub->getNextAttribute();
    EXPECT_EQ(att.getValue(), readerNullVal);
    EXPECT_EQ(att.getText(), "happy");
    att = sub->getNextAttribute();
    EXPECT_FALSE(att.isValid());
}

TEST(ElementReaderTests, TinyxmlElementReaderTest3)
{
    XmlReaderElement reader(elementReaderTestDirectory + "xmlElementReader_test2.xml");
    auto main = reader.clone();

    main->moveToFirstChild("subelementA");
    EXPECT_EQ(main->getName(), "subelementA");
    main->moveToNextSibling("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementA");
    EXPECT_FALSE(main->isValid());
    main->moveToParent();
    EXPECT_EQ(main->getName(), "main_element");
    main->moveToFirstChild("subelementB");
    EXPECT_EQ(main->getName(), "subelementB");
    main->moveToNextSibling("subelementB");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementB");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementB");
    EXPECT_EQ(main->getName(), "subelementB");
    main->moveToNextSibling("subelementB");
    EXPECT_FALSE(main->isValid());

    main->moveToParent();
    main->moveToFirstChild("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("elementWithAttributes");
    EXPECT_TRUE(main->isValid());

    main->moveToFirstChild("badElementName");
    EXPECT_FALSE(main->isValid());
    main->moveToParent();
    EXPECT_TRUE(main->isValid());
    main->moveToFirstChild("badElementName");
    main->moveToFirstChild("badElementName");
    main->moveToParent();
    EXPECT_TRUE(main->isValid());
    main->moveToParent();
    EXPECT_TRUE(main->isValid());
    auto name = main->getName();
    EXPECT_EQ(name, "main_element");
    main->moveToParent();

    EXPECT_TRUE(main->isDocument());
}

TEST(ElementReaderTests, TinyxmlElementReaderTestParse)
{
    std::string XMLtestString = R"xml(<?xml version="1.0" encoding="utf - 8"?>
<!--xml file to test the xml - reader functions-->
        <GridDyn name = "test1" version = "0.0.1">
        <bus name = "bus1">
        <type>SLK</type>
        <angle>0</angle>
        <voltage>1.04</voltage>
        <generator name = "gen1">
        <P>0.7160</P>
        </generator>
        </bus>
        </GridDyn>)xml";

    XmlReaderElement reader;
    reader.parse(XMLtestString);
    ASSERT_TRUE(reader.isValid());

    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), "GridDyn");
    auto sibling = firstChild->nextSibling();
    EXPECT_EQ(sibling, nullptr);
    sibling = firstChild->firstChild();
    EXPECT_EQ(sibling->getName(), "bus");
    auto cChild = sibling->nextSibling();
    EXPECT_EQ(cChild, nullptr);
    sibling->moveToNextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto busElement = firstChild->firstChild();
    auto busChild = busElement->firstChild("type");
    ASSERT_NE(busChild, nullptr);
    EXPECT_EQ(busChild->getName(), "type");

    auto val = busChild->getText();
    EXPECT_EQ(val, "SLK");

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "angle");
    EXPECT_EQ(busChild->getValue(), 0.0);
    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "voltage");
    EXPECT_EQ(busChild->getText(), "1.04");
    EXPECT_NEAR(busChild->getValue(), 1.04, 1e-6);

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "generator");
    EXPECT_TRUE(busChild->getText().empty());
    auto genChild = busChild->firstChild();
    EXPECT_EQ(genChild->getName(), "P");
    EXPECT_NEAR(genChild->getValue(), 0.7160, 1e-5);

    auto att = busChild->getFirstAttribute();
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "gen1");
    busChild->moveToParent();
    EXPECT_EQ(busChild->getName(), busElement->getName());

    att = firstChild->getAttribute("name");
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "test1");
}

TEST(ElementReaderTests, TinyxmlElementReaderTest4)
{
    auto reader =
        std::make_shared<XmlReaderElement>(elementReaderTestDirectory + "xmlElementReader_test3.xml");
    EXPECT_EQ(reader->getName(), "main_element");

    auto main = reader->clone();
    reader = nullptr;
    EXPECT_EQ(main->getName(), "main_element");
    main->bookmark();
    main->moveToFirstChild();
    auto tstr = main->getMultiText(", ");
    EXPECT_EQ(tstr, "part1, part2, part3");
    main->moveToFirstChild();
    double val = main->getAttributeValue("att1");
    EXPECT_EQ(val, readerNullVal);
    main->moveToFirstChild();

    val = main->getValue();
    EXPECT_EQ(val, readerNullVal);
    EXPECT_EQ(main->getText(), "45.3echo");
    main->restore();
    EXPECT_EQ(main->getName(), "main_element");
    main->bookmark();
    main->moveToFirstChild();
    main->moveToFirstChild();
    main->bookmark();
    main->moveToParent();
    main->moveToParent();
    main->restore();
    EXPECT_EQ(main->getName(), "subelementA");
    main->restore();
    EXPECT_EQ(main->getName(), "main_element");
}

TEST(ElementReaderTests, PugixmlElementReaderTest1)
{
    XmlReaderElement reader;
    ASSERT_TRUE(reader.loadFile(elementReaderTestDirectory + "xmlElementReader_test.xml"));
    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), "griddyn");
    auto sibling = firstChild->nextSibling();
    EXPECT_EQ(sibling, nullptr);
    sibling = firstChild->firstChild();
    EXPECT_EQ(sibling->getName(), "bus");
    auto cChild = sibling->nextSibling();
    EXPECT_EQ(cChild, nullptr);
    sibling->moveToNextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto busElement = firstChild->firstChild();
    auto busChild = busElement->firstChild("type");
    ASSERT_NE(busChild, nullptr);
    EXPECT_EQ(busChild->getName(), "type");

    auto val = busChild->getText();
    EXPECT_EQ(val, "SLK");

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "angle");
    EXPECT_EQ(busChild->getValue(), 0.0);
    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "voltage");
    EXPECT_EQ(busChild->getText(), "1.04");
    EXPECT_NEAR(busChild->getValue(), 1.04, 1e-6);

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "generator");
    EXPECT_TRUE(busChild->getText().empty());
    auto genChild = busChild->firstChild();
    EXPECT_EQ(genChild->getName(), "P");
    EXPECT_NEAR(genChild->getValue(), 0.7160, 1e-5);

    auto att = busChild->getFirstAttribute();
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "gen1");
    busChild->moveToParent();
    EXPECT_EQ(busChild->getName(), busElement->getName());

    att = firstChild->getAttribute("name");
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "test1");
}

TEST(ElementReaderTests, PugixmlElementReaderTest2)
{
    XmlReaderElement reader;
    reader.loadFile(elementReaderTestDirectory + "xmlElementReader_testbbad.xml");
    EXPECT_FALSE(reader.isValid());
    reader.loadFile(elementReaderTestDirectory + "xmlElementReader_test2.xml");
    EXPECT_TRUE(reader.isValid());

    auto main = reader.clone();
    auto sub = main->firstChild();
    for (int kk = 0; kk < 8; ++kk) {
        sub->moveToNextSibling();
    }
    auto name = sub->getName();
    EXPECT_EQ(name, "elementWithAttributes");
    auto att = sub->getFirstAttribute();
    EXPECT_EQ(att.getText(), "A");
    att = sub->getNextAttribute();
    EXPECT_EQ(att.getValue(), 46);
    att = sub->getNextAttribute();
    EXPECT_NEAR(att.getValue(), 21.345, 1e-6);
    att = sub->getNextAttribute();
    EXPECT_EQ(att.getValue(), readerNullVal);
    EXPECT_EQ(att.getText(), "happy");
    att = sub->getNextAttribute();
    EXPECT_FALSE(att.isValid());
}

TEST(ElementReaderTests, PugixmlElementReaderTest3)
{
    XmlReaderElement reader(elementReaderTestDirectory + "xmlElementReader_test2.xml");
    auto main = reader.clone();

    main->moveToFirstChild("subelementA");
    EXPECT_EQ(main->getName(), "subelementA");
    main->moveToNextSibling("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementA");
    EXPECT_FALSE(main->isValid());
    main->moveToParent();
    EXPECT_EQ(main->getName(), "main_element");
    main->moveToFirstChild("subelementB");
    EXPECT_EQ(main->getName(), "subelementB");
    main->moveToNextSibling("subelementB");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementB");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("subelementB");
    EXPECT_EQ(main->getName(), "subelementB");
    main->moveToNextSibling("subelementB");
    EXPECT_FALSE(main->isValid());

    main->moveToParent();
    main->moveToFirstChild("subelementA");
    EXPECT_TRUE(main->isValid());
    main->moveToNextSibling("elementWithAttributes");
    EXPECT_TRUE(main->isValid());

    main->moveToFirstChild("badElementName");
    EXPECT_FALSE(main->isValid());
    main->moveToParent();
    EXPECT_TRUE(main->isValid());
    main->moveToFirstChild("badElementName");
    main->moveToFirstChild("badElementName");
    main->moveToParent();
    EXPECT_TRUE(main->isValid());
    main->moveToParent();
    EXPECT_TRUE(main->isValid());
    auto name = main->getName();
    EXPECT_EQ(name, "main_element");
    main->moveToParent();
    EXPECT_TRUE(main->isDocument());
}

TEST(ElementReaderTests, PugixmlElementReaderTestParse)
{
    std::string XMLtestString = R"xml(<?xml version="1.0" encoding="utf - 8"?>
<!--xml file to test the xml - reader functions-->
        <GridDyn name = "test1" version = "0.0.1">
        <bus name = "bus1">
        <type>SLK</type>
        <angle>0</angle>
        <voltage>1.04</voltage>
        <generator name = "gen1">
        <P>0.7160</P>
        </generator>
        </bus>
        </GridDyn>)xml";

    XmlReaderElement reader;
    reader.parse(XMLtestString);

    ASSERT_TRUE(reader.isValid());
    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), "GridDyn");
    auto sibling = firstChild->nextSibling();
    EXPECT_EQ(sibling, nullptr);
    sibling = firstChild->firstChild();
    EXPECT_EQ(sibling->getName(), "bus");
    auto cChild = sibling->nextSibling();
    EXPECT_EQ(cChild, nullptr);
    sibling->moveToNextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto busElement = firstChild->firstChild();
    auto busChild = busElement->firstChild("type");
    ASSERT_NE(busChild, nullptr);
    EXPECT_EQ(busChild->getName(), "type");

    auto val = busChild->getText();
    EXPECT_EQ(val, "SLK");

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "angle");
    EXPECT_EQ(busChild->getValue(), 0.0);
    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "voltage");
    EXPECT_EQ(busChild->getText(), "1.04");
    EXPECT_NEAR(busChild->getValue(), 1.04, 1e-6);

    busChild->moveToNextSibling();
    EXPECT_EQ(busChild->getName(), "generator");
    EXPECT_TRUE(busChild->getText().empty());
    auto genChild = busChild->firstChild();
    EXPECT_EQ(genChild->getName(), "P");
    EXPECT_NEAR(genChild->getValue(), 0.7160, 1e-5);

    auto att = busChild->getFirstAttribute();
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "gen1");
    busChild->moveToParent();
    EXPECT_EQ(busChild->getName(), busElement->getName());

    att = firstChild->getAttribute("name");
    EXPECT_EQ(att.getName(), "name");
    EXPECT_EQ(att.getText(), "test1");
}

TEST(ElementReaderTests, PugixmlElementReaderTest4)
{
    auto reader =
        std::make_shared<XmlReaderElement>(elementReaderTestDirectory + "xmlElementReader_test3.xml");
    EXPECT_EQ(reader->getName(), "main_element");

    auto main = reader->clone();
    reader = nullptr;
    EXPECT_EQ(main->getName(), "main_element");
    main->bookmark();
    main->moveToFirstChild();
    auto tstr = main->getMultiText(", ");
    EXPECT_EQ(tstr, "part1, part2, part3");
    main->moveToFirstChild();
    double val = main->getAttributeValue("att1");
    EXPECT_EQ(val, readerNullVal);
    main->moveToFirstChild();

    val = main->getValue();
    EXPECT_EQ(val, readerNullVal);
    EXPECT_EQ(main->getText(), "45.3echo");
    main->restore();
    EXPECT_EQ(main->getName(), "main_element");

    main->bookmark();
    main->moveToFirstChild();
    main->moveToFirstChild();
    main->bookmark();
    main->moveToParent();
    main->moveToParent();
    main->restore();
    EXPECT_EQ(main->getName(), "subelementA");
    main->restore();
    EXPECT_EQ(main->getName(), "main_element");
}
