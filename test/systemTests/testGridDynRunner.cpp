/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for element readers

#include "../exeTestHelper.h"
#include "runner/gridDynRunner.h"
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>

#ifdef ENABLE_THIS_CASE
TEST(GridDynRunnerTests, RunnerTest1)
{
    std::string fileName = std::string(GRIDDYN_TEST_DIRECTORY "/runnerTests/test_180_trip.xml");
    // std::string fileName = std::string(GRIDDYN_TEST_DIRECTORY
    // "/../../examples/test_180bus_coupled_relay.xml");

    auto gdr = std::make_shared<griddyn::GriddynRunner>();
    int argc = 4;
    char* argv[] = {"griddyn", "-v", "0", (char*)fileName.data()};

    gdr->Initialize(argc, argv);

    gdr->simInitialize();

    long simTime = 0;
    double m_currentGDTime;
    long prevTime = -1;

    std::string line;
    std::ifstream testFile(
        std::string(GRIDDYN_TEST_DIRECTORY "/runner_tests/coupled-time-solvererror.txt"));
    if (!testFile.is_open()) {
        std::cout << "failed to open file";
    } else {
        while (std::getline(testFile, line) && simTime < 18000000000) {
            std::istringstream ss(line);
            std::string time;
            long playbackTime;
            std::getline(ss, time, ',');
            playbackTime = stol(time, nullptr, 10);
            /*    if (playbackTime < 4000000000)
            {
                simTime += 10000000;
            }
            else if (playbackTime > 5000000000 && playbackTime < 15000000000)
            {
                simTime += 10000000;
            }
            else*/
            {
                simTime = playbackTime;
                std::cout << line << "\t\t" << simTime << std::endl;
            }
            if (prevTime < simTime) {
                // Try explicit (double) casting of simTime and see if crashes
                m_currentGDTime = gdr->Step((double)simTime * 1.0E-9);
                prevTime = simTime;
            } else {
                std::cout << "suppressing duplicate" << std::endl;
            }
        }
        testFile.close();
    }

    for (; simTime < 61000000000; simTime += 10000000) {
        std::cout << simTime << std::endl;
        m_currentGDTime = gdr->Step(simTime * 1.0E-9);
        m_currentGDTime = gdr->Step(simTime * 1.0E-9);
    }

    gdr->Finalize();
}

#endif
