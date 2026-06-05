/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridBus.h"
#include "griddyn/simulation/Diagnostics.h"
#include "griddyn/simulation/GridDynSimulationFileOps.h"
#include "griddyn/solvers/SolverInterface.h"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;
using namespace gmlc::utilities;

class ExtraPerformanceTests: public GridDynSimulationTestFixture, public ::testing::Test {};

static constexpr std::string_view validationTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                          "/validation_tests/"};

static std::string makeValidationTestPath(std::string_view fileName)
{
    return std::string{validationTestDirectory} + std::string{fileName};
}

TEST_F(ExtraPerformanceTests, PerformanceTests1)
{
    const stringVec perf_cases{
        "case1354pegase.m", "case2869pegase.m", "case3012wp.m", "case3375wp.m", "case9241pegase.m"};

    std::chrono::duration<double> pflow_time(0);
    std::chrono::duration<double> load_time(0);
    for (const auto& matpowerCase : perf_cases) {
        std::string fileName;
        if (matpowerCase.length() > 25) {
            fileName = matpowerCase;
        } else {
            fileName = makeValidationTestPath(matpowerCase);
        }
        for (int kk = 0; kk < 10; ++kk) {
            gds = std::make_unique<GridDynSimulation>();
            gds->set("consoleprintlevel", "summary");
            auto start_t = std::chrono::high_resolution_clock::now();
            loadFile(gds.get(), fileName);
            gds->setFlag("no_powerflow_adjustments");
            auto stop_t = std::chrono::high_resolution_clock::now();
            load_time += (stop_t - start_t);

            ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);

            start_t = std::chrono::high_resolution_clock::now();
            gds->powerflow();
            stop_t = std::chrono::high_resolution_clock::now();
            pflow_time += (stop_t - start_t);

            if (gds->currentProcessState() != GridDynSimulation::GridState::POWERFLOW_COMPLETE) {
                std::cout << fileName << " did not complete power flow calculation\n";
                break;
            }
            ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::POWERFLOW_COMPLETE);
        }
        std::println("{} load in {:f} powerflow in {:f}",
                     matpowerCase,
                     load_time.count() / 10.0,
                     pflow_time.count() / 10.0);
    }
}

