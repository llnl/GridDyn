/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/links/acLine.h"
#include "griddyn/links/adjustableTransformer.h"
#include <array>
#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <utility>

#include <boost/filesystem.hpp>
// test case for coreObject object

#define INPUT_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/input_tests/"

using namespace griddyn;
using namespace gmlc::utilities;

class InputTests: public gridDynSimulationTestFixture, public ::testing::Test {};

static const std::vector<std::pair<std::string, std::array<int, 2>>> baseCDFcase{
    {"ieee14_act.cdf", {{14, 20}}},
    {"ieee30_act.cdf", {{30, 41}}},
    {"ieee57_act.cdf", {{57, 80}}},
    {"ieee118_act.cdf", {{118, 186}}},
    {"ieee300.cdf", {{300, 411}}},
    {"IEEE39.raw", {{39, 46}}},
    {std::string(INPUT_TEST_DIRECTORY "testCSV5k.xml"), {{5000, 6279}}},
};

TEST_F(InputTests, DISABLED_TestPowerFlowInputs)
{
    for (size_t caseIndex = 0; caseIndex < baseCDFcase.size(); ++caseIndex) {
        std::vector<double> volts1;
        std::vector<double> ang1;
        std::vector<double> volts2;
        std::vector<double> ang2;
        std::vector<double> P1;
        std::vector<double> P2;
        std::vector<double> Q1;
        std::vector<double> Q2;

        auto mp = baseCDFcase[caseIndex];

        SCOPED_TRACE(mp.first);
        gds = std::make_unique<gridDynSimulation>();
        std::string fileName;
        if (mp.first.length() > 25) {
            fileName = mp.first;
        } else {
            fileName = std::string(IEEE_TEST_DIRECTORY) + mp.first;
        }

        loadFile(gds, fileName);
        requireState(gridDynSimulation::gridState_t::STARTUP);
        int count = gds->getInt("totalbuscount");
        EXPECT_EQ(count, mp.second[0]);
        count = gds->getInt("totallinkcount");
        EXPECT_EQ(count, mp.second[1]);
        auto ct = gds->getVoltage(volts1);
        gds->getAngle(ang1);
        EXPECT_EQ(ct, static_cast<count_t>(ang1.size()));
        gds->pFlowInitialize();
        requireState(gridDynSimulation::gridState_t::INITIALIZED);
        gds->updateLocalCache();
        gds->getBusGenerationReal(P1);
        gds->getBusGenerationReactive(Q1);

        gds->powerflow();
        if (gds->currentProcessState() != gridDynSimulation::gridState_t::POWERFLOW_COMPLETE) {
            std::cout << fileName << " did not complete power flow calculation" << '\n';
        }
        requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

        gds->getVoltage(volts2);
        gds->getAngle(ang2);
        gds->getBusGenerationReal(P2);
        gds->getBusGenerationReactive(Q2);
        std::function<void(size_t, double, double)> vfunc =
            [=](size_t index, double v1, double v2) {
                std::cout << mp.first << " Voltage difference bus " << index + 1 << "::" << v1
                          << " vs. " << v2 << "::" << v1 - v2 << '\n';
            };

        auto vdiff = countDiffsCallback(volts1, volts2, 0.0008, vfunc);

        std::function<void(size_t, double, double)> afunc =
            [=](size_t index, double v1, double v2) {
                std::cout << mp.first << " Angle difference bus " << index + 1
                          << "::" << v1 * 180.0 / kPI << " vs. " << v2 * 180.0 / kPI
                          << "::" << (v1 - v2) * 180.0 / kPI << '\n';
            };
        auto adiff = countDiffsCallback(ang1, ang2, 0.0009, afunc);

        std::function<void(size_t, double, double)> Pfunc =
            [=](size_t index, double v1, double v2) {
                std::cout << mp.first << " Power difference-- bus " << index + 1 << "::" << v1
                          << " vs. " << v2 << '\n';
            };
        auto pdiff = countDiffsIfValidCallback(P1, P2, 0.01, Pfunc);
        std::function<void(size_t, double, double)> Qfunc =
            [=](size_t index, double v1, double v2) {
                std::cout << mp.first << " Q difference-- bus " << index + 1 << "::" << v1
                          << " vs. " << v2 << "::" << v1 - v1 << '\n';
            };
        auto qdiff = countDiffsIfValidCallback(Q1, Q2, 0.01, Qfunc);
        EXPECT_EQ(vdiff, 0u);
        EXPECT_EQ(adiff, 0u);
        EXPECT_EQ(pdiff, 0u);
        EXPECT_EQ(qdiff, 0u);
        if (qdiff > 0) {
            printf("%f vs %f diff %f\n", sum(Q1), sum(Q2), sum(Q1) - sum(Q2));
        }

        if (mp.first == "ieee300.cdf") {
            gds->reset(reset_levels::voltage_angle);

            for (int ii = 0; ii < 300; ++ii) {
                gridBus* bus = gds->getBus(ii);
                if (bus->getType() == gridBus::busType::PQ) {
                    bus->set("voltage", 1.0);
                }
            }
            int cnt = 0;
            for (int ii = 0; ii < 411; ++ii) {
                Link* lnk = gds->getLink(ii);
                if (dynamic_cast<links::adjustableTransformer*>(lnk)) {
                    cnt++;
                    if ((cnt >= 2) & (cnt <= 3)) {
                        lnk->reset(reset_levels::full);
                        lnk->set("center", "target");
                        break;
                    }
                }
            }
        } else {
            gds->reset(reset_levels::full);
        }

        gds->powerflow();
        if (gds->currentProcessState() != gridDynSimulation::gridState_t::POWERFLOW_COMPLETE) {
            std::cout << fileName << " did not complete power flow calculation 2" << '\n';
        }
        requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

        gds->getVoltage(volts1);
        gds->getAngle(ang1);
        gds->getBusGenerationReal(P2);
        gds->getBusGenerationReactive(Q2);

        vdiff = countDiffs(volts1, volts2, 0.0005);
        adiff = countDiffs(ang1, ang2, 0.0009);

        EXPECT_EQ(vdiff, 0u);
        EXPECT_EQ(adiff, 0u);
    }
}

