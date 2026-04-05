/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "griddyn/gridBus.h"
#include "griddyn/loads/ThreePhaseLoad.h"
#include "griddyn/loads/approximatingLoad.h"
#include "griddyn/loads/fDepLoad.h"
#include "griddyn/loads/fileLoad.h"
#include "griddyn/loads/gridLabDLoad.h"
#include "griddyn/loads/motorLoad5.h"
#include "griddyn/loads/sourceLoad.h"
#include "griddyn/loads/zipLoad.h"
#include "griddyn/simulation/diagnostics.h"
#include <cmath>

#include <gtest/gtest.h>

using namespace griddyn;
using namespace griddyn::loads;

static const std::string load_test_directory(GRIDDYN_TEST_DIRECTORY "/load_tests/");
static const std::string gridlabd_test_directory(GRIDDYN_TEST_DIRECTORY "/gridlabD_tests/");

class LoadTests: public gridLoadTestFixture, public ::testing::Test {
};

TEST_F(LoadTests, BasicLoadTest)
{
    ld1 = new zipLoad(1.1, -0.3);
    ld1->setFlag("no_pqvoltage_limit");
    double val;
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 1.1, 1e-6);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, -0.3, 1e-6);
    val = ld1->getRealPower(1.5);
    EXPECT_NEAR(val, 1.1, 1e-6);
    val = ld1->getReactivePower(1.5);
    EXPECT_NEAR(val, -0.3, 1e-6);

    ld1->set("p", 1.2);
    ld1->set("q", 0.234);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 1.2, 1e-6);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, 0.234, 1e-6);

    ld1->set("p", 0.0);
    ld1->set("q", 0.0);
    ld1->set("r", 2.0);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 1.2 * 1.2 / 2.0, 1e-6);

    ld1->set("r", 0.0);
    ld1->set("x", 2.0);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 0.0, 1e-6);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);
    val = ld1->getReactivePower(1.2);
    EXPECT_NEAR(val, 1.2 * 1.2 / 2.0, 1e-6);

    ld1->set("r", kBigNum);
    ld1->set("x", 0.0);
    ld1->set("ip", 1.0);
    ld1->set("iq", 2.0);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 1.2, 1e-6);
    val = ld1->getReactivePower(1.2);
    EXPECT_NEAR(val, 2.4, 1e-6);
    val = ld1->getRealPower(0.99);
    EXPECT_NEAR(val, 0.99, 1e-6);
    val = ld1->getReactivePower(0.99);
    EXPECT_NEAR(val, 1.98, 1e-6);

    ld1->set("r", kBigNum);
    ld1->set("x", 0.0);
    ld1->set("ip", 0.0);
    ld1->set("iq", 0.0);
    ld1->set("p", 1.0);
    ld1->set("pf", 0.9);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 1.0, 1e-4);
    val = ld1->getReactivePower(1.2);
    EXPECT_NEAR(val, 0.4843, 3e-4);
    ld1->set("p", 1.4);
    val = ld1->getReactivePower();
    EXPECT_NEAR(val, 0.6781, 3e-4);
}

TEST_F(LoadTests, LoadVoltageSweep)
{
    ld1 = new zipLoad(1.0, 0.0);
    std::vector<double> v;
    std::vector<double> out;
    ld1->set("vpqmin", 0.75);
    ld1->set("vpqmax", 1.25);

    for (double vt = 0.0; vt <= 1.5; vt += 0.001) {
        v.push_back(vt);
        out.push_back(ld1->getRealPower(vt));
    }
    v.push_back(1.5);
    EXPECT_NEAR(std::abs(out[400] - v[400] * v[400] / (0.75 * 0.75)), 0.0, 0.001);
    EXPECT_NEAR(std::abs(out[1350] - v[1350] * v[1350] / (1.25 * 1.25)), 0.0, 0.001);
    EXPECT_NEAR(std::abs(out[800] - 1.0), 0.0, 0.001);
    EXPECT_NEAR(std::abs(out[1249] - 1.0), 0.0, 0.001);
}

