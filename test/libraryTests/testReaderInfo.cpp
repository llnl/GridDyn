/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for coreObject object

#include "../gtestHelper.h"
#include "fileInput/readerInfo.h"
#include "griddyn/gridDynDefinitions.hpp"
#include <gtest/gtest.h>

static const std::string xmlTestDirectory(GRIDDYN_TEST_DIRECTORY "/xml_tests/");
using namespace griddyn;

TEST(ReaderInfoTests, Defines)
{
    readerInfo r1;
    r1.addDefinition("bob", "rt");
    r1.addDefinition("bb2", "rt2");
    r1.addDefinition("bbnum", "3");
    auto text1 = r1.checkDefines("bob");
    EXPECT_EQ(text1, "rt");
    auto text2 = r1.checkDefines("bb2");
    EXPECT_EQ(text2, "rt2");
    // test the substring replacement
    auto text3 = r1.checkDefines("$bob$_and_$bb2$");
    EXPECT_EQ(text3, "rt_and_rt2");
    // test numerical conversion with string replacement
    auto text4 = r1.checkDefines("object$bbnum*2$");
    EXPECT_EQ(text4, "object6");
    // test numerical conversion with string replacement for floating point
    auto text5 = r1.checkDefines("object$bbnum*3.3$");
    EXPECT_EQ(text5, "object9.9");
    // test secondary conversion after numeric conversion
    r1.addDefinition("object99", "fred");
    auto text6 = r1.checkDefines("object$bbnum*33$");
    EXPECT_EQ(text6, "fred");
    // test embedded $'s
    auto text7 = r1.checkDefines("$object$bbnum*33$$$bbnum*2$");
    EXPECT_EQ(text7, "fred6");
}

TEST(ReaderInfoTests, IteratedDefine)
{
    readerInfo r1;
    r1.addDefinition("bob", "rt");
    r1.addDefinition("rt", "rtb");
    auto text1 = r1.checkDefines("bob");
    EXPECT_EQ(text1, "rtb");

    r1.addDefinition("rtbrtb", "rtb^2");
    auto text2 = r1.checkDefines("$rt$$rt$");
    EXPECT_EQ(text2, "rtb^2");
}

TEST(ReaderInfoTests, DefinitionScope)
{
    readerInfo r1;
    r1.addDefinition("bob", "rt");
    r1.addDefinition("bb2", "rt2");
    auto p = r1.newScope();
    r1.addDefinition("bob2", "rt3");
    auto text1 = r1.checkDefines("bob2");
    EXPECT_EQ(text1, "rt3");
    r1.closeScope(p);
    text1 = r1.checkDefines("bob2");
    EXPECT_EQ(text1, "bob2");
    // check overwritten definitions in different scopes
    r1.addDefinition("scope", "scope0");
    r1.newScope();
    r1.addDefinition("scope", "scope1");
    r1.newScope();
    r1.addDefinition("scope", "scope2");
    text1 = r1.checkDefines("scope");
    EXPECT_EQ(text1, "scope2");
    r1.closeScope();
    text1 = r1.checkDefines("scope");
    EXPECT_EQ(text1, "scope1");
    r1.closeScope();
    text1 = r1.checkDefines("scope");
    EXPECT_EQ(text1, "scope0");
    r1.closeScope();
    text1 = r1.checkDefines("scope");
    EXPECT_EQ(text1, "scope0");
}

TEST(ReaderInfoTests, Directories)
{
    readerInfo r1;
    r1.addDirectory(xmlTestDirectory);

    std::string test1 = "test_xmltest1.xml";

    auto res = r1.checkFileParam(test1, false);

    EXPECT_TRUE(res);
    EXPECT_EQ(test1, xmlTestDirectory + "test_xmltest1.xml");

    readerInfo r2;
    r2.addDirectory(GRIDDYN_TEST_DIRECTORY);
    std::string testfile = "location_testFile.txt";

    res = r2.checkFileParam(testfile, false);
    EXPECT_TRUE((testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "\\location_testFile.txt")) ||
                (testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "/location_testFile.txt")));
    r2.newScope();
    r2.addDirectory(xmlTestDirectory);
    // this file is in 2 locations to ensure the recent directory takes precedence
    testfile = "location_testFile.txt";

    res = r2.checkFileParam(testfile, false);
    EXPECT_EQ(testfile, (xmlTestDirectory + "location_testFile.txt"));
    r2.closeScope();
    testfile = "location_testFile.txt";

    res = r2.checkFileParam(testfile, false);
    EXPECT_TRUE((testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "\\location_testFile.txt")) ||
                (testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "/location_testFile.txt")));
    // now check if we close the extra scope;
    r2.closeScope();
    testfile = "location_testFile.txt";

    res = r2.checkFileParam(testfile, false);
    EXPECT_TRUE((testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "\\location_testFile.txt")) ||
                (testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "/location_testFile.txt")));
}
