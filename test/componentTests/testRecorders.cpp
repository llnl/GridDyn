/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include "griddyn/events/Event.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/measurement/collector.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// test case for coreObject object

#define RECORDER_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/recorder_tests/"
static const char collector_test_directory[] = GRIDDYN_TEST_DIRECTORY "/recorder_tests/";

using namespace griddyn;
using namespace gmlc::utilities;

class RecorderTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(RecorderTests, TsMultiTests)
{
    TimeSeriesMulti<> ts2;
    TimeSeriesMulti<> ts3(1, 10);
    std::vector<double> tv(10);
    std::vector<double> val(10);
    ts2.setCols(1);
    double t = 0.0;
    for (int kk = 0; kk < 10; ++kk) {
        ts2.addData(t, 4.5);
        tv[kk] = t;
        val[kk] = 4.5;
        t = t + 1.0;
    }
    EXPECT_EQ(ts2.size(), 10U);
    ts3.addData(tv, val);
    EXPECT_EQ(ts3.size(), 10U);

    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-4);

    ts3.setCols(4);
    ts3.addData(val, 1);
    ts3.addData(val, 2);
    ts3.addData(val, 3);
    ts3.updateData(3, 4, 6.5);

    EXPECT_NEAR(compare(ts2, ts3, 0, 3), 2.0, std::abs(2.0) * 1e-6 + 1e-12);
}

TEST_F(RecorderTests, FileSaveTests)
{
    TimeSeriesMulti<> ts2(1);
    double t = 0.0;
    for (int kk = 0; kk < 10; ++kk) {
        ts2.addData(t, 4.5);
        t = t + 1.0;
    }
    EXPECT_EQ(ts2.size(), 10U);
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "ts_test.dat");
    ts2.writeBinaryFile(fileName);

    TimeSeriesMulti<> ts3;

    ts3.loadBinaryFile(fileName);
    EXPECT_EQ(ts3.columns(), 1U);
    EXPECT_EQ(ts3.size(), 10U);
    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-5);
    int ret = remove(fileName.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(RecorderTests, FileSaveTests2)
{
    TimeSeriesMulti<> ts2;
    ts2.setCols(4);  // test the set cols method
    EXPECT_EQ(ts2.columns(), 4);

    std::vector<double> vt{4.5, 5.5, 6.5, 7.5};

    double t = 0.0;
    for (int kk = 0; kk < 30; ++kk) {
        ts2.addData(t, vt);
        t = t + 1.0;
    }
    EXPECT_EQ(ts2.size(), 30U);
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "ts_test2.dat");
    ts2.writeBinaryFile(fileName);

    TimeSeriesMulti<> ts3(fileName);

    EXPECT_EQ(ts3.columns(), 4U);
    EXPECT_EQ(ts3.size(), 30U);
    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-5);

    ts3.updateData(3, 2, 7.0);
    double diff = compare(ts2, ts3);
    EXPECT_NEAR(diff, 0.5, std::abs(0.5) * 1e-5 + 1e-12);
    EXPECT_EQ(ts3.size(), ts3[3].size());
    int ret = remove(fileName.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(RecorderTests, RecorderTest1)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::debug;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 1);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "loadrec.dat");
    TimeSeriesMulti<> ts3(recname);
    EXPECT_EQ(ts3.getField(0), "load3:power");
    EXPECT_EQ(ts3.size(), 31U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
}

TEST_F(RecorderTests, RecorderTest2)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test2.xml");
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 1);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "genrec.dat");
    TimeSeriesMulti<> ts3(recname);

    EXPECT_EQ(ts3.size(), 31U);
    EXPECT_EQ(ts3.columns(), 2U);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