TEST_F(LoadTests, RampLoadTest)
{
    ld1 = new rampLoad();
    auto ldT = dynamic_cast<rampLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    double val;
    ld1->set("p", 0.5);
    ld1->pFlowInitializeA(timeZero, 0);
    ldT->set("dpdt", 0.01);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);
    ldT->setState(4.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.54, 1e-6);
    ld1->set("dpdt", 0.0);
    ld1->pFlowInitializeA(timeZero, 0);
    ldT->set("p", 0.0);
    ldT->set("q", -0.3);
    ldT->set("dqdt", -0.01);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, -0.3, 1e-6);
    ldT->setState(6.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, -0.36, 1e-6);
    ld1->set("dqdt", 0.0);
    ld1->set("q", 0.0);
    ld1->pFlowInitializeA(timeZero, 0);
    ldT->set("r", 1.0);
    ldT->set("drdt", 0.05);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 1.0, 1e-6);
    ldT->setState(2.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 1.44 / 1.1, 1e-6);
    ld1->set("drdt", 0.0);
    ld1->set("r", 0.0);
    ld1->pFlowInitializeA(timeZero, 0);
    ldT->set("x", 0.5);
    ldT->set("dxdt", -0.05);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, 2.0, 1e-6);
    ldT->setState(4.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getReactivePower(1.2);
    EXPECT_NEAR(val, 1.2 * 1.2 / 0.3, 1e-6);
    ld1->set("dxdt", 0.0);
    ld1->set("x", 0.0);
    ld1->set("r", kBigNum);
    ld1->pFlowInitializeA(timeZero, 0);
    ldT->set("ip", 0.5);
    ldT->set("dipdt", -0.01);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);
    ldT->setState(10.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 1.2 * 0.4, 1e-6);
    ld1->set("dipdt", 0.0);
    ld1->set("ip", 0.0);
    ld1->pFlowInitializeA(timeZero, 0);
    ldT->set("iq", 1.0);
    ldT->set("diqdt", 0.05);
    val = ld1->getReactivePower(1.0);
    EXPECT_NEAR(val, 1.0, 1e-6);
    ldT->setState(1.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getReactivePower(1.2);
    EXPECT_NEAR(val, 1.2 * 1.05, 1e-6);
    ld1->set("diqdt", 0.0);
    ld1->set("iq", 0.0);

    val = ld1->getReactivePower(1.2);
    EXPECT_NEAR(val, 0.0, 1e-8);
    val = ld1->getRealPower(1.2);
    EXPECT_NEAR(val, 0.0, 1e-6);
}

TEST_F(LoadTests, RandomLoadTest)
{
    ld1 = new sourceLoad(sourceLoad::sourceType::random);
    auto ldT = static_cast<sourceLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    ld1->set("p:trigger_dist", "constant");
    ldT->set("p:mean_t", 5.0);
    ld1->set("p:size_dist", "constant");
    ldT->set("p:mean_l", 0.3);
    ld1->set("p", 0.5);
    ld1->pFlowInitializeA(timeZero, 0);
    double val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);
    ld1->timestep(5.0, noInputs, cLocalSolverMode);
    val = ld1->getRealPower(1.1);
    EXPECT_NEAR(val, 0.8, 1e-6);
    ldT->reset();
    ld1->set("p:trigger_dist", "uniform");
    ldT->set("p:min_t", 2.0);
    ldT->set("p:max_t", 5.0);
    ld1->pFlowInitializeA(6.0, 0);
    auto src = ld1->find("p");
    ASSERT_NE(src, nullptr);
    auto otime = src->getNextUpdateTime();
    EXPECT_GE(otime, coreTime(8.0));
    EXPECT_LE(otime, coreTime(11.0));
    ld1->timestep(otime - 0.2, noInputs, cLocalSolverMode);
    val = ld1->getRealPower(1.1);
    EXPECT_NEAR(val, 0.8, 1e-6);
    ld1->timestep(otime + 0.2, noInputs, cLocalSolverMode);
    val = ld1->getRealPower(1.1);
    EXPECT_NEAR(val, 1.1, 1e-6);
    ld1->pFlowInitializeA(6.0, 0);
    auto ntime = src->getNextUpdateTime();
    EXPECT_NE(otime, ntime);
    ldT->set("p:seed", 0);
    ld1->pFlowInitializeA(6.0, 0);
    ntime = src->getNextUpdateTime();
    ldT->set("p:seed", 0);
    ld1->pFlowInitializeA(6.0, 0);
    otime = src->getNextUpdateTime();
    EXPECT_EQ(otime, ntime);
}

TEST_F(LoadTests, RandomLoadTest2)
{
    ld1 = new sourceLoad(sourceLoad::sourceType::random);
    auto ldT = static_cast<sourceLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    double val;
    ld1->set("p:trigger_dist", "constant");
    ldT->set("p:mean_t", 5.0);
    ld1->set("p:size_dist", "constant");
    ldT->set("p:mean_l", 0.5);
    ld1->set("p", 0.5);
    ldT->setFlag("p:interpolate");
    ld1->pFlowInitializeA(timeZero, 0);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);
    ldT->setState(2.0, nullptr, nullptr, cLocalSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.7, 1e-6);
    ldT->reset();
    ldT->setFlag("p:interpolate", false);
    ld1->set("p:size_dist", "uniform");
    ldT->set("p:min_l", 0.2);
    ldT->set("p:max_l", 0.5);
    ldT->set("p:seed", "");
    ld1->set("p", 0.5);
    ld1->pFlowInitializeA(6.0, 0);
    ld1->timestep(12.0, noInputs, cLocalSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_GE(val, 0.7);
    EXPECT_LE(val, 1.0);
}