// if more checks are added the data::xrange(X) must be changed as well

static const std::vector<griddyn::stringVec> compareCases{
    {"ieee118_act.cdf", "ieee118.psp", "IEEE 118 Bus.EPC"},
    {"ieee14_act.cdf", "IEEE 14 bus.epc", "IEEE 14 bus.raw"},
    {"IEEE39.raw", "ieee39_v29.raw"},
    {"powerflowWECC179_v30.raw", "powerflowWECC179_v31.raw", "powerflowWECC179_v32.raw"},
    {"ieee30_no_limit.cdf", "testCSV.xml"},
    // {"powerflowWECC1.raw","WECC179_powerflow.m"},
};

TEST_F(InputTests, DISABLED_CompareCases)
{
    for (size_t caseIndex = 0; caseIndex < compareCases.size(); ++caseIndex) {
        std::vector<double> volts1;
        std::vector<double> ang1;
        std::vector<double> volts2;
        std::vector<double> ang2;

        gds = std::make_unique<gridDynSimulation>();

        auto caseSet = compareCases[caseIndex];
        SCOPED_TRACE(caseSet[0]);
        std::string fileName = std::string(IEEE_TEST_DIRECTORY) + caseSet[0];
        if (!boost::filesystem::exists(fileName)) {
            fileName = std::string(INPUT_TEST_DIRECTORY) + caseSet[0];
        }
        loadFile(gds, fileName);

        int bcount = gds->getInt("totalbuscount");
        int lcount = gds->getInt("totallinkcount");

        gds->powerflow();

        gds->getVoltage(volts1);
        gds->getAngle(ang1);

        for (size_t ns = 1; ns < caseSet.size(); ++ns) {
            std::string fname2 = caseSet[ns];
            std::string nf = caseSet[ns];
            gds2 = std::make_unique<gridDynSimulation>();
            if (fname2.size() < 25) {
                fname2 = std::string(IEEE_TEST_DIRECTORY) + nf;
                if (!boost::filesystem::exists(fname2)) {
                    fname2 = std::string(INPUT_TEST_DIRECTORY) + nf;
                }
            }

            loadFile(gds2, fname2);

            int count = gds2->getInt("totalbuscount");
            EXPECT_EQ(count, bcount);
            for (index_t kk = 0; kk < count; ++kk) {
                auto b2 = gds2->getBus(kk);
                auto b1 = gds->getBus(kk);
                auto cmp = compareBus(b2, b1, false);
                if (cmp == false) {
                }
                b2->setVoltageAngle(volts1[kk], ang1[kk]);
            }
            count = gds2->getInt("totallinkcount");
            EXPECT_EQ(count, lcount);

            gds2->powerflow();

            ASSERT_EQ(gds2->currentProcessState(),
                      gridDynSimulation::gridState_t::POWERFLOW_COMPLETE)
                << fname2 << " failed to complete";

            gds2->getVoltage(volts2);
            gds2->getAngle(ang2);

            int vdiff = 0;
            int adiff = 0;

            for (size_t kk = 0; kk < volts1.size(); ++kk) {
                if (std::abs(volts1[kk] - volts2[kk]) > 0.0008) {
                    std::cout << caseSet[0] << " vs. " << nf << " Voltage difference bus " << kk + 1
                              << "::" << volts1[kk] << " vs. " << volts2[kk] << '\n';
                    vdiff++;
                }

                if (std::abs(ang1[kk] - ang2[kk]) > 0.0009) {
                    std::cout << caseSet[0] << " vs. " << nf << " Angle difference-- bus " << kk + 1
                              << "::" << ang1[kk] * 180.0 / kPI << " vs. " << ang2[kk] * 180.0 / kPI
                              << "::" << std::abs(ang1[kk] - ang2[kk]) * 180.0 / kPI << " deg"
                              << '\n';
                    adiff++;
                }
            }
            EXPECT_EQ(vdiff, 0);
            EXPECT_EQ(adiff, 0);
        }
    }
}

