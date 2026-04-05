/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include <gtest/gtest.h>

/** these test cases test out the value converters
 */
#include "griddyn.h"

static const std::string ieee_test_directory =
    std::string(GRIDDYN_TEST_DIRECTORY "/IEEE_test_cases/");
static const std::string matlab_test_directory =
    std::string(GRIDDYN_TEST_DIRECTORY "/matlab_test_files/");
static const std::string other_test_directory =
    std::string(GRIDDYN_TEST_DIRECTORY "/other_test_cases/");

/** just a check that in the simple case we do actually get the time back we requested*/
TEST(SharedLibraryBasicTests, SimpleLoadTest)
{
    GridDynError err = gridDynErrorInitialize();
    GridDynSimulation sim = gridDynSimulationCreate("", "sim1", &err);
    EXPECT_NE(sim, nullptr);
    std::string file = ieee_test_directory + "ieee14.cdf";
    gridDynSimulationLoadfile(sim, file.c_str(), "", &err);
    EXPECT_EQ(err.error_code, griddyn_ok);
    gridDynSimulationRun(sim, &err);

    double time = gridDynSimulationGetCurrentTime(sim, &err);

    EXPECT_NEAR(time, 30.0, 0.0001);
    auto obj = getSimulationObject(sim, &err);

    gridDynObjectFree(obj);

    gridDynSimulationFree(sim);
}

TEST(SharedLibraryBasicTests, GetResultTest)
{
    GridDynError err = gridDynErrorInitialize();
    GridDynSimulation sim = gridDynSimulationCreate("", "sim1", &err);
    EXPECT_NE(sim, nullptr);
    std::string file = ieee_test_directory + "ieee14.cdf";
    gridDynSimulationLoadfile(sim, file.c_str(), "", &err);
    EXPECT_EQ(err.error_code, griddyn_ok);
    gridDynSimulationPowerflow(sim, &err);

    int cnt = gridDynSimulationBusCount(sim);

    EXPECT_EQ(cnt, 14);
    std::vector<double> voltages(cnt);
    std::vector<double> angles(cnt);
    int act;
    gridDynSimulationGetResults(sim, "voltage", voltages.data(), cnt, &act, &err);
    ASSERT_EQ(cnt, act);
    gridDynSimulationGetResults(sim, "angles", angles.data(), cnt, &act, &err);
    ASSERT_EQ(cnt, act);
    EXPECT_NEAR(voltages[0], 1.06, 0.001);
    EXPECT_NEAR(angles[0], 0.0, 0.00001);
    EXPECT_NEAR(voltages[1], 1.045, 0.001);
    EXPECT_NEAR(voltages[2], 1.01, 0.001);
    EXPECT_NEAR(voltages[8], 1.056, 0.1);
    EXPECT_NEAR(angles[5], -14.22 * 3.1415927 / 180.0, 0.1);

    gridDynSimulationFree(sim);
}

TEST(SharedLibraryBasicTests, GetObjectTests)
{
    GridDynError err = gridDynErrorInitialize();
    GridDynSimulation sim = gridDynSimulationCreate("", "sim1", &err);
    EXPECT_NE(sim, nullptr);
    std::string file = ieee_test_directory + "ieee14.cdf";
    gridDynSimulationLoadfile(sim, file.c_str(), "", &err);
    EXPECT_EQ(err.error_code, griddyn_ok);
    gridDynSimulationPowerflow(sim, &err);

    auto obj = getSimulationObject(sim, &err);

    auto bus2 = gridDynObjectGetSubObject(obj, "bus", 8, &err);
    gridDynObjectFree(obj);  // just making sure the bus object is disconnected from obj
    EXPECT_NE(bus2, nullptr);

    double result = gridDynObjectGetValue(bus2, "voltage", &err);
    EXPECT_EQ(err.error_code, griddyn_ok);
    EXPECT_NEAR(result, 1.056, 0.1);

    char name[50];
    int strSize;
    gridDynObjectGetString(bus2, "name", name, 50, &strSize, &err);

    std::string namestr(name);
    EXPECT_EQ(strSize, static_cast<int>(namestr.size()));
    EXPECT_EQ(namestr.compare(0, 5, "Bus 9"), 0);
    gridDynObjectFree(bus2);
    gridDynSimulationFree(sim);
}

TEST(SharedLibraryBasicTests, BuildSmallTestCase)
{
    GridDynError err = gridDynErrorInitialize();
    GridDynSimulation sim = gridDynSimulationCreate("", "sim1", &err);
    EXPECT_NE(sim, nullptr);
    auto obj = getSimulationObject(sim, &err);

    auto bus1 = gridDynObjectCreate("bus", "infinite", &err);
    gridDynObjectSetString(bus1, "name", "bus1", &err);
    gridDynObjectAdd(obj, bus1, &err);

    auto bus2 = gridDynObjectCreate("bus", "", &err);
    gridDynObjectSetString(bus2, "name", "bus2", &err);
    gridDynObjectAdd(obj, bus2, &err);

    auto ld1 = gridDynObjectCreate("load", "", &err);
    gridDynObjectSetValue(ld1, "p", 0.7, &err);
    gridDynObjectSetValue(ld1, "q", 0.3, &err);
    gridDynObjectAdd(bus2, ld1, &err);
    EXPECT_EQ(err.error_code, 0);

    auto lnk = gridDynObjectCreate("link", "ac", &err);
    gridDynObjectSetValue(lnk, "x", 0.1, &err);
    gridDynObjectSetValue(lnk, "r", 0.03, &err);

    gridDynObjectAdd(obj, lnk, &err);
    gridDynObjectSetString(lnk, "from", "bus1", &err);
    gridDynObjectSetString(lnk, "to", "bus2", &err);

    gridDynSimulationPowerflow(sim, &err);

    double V = gridDynObjectGetValueUnits(bus2, "voltage", "pu", &err);
    EXPECT_LT(V, 1.0);
    double A = gridDynObjectGetValueUnits(bus2, "angle", "deg", &err);
    EXPECT_LT(A, 0);
    gridDynObjectFree(bus1);
    gridDynObjectFree(ld1);
    gridDynObjectFree(lnk);
    gridDynObjectFree(obj);
    gridDynSimulationFree(sim);
    double V2 = gridDynObjectGetValue(bus2, "voltage", &err);
    EXPECT_EQ(V, V2);
    gridDynObjectFree(bus2);
}