TEST_F(ExtraPerformanceTests, PerformanceTestsScalingPflow)
{
    std::string testFile =
        std::string(GRIDDYN_TEST_DIRECTORY "/performance_tests/block_grid3_motor.xml");

    std::vector<int> elements = {
        3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  14,  16,  18,  20,  24,  28,
        32,  36,  40,  45,  50,  56,  60,  66,  70,  74,  80,  84,  86,  88,  90,  92,
        100, 110, 120, 132, 136, 140, 148, 156, 164, 168, 172, 180, 188, 196, 204, 212,
        218, 220, 224, 230, 240, 244, 250, 260, 270, 318, 340, 360, 400,
    };
    int numLoops = 1;
    for (int rr = 0; rr < numLoops; ++rr) {
        std::chrono::duration<double> pflow_time(0);
        std::chrono::duration<double> load_time(0);

        std::vector<double> ldtime(elements.size());
        std::vector<double> pftime(elements.size());
        std::string outstring = "block3_motor_timing_kin1_" + std::to_string(rr) + ".csv";
        std::ofstream outfile(outstring);
        outfile
            << "N, buses, states, nnz, resid calls, jac call, load time, powerflow time,  solve time, resid "
               "time, jac time, jac1 time, kin1time\n";
        int sizeIndex = 0;
        for (auto gsize : elements) {
            gds = std::make_unique<GridDynSimulation>();

            ReaderInfo readerInfo;
            readerInfo.addLockedDefinition("garraySize", std::to_string(gsize));
            gds->set("consoleprintlevel", "summary");
            auto start_t = std::chrono::high_resolution_clock::now();
            loadFile(gds.get(), testFile, &readerInfo);
            gds->setFlag("no_powerflow_adjustments");
            auto stop_t = std::chrono::high_resolution_clock::now();
            load_time = (stop_t - start_t);
            ldtime[sizeIndex] = load_time.count();

            ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);

            start_t = std::chrono::high_resolution_clock::now();
            gds->powerflow();
            stop_t = std::chrono::high_resolution_clock::now();
            gds->updateLocalCache();
            pflow_time = (stop_t - start_t);
            pftime[sizeIndex] = pflow_time.count();
            int stateSize = gds->getInt("statesize");
            int nnz = gds->getInt("nonzeros");
            int rcount = gds->getInt("residcount");
            int jcount = gds->getInt("jaccount");
            std::println("{} size={}, nnz={},ld time={:f}, pflow time={:f}",
                         gsize,
                         stateSize,
                         nnz,
                         load_time.count(),
                         pflow_time.count());
            std::println("{} residual calls, {} Jacobian call", rcount, jcount);
            auto bus = static_cast<GridBus*>(gds->findByUserID("bus", 10000000));
            std::println("slack bus gen p={:f}, gen q ={:f}",
                         bus->getGenerationReal(),
                         bus->getGenerationReactive());
            outfile << gsize << ", " << gsize * gsize << ", " << stateSize << ", " << nnz
                    << ", ";
            outfile << rcount << ", " << jcount << ", ";
            outfile << load_time.count() << ", " << pflow_time.count();
            auto solverInterface = gds->getSolverInterface("powerflow");
            outfile << ", " << solverInterface->get("kintime") << ", "
                    << solverInterface->get("residtime") << ", "
                    << solverInterface->get("jactime");
            outfile << ", " << solverInterface->get("jac1time") << ", "
                    << solverInterface->get("kin1time") << "\n";
            std::vector<double> voltages;
            int voltageCount = gds->getVoltage(voltages);
            auto minV = minLoc(voltages);
            auto maxV = maxLoc(voltages);
            std::println("cnt={} vmin={:f} at {}, vmax={:f} at {} ",
                         voltageCount,
                         minV.first,
                         minV.second,
                         maxV.first,
                         maxV.second);
            ++sizeIndex;
        }
    }
}

TEST_F(ExtraPerformanceTests, DynamicScalableTest)
{
    std::string testFile =
        std::string(GRIDDYN_TEST_DIRECTORY "/performance_tests/block_grid2_dynamic.xml");

    std::chrono::duration<double> pflow_time(0);
    std::chrono::duration<double> load_time(0);

    int gsize = 50;

    gds = std::make_unique<GridDynSimulation>();

    ReaderInfo readerInfo;
    readerInfo.addLockedDefinition("garraySize", std::to_string(gsize));
    gds->set("consoleprintlevel", "summary");
    auto start_t = std::chrono::high_resolution_clock::now();
    loadFile(gds.get(), testFile, &readerInfo);
    gds->setFlag("no_powerflow_adjustments");
    auto stop_t = std::chrono::high_resolution_clock::now();
    load_time = (stop_t - start_t);

    ASSERT_EQ(gds->currentProcessState(), GridDynSimulation::GridState::STARTUP);

    start_t = std::chrono::high_resolution_clock::now();
    gds->powerflow();
    stop_t = std::chrono::high_resolution_clock::now();
    gds->updateLocalCache();
    pflow_time = (stop_t - start_t);

    int stateSize = gds->getInt("statesize");
    int nnz = gds->getInt("nonzeros");
    int rcount = gds->getInt("residcount");
    int jcount = gds->getInt("jaccount");
    std::println("{} size={}, nnz={},ld time={:f}, pflow time={:f}",
                 gsize,
                 stateSize,
                 nnz,
                 load_time.count(),
                 pflow_time.count());
    std::println("{} residual calls, {} Jacobian call", rcount, jcount);
    auto bus = static_cast<GridBus*>(gds->findByUserID("bus", 10000000));
    std::println("slack bus gen p={:f}, gen q ={:f}",
                 bus->getGenerationReal(),
                 bus->getGenerationReactive());

    gds->run();
}
