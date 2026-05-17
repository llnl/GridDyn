/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for CoreObject object

#include "../gtestHelper.h"
#include "formatInterpreters/jsonReaderElement.h"
#include <gtest/gtest.h>
#include <iostream>
#include <string>

static const std::string elementReaderTestDirectory(GRIDDYN_TEST_DIRECTORY
                                                    "/element_reader_tests/");

TEST(JsonElementReaderTests, JsonElementReaderTest1)
{
    jsonReaderElement reader;
    ASSERT_TRUE(reader.loadFile(elementReaderTestDirectory + "json_test1.json"));
    EXPECT_TRUE(reader.isValid());
    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), elementReaderTestDirectory + "json_test1.json");

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
    EXPECT_EQ(firstChild->getName(), elementReaderTestDirectory + "json_test1.json");
}

TEST(JsonElementReaderTests, JsonElementReaderTest2)
{
    jsonReaderElement reader;
    // test a bad file
    reader.loadFile(elementReaderTestDirectory + "xmlElementReader_missing_file.xml");
    std::cout
        << "NOTE:: this should have a message about a missing file >>testing bad file input\n";
    EXPECT_FALSE(reader.isValid());
    reader.loadFile(elementReaderTestDirectory + "json_test2.json");
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
    jsonReaderElement reader(elementReaderTestDirectory + "xmlElementReader_test2.xml");
    std::cout
        << "NOTE:: this should have a message indicating format error >>testing bad file input\n";
    EXPECT_FALSE(reader.isValid());
    reader.loadFile(elementReaderTestDirectory + "json_test3.json");
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
    /*auto reader = std::make_shared<jsonReaderElement>(xmlTestDirectory +
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
