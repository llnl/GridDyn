/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/simulation/diagnostics.h"
#include "griddyn/simulation/gridDynSimulationFileOps.h"
#include "griddyn/solvers/solverInterface.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <utility>

#include <gtest/gtest.h>

using namespace griddyn;
using namespace gmlc::utilities;

class ExtraPerformanceTests : public gridDynSimulationTestFixture, public ::testing::Test {};

static const std::string validationTestDirectory(GRIDDYN_TEST_DIRECTORY "/validation_tests/");

TEST_F(ExtraPerformanceTests, PerformanceTests1)
{
    const stringVec perf_cases{
        "case1354pegase.m", "case2869pegase.m", "case3012wp.m", "case3375wp.m", "case9241pegase.m"};

    std::chrono::duration<double> pflow_time(0);
    std::chrono::duration<double> load_time(0);
    for (const auto& mp : perf_cases) {
        std::string fileName;
        if (mp.length() > 25) {
            fileName = mp;
        } else {
            fileName = validationTestDirectory + mp;
        }
        for (int kk = 0; kk < 10; ++kk) {
            gds = std::make_unique<gridDynSimulation>();
            gds->set("consoleprintlevel", "summary");
            auto start_t = std::chrono::high_resolution_clock::now();
            loadFile(gds.get(), fileName);
            gds->setFlag("no_powerflow_adjustments");
            auto stop_t = std::chrono::high_resolution_clock::now();
            load_time += (stop_t - start_t);

            ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);

            start_t = std::chrono::high_resolution_clock::now();
            gds->powerflow();
            stop_t = std::chrono::high_resolution_clock::now();
            pflow_time += (stop_t - start_t);

            if (gds->currentProcessState() != gridDynSimulation::gridState_t::POWERFLOW_COMPLETE) {
                std::cout << fileName << " did not complete power flow calculation\n";
                break;
            }
            ASSERT_EQ(gds->currentProcessState(),
                      gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
        }
        printf("%s load in %f powerflow in %f\n",
               mp.c_str(),
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
        int ii = 0;
        for (auto gsize : elements) {
            gds = std::make_unique<gridDynSimulation>();

            readerInfo ri;
            ri.addLockedDefinition("garraySize", std::to_string(gsize));
            gds->set("consoleprintlevel", "summary");
            auto start_t = std::chrono::high_resolution_clock::now();
            loadFile(gds.get(), testFile, &ri);
            gds->setFlag("no_powerflow_adjustments");
            auto stop_t = std::chrono::high_resolution_clock::now();
            load_time = (stop_t - start_t);
            ldtime[ii] = load_time.count();

            ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);

            start_t = std::chrono::high_resolution_clock::now();
            gds->powerflow();
            stop_t = std::chrono::high_resolution_clock::now();
            gds->updateLocalCache();
            pflow_time = (stop_t - start_t);
            pftime[ii] = pflow_time.count();
            int ss = gds->getInt("statesize");
            int nnz = gds->getInt("nonzeros");
            int rcount = gds->getInt("residcount");
            int jcount = gds->getInt("jaccount");
            printf("%d size=%d, nnz=%d,ld time=%f, pflow time=%f\n",
                   gsize,
                   ss,
                   nnz,
                   load_time.count(),
                   pflow_time.count());
            printf("%d residual calls, %d Jacobian call\n", rcount, jcount);
            auto bus = static_cast<gridBus*>(gds->findByUserID("bus", 10000000));
            printf("slack bus gen p=%f, gen q =%f\n",
                   bus->getGenerationReal(),
                   bus->getGenerationReactive());
            outfile << gsize << ", " << gsize * gsize << ", " << ss << ", " << nnz << ", ";
            outfile << rcount << ", " << jcount << ", ";
            outfile << load_time.count() << ", " << pflow_time.count();
            auto sc = gds->getSolverInterface("powerflow");
            outfile << ", " << sc->get("kintime") << ", " << sc->get("residtime") << ", "
                    << sc->get("jactime");
            outfile << ", " << sc->get("jac1time") << ", " << sc->get("kin1time") << "\n";
            std::vector<double> v;
            int cnt = gds->getVoltage(v);
            auto minV = minLoc(v);
            auto maxV = maxLoc(v);
            printf("cnt=%d vmin=%f at %d, vmax=%f at %d \n",
                   cnt,
                   minV.first,
                   minV.second,
                   maxV.first,
                   maxV.second);
            ++ii;
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

    gds = std::make_unique<gridDynSimulation>();

    readerInfo ri;
    ri.addLockedDefinition("garraySize", std::to_string(gsize));
    gds->set("consoleprintlevel", "summary");
    auto start_t = std::chrono::high_resolution_clock::now();
    loadFile(gds.get(), testFile, &ri);
    gds->setFlag("no_powerflow_adjustments");
    auto stop_t = std::chrono::high_resolution_clock::now();
    load_time = (stop_t - start_t);

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::STARTUP);

    start_t = std::chrono::high_resolution_clock::now();
    gds->powerflow();
    stop_t = std::chrono::high_resolution_clock::now();
    gds->updateLocalCache();
    pflow_time = (stop_t - start_t);

    int ss = gds->getInt("statesize");
    int nnz = gds->getInt("nonzeros");
    int rcount = gds->getInt("residcount");
    int jcount = gds->getInt("jaccount");
    printf("%d size=%d, nnz=%d,ld time=%f, pflow time=%f\n",
           gsize,
           ss,
           nnz,
           load_time.count(),
           pflow_time.count());
    printf("%d residual calls, %d Jacobian call\n", rcount, jcount);
    auto bus = static_cast<gridBus*>(gds->findByUserID("bus", 10000000));
    printf("slack bus gen p=%f, gen q =%f\n",
           bus->getGenerationReal(),
           bus->getGenerationReactive());

    gds->run();
}
