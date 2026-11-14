/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for CoreObject object

#include "../gtestHelper.h"
#include "fileInput/ReaderInfo.h"
#include "griddyn/gridDynDefinitions.hpp"
#include <gtest/gtest.h>
#include <string>
#include <string_view>

static constexpr std::string_view xmlTestDirectory{GRIDDYN_TEST_DIRECTORY "/xml_tests/"};

static std::string makeXmlTestPath(std::string_view fileName)
{
    return std::string{xmlTestDirectory} + std::string{fileName};
}
using namespace griddyn;

TEST(ReaderInfoTests, Defines)
{
    ReaderInfo readerInfo;
    readerInfo.addDefinition("bob", "rt");
    readerInfo.addDefinition("bb2", "rt2");
    readerInfo.addDefinition("bbnum", "3");
    auto text1 = readerInfo.checkDefines("bob");
    EXPECT_EQ(text1, "rt");
    auto text2 = readerInfo.checkDefines("bb2");
    EXPECT_EQ(text2, "rt2");
    // test the substring replacement
    auto text3 = readerInfo.checkDefines("$bob$_and_$bb2$");
    EXPECT_EQ(text3, "rt_and_rt2");
    // test numerical conversion with string replacement
    auto text4 = readerInfo.checkDefines("object$bbnum*2$");
    EXPECT_EQ(text4, "object6");
    // test numerical conversion with string replacement for floating point
    auto text5 = readerInfo.checkDefines("object$bbnum*3.3$");
    EXPECT_EQ(text5, "object9.9");
    // test secondary conversion after numeric conversion
    readerInfo.addDefinition("object99", "fred");
    auto text6 = readerInfo.checkDefines("object$bbnum*33$");
    EXPECT_EQ(text6, "fred");
    // test embedded $'s
    auto text7 = readerInfo.checkDefines("$object$bbnum*33$$$bbnum*2$");
    EXPECT_EQ(text7, "fred6");
}

TEST(ReaderInfoTests, IteratedDefine)
{
    ReaderInfo readerInfo;
    readerInfo.addDefinition("bob", "rt");
    readerInfo.addDefinition("rt", "rtb");
    auto text1 = readerInfo.checkDefines("bob");
    EXPECT_EQ(text1, "rtb");

    readerInfo.addDefinition("rtbrtb", "rtb^2");
    auto text2 = readerInfo.checkDefines("$rt$$rt$");
    EXPECT_EQ(text2, "rtb^2");
}

TEST(ReaderInfoTests, DefinitionScope)
{
    ReaderInfo readerInfo;
    readerInfo.addDefinition("bob", "rt");
    readerInfo.addDefinition("bb2", "rt2");
    auto scopeId = readerInfo.newScope();
    readerInfo.addDefinition("bob2", "rt3");
    auto text1 = readerInfo.checkDefines("bob2");
    EXPECT_EQ(text1, "rt3");
    readerInfo.closeScope(scopeId);
    text1 = readerInfo.checkDefines("bob2");
    EXPECT_EQ(text1, "bob2");
    // check overwritten definitions in different scopes
    readerInfo.addDefinition("scope", "scope0");
    readerInfo.newScope();
    readerInfo.addDefinition("scope", "scope1");
    readerInfo.newScope();
    readerInfo.addDefinition("scope", "scope2");
    text1 = readerInfo.checkDefines("scope");
    EXPECT_EQ(text1, "scope2");
    readerInfo.closeScope();
    text1 = readerInfo.checkDefines("scope");
    EXPECT_EQ(text1, "scope1");
    readerInfo.closeScope();
    text1 = readerInfo.checkDefines("scope");
    EXPECT_EQ(text1, "scope0");
    readerInfo.closeScope();
    text1 = readerInfo.checkDefines("scope");
    EXPECT_EQ(text1, "scope0");
}

TEST(ReaderInfoTests, Directories)
{
    ReaderInfo readerInfo;
    readerInfo.addDirectory(std::string{xmlTestDirectory});

    std::string test1 = "test_xmltest1.xml";

    auto res = readerInfo.checkFileParam(test1, false);

    EXPECT_TRUE(res);
    EXPECT_EQ(test1, makeXmlTestPath("test_xmltest1.xml"));

    ReaderInfo scopedReaderInfo;
    scopedReaderInfo.addDirectory(GRIDDYN_TEST_DIRECTORY);
    std::string testfile = "location_testFile.txt";

    res = scopedReaderInfo.checkFileParam(testfile, false);
    EXPECT_TRUE(res);
    EXPECT_TRUE((testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "\\location_testFile.txt")) ||
                (testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "/location_testFile.txt")));
    scopedReaderInfo.newScope();
    scopedReaderInfo.addDirectory(std::string{xmlTestDirectory});
    // this file is in 2 locations to ensure the recent directory takes precedence
    testfile = "location_testFile.txt";

    res = scopedReaderInfo.checkFileParam(testfile, false);
    EXPECT_TRUE(res);
    EXPECT_EQ(testfile, makeXmlTestPath("location_testFile.txt"));
    scopedReaderInfo.closeScope();
    testfile = "location_testFile.txt";

    res = scopedReaderInfo.checkFileParam(testfile, false);
    EXPECT_TRUE(res);
    EXPECT_TRUE((testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "\\location_testFile.txt")) ||
                (testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "/location_testFile.txt")));
    // now check if we close the extra scope;
    scopedReaderInfo.closeScope();
    testfile = "location_testFile.txt";

    res = scopedReaderInfo.checkFileParam(testfile, false);
    EXPECT_TRUE(res);
    EXPECT_TRUE((testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "\\location_testFile.txt")) ||
                (testfile == (std::string(GRIDDYN_TEST_DIRECTORY) + "/location_testFile.txt")));
}