TEST_F(RecorderTests, RecorderTest3)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test3.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 3);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "genrec.dat");
    TimeSeriesMulti<> ts3(recname);

    EXPECT_EQ(ts3.size(), 121U);
    EXPECT_EQ(ts3.columns(), 4U);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    recname = std::string(RECORDER_TEST_DIRECTORY "busrec.dat");
    ts3.loadBinaryFile(recname);

    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 2U);
    ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    recname = std::string(RECORDER_TEST_DIRECTORY "loadrec.dat");
    ts3.loadBinaryFile(recname);

    EXPECT_EQ(ts3.size(), 31U);
    EXPECT_EQ(ts3.columns(), 1U);
    ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
}

// testing the recorder as subObject and recorder found via link
TEST_F(RecorderTests, RecorderTest4)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test4.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 2);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec.dat");
    TimeSeriesMulti<> ts3(recname);

    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 2U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);

    recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2(recname);

    EXPECT_EQ(ts2.size(), 61U);
    EXPECT_EQ(ts2.columns(), 2U);
    ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-5);
}

// testing the vector recorders of the simulation object
TEST_F(RecorderTests, RecorderTest5)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test5.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 2);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busVrec.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);

    EXPECT_EQ(ts3.getField(2), "test1:voltage[2]");
    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 4U);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
    recname = std::string(RECORDER_TEST_DIRECTORY "linkVrec.dat");

    ts3.loadBinaryFile(recname);

    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 5u);
    ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);
}

// testing multiple items in a field
TEST_F(RecorderTests, RecorderTest6)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test6.xml");
    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    EXPECT_EQ(gds->getInt("recordercount"), 2);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec.dat");
    TimeSeriesMulti<> ts3(recname);
    // ts3.loadBinaryFile ;

    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 2U);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 61U);
    EXPECT_EQ(ts2.columns(), 2U);
    ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-5);
}

// testing multiple :: naming for fields
TEST_F(RecorderTests, RecorderTest7)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test7.xml");
    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 2);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);

    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 2U);
    int ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 61U);
    EXPECT_EQ(ts2.columns(), 2U);
    ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-5);
}

// testing multiple :: naming for fields and units
TEST_F(RecorderTests, RecorderTest8)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test8.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);
    int val = gds->getInt("recordercount");
    EXPECT_EQ(val, 2);
    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec.dat");
    TimeSeriesMulti<> ts3;
    ts3.loadBinaryFile(recname);

    EXPECT_EQ(ts3.size(), 61U);
    EXPECT_EQ(ts3.columns(), 3U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
    // check to make sure the conversion is correct
    EXPECT_NEAR(ts3.data(0, 3) * 180 / kPI - ts3.data(2, 3), 0.0, 1e-4);

    recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 61U);
    EXPECT_EQ(ts2.columns(), 3U);
    ret = remove(recname.c_str());

    EXPECT_EQ(ret, 0);

    EXPECT_NEAR(compare(ts2, ts3), 0.0, 1e-5);
}

// testing gain and bias
TEST_F(RecorderTests, RecorderTest9)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test9.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);

    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 21U);
    EXPECT_EQ(ts2.columns(), 4U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
    EXPECT_NEAR(ts2.data(1, 2) - 1.0, ts2.data(3, 2), std::abs(ts2.data(3, 2)) * 1e-6 + 1e-12);
}

// testing multiple grabber calculations
TEST_F(RecorderTests, RecorderTest10)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test10.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);

    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 11U);
    EXPECT_EQ(ts2.columns(), 3U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
    EXPECT_NEAR(ts2.data(0, 2) - ts2.data(1, 2),
                ts2.data(2, 2),
                std::abs(ts2.data(2, 2)) * 1e-6 + 1e-12);
    EXPECT_NEAR(ts2.data(0, 8) - ts2.data(1, 8),
                ts2.data(2, 8),
                std::abs(ts2.data(2, 8)) * 1e-6 + 1e-12);
}