TEST_F(LoadTests, PulseLoadTest2)
{
    ld1 = new sourceLoad(sourceLoad::sourceType::pulse);
    auto ldT = static_cast<sourceLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);

    ld1->set("p:type", "square");
    ldT->set("p:amplitude", 1.3);
    ld1->set("p:period", 5);
    ld1->pFlowInitializeA(timeZero, 0);
    double val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.0, 1e-6);
    ldT->timestep(1.0, noInputs, cLocalSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.0, 1e-6);
    ld1->timestep(2.0, noInputs, cLocalSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 1.3, 1e-6);
    ld1->timestep(4.0, noInputs, cLocalSolverMode);
    val = ldT->getRealPower(1.0);
    EXPECT_NEAR(val, 0.0, 1e-6);
}

TEST_F(LoadTests, FileLoadTest1)
{
    ld1 = new fileLoad();
    auto ldT = static_cast<fileLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    std::string fileName = load_test_directory + "FileLoadInfo.bin";
    ld1->set("file", fileName);
    ldT->setFlag("step");

    ld1->pFlowInitializeA(timeZero, 0);
    double val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.5, 1e-6);

    IOdata input{0, 0};
    ldT->timestep(12.0, input, cPflowSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.8, 1e-6);
    delete ld1;

    ld1 = new fileLoad();
    ldT = static_cast<fileLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);

    ld1->set("file", fileName);
    ldT->set("mode", "interpolate");
    ld1->pFlowInitializeA(timeZero, 0u);

    ldT->timestep(1.0, input, cPflowSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.53, 1e-6);
    ldT->timestep(10.0, input, cPflowSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.8, 1e-6);

    ldT->timestep(12.0, input, cPflowSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.78, 1e-6);

    ldT->timestep(50.0, input, cPflowSolverMode);
    val = ld1->getRealPower(1.0);
    EXPECT_NEAR(val, 0.6, 1e-6);
}

TEST_F(LoadTests, FileLoadTest2)
{
    std::string fileName = load_test_directory + "testLoad.bin";
    ld1 = new fileLoad("fload", fileName);
    auto ldT = static_cast<fileLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    ldT->set("column", "yp");
    ldT->set("scaling", 1.0);
    ldT->set("qratio", 0.3);
    gmlc::utilities::TimeSeries<> Tdata(fileName);
    ldT->pFlowInitializeA(timeZero, 0u);

    double val = ldT->getRealPower();
    auto tod = Tdata.data()[0];
    EXPECT_NEAR(val, tod, std::abs(tod) * 1e-6 + 1e-12);
}

#ifndef GRIDDYN_ENABLE_MPI
TEST_F(LoadTests, GridDynLoadTest1)
{
    std::string fileName = gridlabd_test_directory + "IEEE_13_mod.xml";

    auto gds = readSimXMLFile(fileName);

    auto bus = gds->getBus(1);
    auto gld = dynamic_cast<gridLabDLoad*>(bus->getLoad());

    ASSERT_NE(gld, nullptr);

    gds->run();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

TEST_F(LoadTests, MotorTest1)
{
    std::string fileName = load_test_directory + "motorload_test1.xml";

    auto gds = readSimXMLFile(fileName);

    gridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<motorLoad*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);

    gds->dynInitialize();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode);
    gds->run();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, MotorTest3)
{
    std::string fileName = load_test_directory + "motorload_test3.xml";

    auto gds = readSimXMLFile(fileName);

    gridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<motorLoad3*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode, 1e-8);
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    gds->run();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, MotorTest3Stall)
{
    std::string fileName = load_test_directory + "motorload_test3_stall.xml";

    auto gds = readSimXMLFile(fileName);

    gridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<motorLoad3*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode, 1e-8);
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    gds->run(2.5);
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    EXPECT_TRUE(mtld->checkFlag(motorLoad::stalled));
    gds->run();
    EXPECT_FALSE(mtld->checkFlag(motorLoad::stalled));
}

#ifdef ENABLE_IN_DEVELOPMENT_CASES
#    ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(LoadTests, MotorTest5)
{
    std::string fileName = std::string(LOAD_TEST_DIRECTORY "motorload_test5.xml");
    readerConfig::setPrintMode(0);
    auto gds = readSimXMLFile(fileName);

    gridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<motorLoad5*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::INITIALIZED);
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode);
    gds->run();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#    endif
#endif