/* format { fileName , {buscount, linkcount, eventcount, 0=powerflow 1=dynamic}}*/

static const std::vector<std::pair<std::string, std::array<int, 4>>> executionCases{
    {std::string(MATLAB_TEST_DIRECTORY "case4gs.m"), {{0, 4, 4, 0}}},
    // { std::string(MATLAB_TEST_DIRECTORY "d_003.m"), { { 0, 3, 3, 0 } } },
    // { std::string(INPUT_TEST_DIRECTORY "test_mat_dyn.xml"), { { 1, 9, 9, 2 } } },
    {std::string(INPUT_TEST_DIRECTORY "test_2m4bDyn_inputchange.xml"), {{1, 0, 0, 0}}},
    {std::string(INPUT_TEST_DIRECTORY "testIEEE39dynamic.xml"), {{1, 39, 0, 0}}},
    //  { std::string(INPUT_TEST_DIRECTORY "testIEEE39dynamic_relay.xml"), { { 1, 39, 0, 0 } } },
    //{ std::string(INPUT_TEST_DIRECTORY "180busdyn_test.xml"),{ { 1, 179, 0, 1 } } },
};

TEST_F(InputTests, DISABLED_InputExecTest)
{
    for (size_t caseIndex = 0; caseIndex < executionCases.size(); ++caseIndex) {
        int count;
        auto mp = executionCases[caseIndex];
        auto fileName = mp.first;
        SCOPED_TRACE(fileName);

        gds = std::make_unique<gridDynSimulation>();

        loadFile(gds, fileName);
        requireState(gridDynSimulation::gridState_t::STARTUP);

        if (mp.second[1] > 0) {
            count = gds->getInt("totalbuscount");
            EXPECT_EQ(count, mp.second[1]);
        }
        if (mp.second[2] > 0) {
            count = gds->getInt("totallinkcount");
            EXPECT_EQ(count, mp.second[2]);
        }
        if (mp.second[3] > 0) {
            count = gds->getInt("eventcount");
            EXPECT_EQ(count, mp.second[3]);
        }
        if (mp.second[0] == 0) {
            gds->powerflow();
            requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
        } else if (mp.second[0] == 1) {
            gds->run();
            requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
        }
    }
}