TEST_F(RecorderTests, RecorderTest11)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test11.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);

    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 11U);
    EXPECT_EQ(ts2.columns(), 4U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
    EXPECT_NEAR(ts2.data(0, 2) - (ts2.data(1, 2) - ts2.data(2, 2)),
                ts2.data(3, 2),
                std::abs(ts2.data(3, 2)) * 1e-6 + 1e-12);
    EXPECT_NEAR(ts2.data(0, 8) - (ts2.data(1, 8) - ts2.data(2, 8)),
                ts2.data(3, 8),
                std::abs(ts2.data(3, 8)) * 1e-6 + 1e-12);
}

// testing function calls
TEST_F(RecorderTests, RecorderTest12)
{
    std::string fileName = std::string(RECORDER_TEST_DIRECTORY "recorder_test12.xml");
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);

    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "busrec2.dat");
    TimeSeriesMulti<> ts2;
    ts2.loadBinaryFile(recname);

    EXPECT_EQ(ts2.size(), 11U);
    EXPECT_EQ(ts2.columns(), 3U);
    int ret = remove(recname.c_str());
    EXPECT_EQ(ret, 0);
    EXPECT_NEAR(sin(ts2.data(0, 2) - ts2.data(1, 2)),
                ts2.data(2, 2),
                std::abs(ts2.data(2, 2)) * 1e-6 + 1e-12);
    EXPECT_NEAR(sin(ts2.data(0, 8) - ts2.data(1, 8)),
                ts2.data(2, 8),
                std::abs(ts2.data(2, 8)) * 1e-6 + 1e-12);
}

// test and invalid input
TEST_F(RecorderTests, RecorderTestBadInput)
{
    std::string fileName = std::string(collector_test_directory) + "recorder_test_invalid_field1.xml";
    printf("NOTE:  this should produce some warning messages\n");
    gds = readSimXMLFile(fileName);

    EXPECT_GT(readerConfig::warnCount, 0);
    readerConfig::warnCount = 0;

    fileName = std::string(collector_test_directory) + "recorder_test_invalid_field2.xml";
    gds = readSimXMLFile(fileName);

    EXPECT_GT(readerConfig::warnCount, 0);

    readerConfig::warnCount = 0;
}

// testing if the recorders have any material impact on the results
TEST_F(RecorderTests, RecorderTestPeriod)
{
    std::string fileName = std::string(collector_test_directory) + "recorder_test_sineA.xml";
    gds = readSimXMLFile(fileName);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds->consolePrintLevel = print_level::no_print;
    gds->solverSet("dynamic", "printlevel", 0);

    gds->set("recorddirectory", collector_test_directory);

    gds->run();

    std::string recname = std::string(RECORDER_TEST_DIRECTORY "recorder_dataA.dat");
    TimeSeriesMulti<> tsA(recname);

    std::string fname2 = std::string(collector_test_directory) + "recorder_test_sineB.xml";
    gds2 = readSimXMLFile(fname2);
    EXPECT_EQ(readerConfig::warnCount, 0);
    gds2->consolePrintLevel = print_level::no_print;
    gds2->solverSet("dynamic", "printlevel", 0);

    gds2->set("recorddirectory", collector_test_directory);

    gds2->run();

    std::string recname2 = std::string(RECORDER_TEST_DIRECTORY "recorder_dataB.dat");
    TimeSeriesMulti<> tsB(recname2);

    size_t diffc = 0;
    ASSERT_EQ((tsA.size() - 1) * 4, (tsB.size() - 1));
    for (decltype(tsA.size()) ii = 1; ii < tsA.size(); ++ii) {
        for (decltype(tsA.columns()) jj = 0; jj < tsA.columns(); ++jj) {
            if (std::abs(tsA.data(jj, ii) - tsB.data(jj, 4 * ii)) >
                1e-4)  // TODO(PT):: this is still small but bigger than it really should be
            {
                //  printf("%d,%d t=%f,diff=%f\n", jj, ii, static_cast<double>(tsA.time()[ii]),
                //  tsA.data(jj, ii) - tsB.data(jj, 4 * ii));
                ++diffc;
            }
        }
    }
    EXPECT_EQ(diffc, 0U);
    // remove (recname.c_str ());
    // remove (recname2.c_str ());
}