TEST_F(LoadTests, FdepTest)
{
    std::string fileName = load_test_directory + "fdepLoad.xml";
    readerConfig::setPrintMode(0);
    auto gds = readSimXMLFile(fileName);

    gridBus* bus = gds->getBus(2);
    auto mtld = dynamic_cast<fDepLoad*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode);
    gds->run();
    requireStates(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, ApproxloadTest1)
{
    approximatingLoad apload("apload1");

    ld1 = new zipLoad("zload1");
    ld1->set("p", 0.4);
    ld1->set("q", 0.3);
    ld1->set("yp", 0.1);
    ld1->set("yq", -0.12);
    ld1->set("ip", 0.03);
    ld1->set("iq", 0.06);
    apload.add(ld1);
    apload.pFlowInitializeA(0, 0);
    apload.pFlowInitializeB();
    auto p = apload.get("p");
    auto q = apload.get("q");
    auto yp = apload.get("yp");
    auto yq = apload.get("yq");
    auto ip = apload.get("ip");
    auto iq = apload.get("iq");

    EXPECT_NEAR(p, 0.4, 0.001);
    EXPECT_NEAR(q, 0.3, 0.001);
    EXPECT_NEAR(yp, 0.1, 0.001);
    EXPECT_NEAR(yq, -0.12, 0.001);
    EXPECT_NEAR(ip, 0.03, 0.001);
    EXPECT_NEAR(iq, 0.06, 0.001);

    ld1->set("p", 0.5);
    ld1->set("q", -0.1);
    ld1->set("yp", 0.13);
    ld1->set("yq", -0.12);
    ld1->set("ip", 0);
    ld1->set("iq", 0.23);

    apload.updateA(2);
    apload.updateB();
    p = apload.get("p");
    q = apload.get("q");
    yp = apload.get("yp");
    yq = apload.get("yq");
    ip = apload.get("ip");
    iq = apload.get("iq");

    EXPECT_NEAR(p, 0.5, 0.001);
    EXPECT_NEAR(q, -0.1, 0.001);
    EXPECT_NEAR(yp, 0.13, 0.001);
    EXPECT_NEAR(yq, -0.12, 0.001);
    EXPECT_NEAR(ip, 0.0, 1e-5);
    EXPECT_NEAR(iq, 0.23, 0.001);
    ld1 = nullptr;
}

TEST_F(LoadTests, Simple3PhaseLoadTest)
{
    auto ld3 = std::make_unique<ThreePhaseLoad>();
    ld3->setPa(1.1);
    ld3->setPb(1.2);
    ld3->setPc(1.3);

    auto P = ld3->getRealPower();
    EXPECT_NEAR(P, 3.6, 1e-5);

    ld3->setQa(0.1);
    ld3->setQb(0.3);
    ld3->setQc(0.62);

    auto Q = ld3->getReactivePower();
    EXPECT_NEAR(Q, 1.02, 1e-5);

    ld3->setLoad(3.0);
    EXPECT_NEAR(ld3->get("pa"), 1.0, 1e-7);

    ld3->set("pb", 0.5);
    ld3->set("pc", 0.4);

    P = ld3->getRealPower();
    EXPECT_NEAR(P, 1.9, 1e-5);

    auto res = ld3->getRealPower3Phase();
    EXPECT_EQ(res.size(), 3u);
    EXPECT_NEAR(res[0], 1.0, 1e-7);
    EXPECT_NEAR(res[1], 0.5, 1e-7);
    EXPECT_NEAR(res[2], 0.4, 1e-7);

    auto res2 = ld3->getRealPower3Phase(phase_type_t::pnz);
    EXPECT_EQ(res2.size(), 3u);
    EXPECT_NEAR(res2[0], 1.9, 1e-5);

    ld3->setLoad(2.7, 0.3);
    res = ld3->getRealPower3Phase();
    EXPECT_NEAR(res[0], 0.9, 1e-7);
    EXPECT_NEAR(res[1], 0.9, 1e-7);
    EXPECT_NEAR(res[2], 0.9, 1e-7);

    res = ld3->getRealPower3Phase(phase_type_t::pnz);
    EXPECT_NEAR(res[0], 2.7, 1e-7);
    EXPECT_NEAR(res[1], 0.0, 1e-7);
    EXPECT_NEAR(res[2], 0.0, 1e-7);

    res = ld3->getReactivePower3Phase(phase_type_t::pnz);
    EXPECT_NEAR(res[0], 0.3, 1e-7);
    EXPECT_NEAR(res[1], 0.0, 1e-7);
    EXPECT_NEAR(res[2], 0.0, 1e-7);
}

TEST_F(LoadTests, Secondary3PhaseLoadTest)
{
    auto ld3 = std::make_unique<ThreePhaseLoad>();

    ld3->setLoad(5.0, 1.0);

    ld3->set("imaga", 3.0);
    auto Pa = ld3->get("pa");
    ld3->set("imaga", 6.0);
    auto Pa2 = ld3->get("pa");
    EXPECT_NEAR(Pa * 2.0, Pa2, 1e-5);
}
