/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmi/fmiGDinfo.h"
#include "fmi/fmi_import/fmiImport.h"
#include "fmi/fmi_import/fmiObjects.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/simulation/diagnostics.h"
#include <gtest/gtest.h>

#include <filesystem>

// test case for coreObject object

#include "../gtestHelper.h"
#include "fmi/fmi_models/fmiMELoad3phase.h"
#include "griddyn/loads/approximatingLoad.h"

static const std::string fmi_test_directory(GRIDDYN_TEST_DIRECTORY "/fmi_tests/");
static const std::string fmu_directory(GRIDDYN_TEST_DIRECTORY "/fmi_tests/test_fmus/");

// create a test fixture that makes sure everything gets deleted properly

using namespace std::filesystem;
class FmiTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(FmiTests, TestFmiXml)
{
    std::string fmu = fmu_directory + "Rectifier.fmu";
    path rectDir(fmu_directory + "Rectifier");
    fmiLibrary rectFmu(fmu);
    EXPECT_TRUE(rectFmu.isXmlLoaded());
    EXPECT_EQ(rectFmu.getCounts("states"), 4);
    EXPECT_EQ(rectFmu.getCounts("outputs"), 8);
    EXPECT_EQ(rectFmu.getCounts("parameters"), 23);
    EXPECT_EQ(rectFmu.getCounts("unit"), 13);
    auto vars = rectFmu.getCounts("variables");
    EXPECT_EQ(vars, 190);
    EXPECT_EQ(rectFmu.getCounts("locals"), vars - 23 - 8);
    rectFmu.close();
    ASSERT_TRUE(exists(rectDir)) << "fmu directory does not exist";
    try {
        remove_all(rectDir);
    }
    catch (filesystem_error const& e) {
        ADD_FAILURE() << "unable to remove the folder: " << e.what();
    }

    // test second fmu with different load mechanics
    std::string fmu2 = fmu_directory + "Rectifier2.fmu";
    path rectDir2(fmu_directory + "Rectifier2");
    fmiLibrary rect2Fmu(fmu2, rectDir2.string());
    EXPECT_TRUE(rect2Fmu.isXmlLoaded());
    EXPECT_EQ(rect2Fmu.getCounts("states"), 4);
    EXPECT_EQ(rect2Fmu.getCounts("outputs"), 1);
    EXPECT_EQ(rect2Fmu.getCounts("parameters"), 0);
    EXPECT_EQ(rect2Fmu.getCounts("units"), 2);
    vars = rect2Fmu.getCounts("variables");
    EXPECT_EQ(vars, 61);
    EXPECT_EQ(rect2Fmu.getCounts("locals"), vars - 2);

    rect2Fmu.close();
    ASSERT_TRUE(exists(rectDir2)) << "fmu directory does not exist";
    try {
        remove_all(rectDir2);
    }
    catch (filesystem_error const& e) {
        ADD_FAILURE() << "unable to remove the folder: " << e.what();
    }
}

#if defined _WIN32 && defined _WIN64
TEST_F(FmiTests, TestFmiLoadShared)
{
    std::string fmu = fmu_directory + "Rectifier.fmu";
    path rectDir(fmu_directory + "Rectifier");
    {
        fmiLibrary rectFmu(fmu);
        rectFmu.loadSharedLibrary();
        ASSERT_TRUE(rectFmu.isSoLoaded());

        auto b = rectFmu.createModelExchangeObject("rctf");
        ASSERT_TRUE(b);
        b->setMode(fmuMode::initializationMode);
        auto v = b->get<double>("VAC");
        EXPECT_NEAR(v, 400.0, 4e-2 + 1e-12);
        auto phase = b->get<double>("SineVoltage1.phase");
        EXPECT_NEAR(phase, 0.0, 1e-12);

        b->set("VAC", 394.23);
        v = b->get<double>("VAC");
        EXPECT_NEAR(v, 394.23, 3.9423e-2 + 1e-12);

        b.reset();
        rectFmu.close();
    }
    try {
        remove_all(rectDir);
    }
    catch (filesystem_error const& e) {
        ADD_FAILURE() << "unable to remove the folder: " << e.what();
    }
}

#endif

#if defined _WIN32 && !defined _WIN64
TEST_F(FmiTests, Test3phaseFmu)
{
    std::string fmu = fmu_directory + "DUMMY_0CYMDIST.fmu";
    fmiLibrary loadFmu(fmu);
    EXPECT_TRUE(loadFmu.isXmlLoaded());
    auto states = loadFmu.getCounts("states");
    EXPECT_EQ(states, 0);
    auto outputs = loadFmu.getCounts("outputs");
    EXPECT_EQ(outputs, 6);
    auto inputs = loadFmu.getCounts("inputs");
    EXPECT_EQ(inputs, 6);
    // auto param = loadFmu.getCounts("parameters");
    // auto units = loadFmu.getCounts("unit");
    auto fm = loadFmu.createModelExchangeObject("meload");
    ASSERT_TRUE(fm);
    double inp[6];
    double out[6];
    double out2[6];
    fm->setMode(fmuMode::continuousTimeMode);
    fm->getCurrentInputs(inp);
    inp[0] = 1.0;
    inp[1] = 1.0;
    inp[2] = 1.0;
    fm->setInputs(inp);
    fm->getCurrentInputs(inp);
    EXPECT_EQ(inp[2], 1.0);
    fm->getOutputs(out);
    inp[0] = 0.97;
    inp[1] = 0.97;
    inp[2] = 0.97;
    auto res = fm->get<double>("a0");
    EXPECT_EQ(res, 1.0);
    fm->setInputs(inp);
    fm->getOutputs(out2);
    EXPECT_LT(out2[0], out[0]);
    loadFmu.close();
}

TEST_F(FmiTests, TestFmiApproxLoad)
{
    using namespace griddyn::loads;
    approximatingLoad apload("apload1");

    auto ld1 = new griddyn::fmi::fmiMELoad3phase("fmload1");
    std::string fmu = fmu_directory + "DUMMY_0CYMDIST.fmu";
    ld1->set("fmu", fmu);
    apload.add(ld1);
    ld1->set("a2", 0.0);
    ld1->set("b2", 0);
    ld1->set("c2", 0.0);
    ld1->setFlag("current_output", true);
    apload.pFlowInitializeA(0, 0);
    apload.pFlowInitializeB();
    auto p = apload.get("p");
    auto q = apload.get("q");
    auto ip = apload.get("ip");
    auto iq = apload.get("iq");
    auto yp = apload.get("yp");
    auto yq = apload.get("yq");

    EXPECT_NEAR(p, 0.0, 1e-4);
    EXPECT_NEAR(ip, 3.0, 3e-4 + 1e-12);
    EXPECT_NEAR(yp, 3.0, 3e-4 + 1e-12);

    EXPECT_NEAR(q, 0.0, 1e-4);
    EXPECT_NEAR(iq, 0.0, 1e-4);
    EXPECT_NEAR(yq, 0.0, 1e-4);

    ld1->set("a0", 2.0);
    ld1->set("b0", 2.0);
    ld1->set("c0", 2.0);
    apload.updateA(0);
    apload.updateB();
    auto p2 = apload.get("p");
    auto ip2 = apload.get("ip");
    auto yp2 = apload.get("yp");
    EXPECT_NEAR(p2, 0.0, 1e-4);
    EXPECT_NEAR(ip * 2.0, ip2, std::abs(ip2) * 2e-7 + 1e-12);
    EXPECT_NEAR(yp, yp2, std::abs(yp2) * 1e-5 + 1e-12);

    ld1->set("angle0", 10, units::deg);
    apload.updateA(0);
    apload.updateB();
    auto iq2 = apload.get("iq");
    auto yq2 = apload.get("yq");

    EXPECT_GT(std::abs(iq2), 0.001);
    EXPECT_NEAR(iq2 / 2.0, yq2, std::abs(yq2) * 1e-6 + 1e-12);
    ld1 = nullptr;
}

TEST_F(FmiTests, TestFmiApproxLoadXml)
{
    using namespace griddyn::loads;
    std::string fileName = fmi_test_directory + "approxLoad_testfmi.xml";

    gds = griddyn::readSimXMLFile(fileName);
    ASSERT_TRUE(gds);
    auto obj = gds->getBus(1);
    ASSERT_NE(obj, nullptr);
    auto apload = dynamic_cast<approximatingLoad*>(gds->getBus(1)->getLoad(0));
    ASSERT_NE(apload, nullptr);

    auto ld1 = dynamic_cast<griddyn::Load*>(apload->getSubObject("subobject", 0));
    ASSERT_NE(ld1, nullptr);
    ld1->set("a2", 0.0);
    ld1->set("b2", 0);
    ld1->set("c2", 0.0);
    ld1->setFlag("current_output", true);
    gds->pFlowInitialize();
    auto p = apload->get("p");
    auto q = apload->get("q");
    auto ip = apload->get("ip");
    auto iq = apload->get("iq");
    auto yp = apload->get("yp");
    auto yq = apload->get("yq");

    EXPECT_NEAR(p, 0.0, 1e-4);
    EXPECT_NEAR(ip, 3.0, 3e-4 + 1e-12);
    EXPECT_NEAR(yp, 3.0, 3e-4 + 1e-12);

    EXPECT_NEAR(q, 0.0, 1e-4);
    EXPECT_NEAR(iq, 0.0, 1e-4);
    EXPECT_NEAR(yq, 0.0, 1e-4);

    ld1 = nullptr;
}

#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FmiTests, FmiTest1)
{
    loadFmiLibrary();
    // test the loading of a single bus
    auto tsb = new fmiLoad(FMU_LOC "ACMotorFMU.fmu");
    tsb->set("a_in", "#");
    stringVec b;
    tsb->getParameterStrings(b, paramStringType::numeric);
    tsb->set("a_in", 0.7);
    tsb->pFlowInitializeA(0, 0);
    auto outP = tsb->getRealPower();
    auto outQ = tsb->getReactivePower();
    tsb->dynInitializeA(0, 0);
    IOdata oset;
    tsb->dynInitializeB({1.0, 0}, oset);

    auto out = tsb->timestep(6, {1.0, 0}, cLocalSolverMode);
    outP = tsb->getRealPower();
    outQ = tsb->getReactivePower();
    EXPECT_NEAR(out, 0.7, 7e-4 + 1e-12);
    tsb->set("a_in", 0.9);
    out = tsb->timestep(14, {1.0, 0}, cLocalSolverMode);
    outP = tsb->getRealPower();
    outQ = tsb->getReactivePower();
    EXPECT_NEAR(out, 0.9, 9e-4 + 1e-12);
    tsb->set("a_in", 0.6);
    out = tsb->timestep(22, {1.0, 0}, cLocalSolverMode);
    outP = tsb->getRealPower();
    outQ = tsb->getReactivePower();
    EXPECT_NEAR(out, 0.6, 6e-4 + 1e-12);
    delete tsb;
}

TEST_F(FmiTests, FmiXml1)
{
    std::string fileName = std::string(FMI_TEST_DIRECTORY "fmimotorload_test1.xml");

    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    int retval = gds->pFlowInitialize();
    EXPECT_EQ(retval, 0);

    int mmatch = JacobianCheck(gds, cPflowSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cPflowSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    std::vector<double> v;
    gds->getVoltage(v);
    mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    mmatch = JacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(FmiTests, FmiXml2)
{
    std::string fileName = std::string(FMI_TEST_DIRECTORY "fmiload_test2.xml");

    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    int retval = gds->pFlowInitialize();
    EXPECT_EQ(retval, 0);

    int mmatch = JacobianCheck(gds, cPflowSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cPflowSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    std::vector<double> v;
    gds->getVoltage(v);
    mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    mmatch = JacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(FmiTests, FmiXml3)
{
    std::string fileName = std::string(FMI_TEST_DIRECTORY "fmiload_test3.xml");

    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    int retval = gds->pFlowInitialize();
    EXPECT_EQ(retval, 0);

    int mmatch = JacobianCheck(gds, cPflowSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cPflowSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    std::vector<double> v;
    gds->getVoltage(v);
    mmatch = residualCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);
    mmatch = JacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    ASSERT_EQ(mmatch, 0);

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

TEST_F(FmiTests, FmiArray)
{
    std::string fileName = std::string(FMI_TEST_DIRECTORY "block_grid.xml");

    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    int retval = gds->pFlowInitialize();
    EXPECT_EQ(retval, 0);

    int cnt = gds->getInt("totalbuscount");
    if (cnt < 200) {
        int mmatch = JacobianCheck(gds, cPflowSolverMode);
        if (mmatch > 0) {
            printStateNames(gds, cPflowSolverMode);
        }
        ASSERT_EQ(mmatch, 0);
    }
    gds->powerflow();

    requireState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    gds->dynInitialize();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    std::vector<double> v;
    gds->getVoltage(v);

    auto bus = static_cast<gridBus*>(gds->find("bus_1_1"));
    printf("slk bus p=%f min v= %f\n",
           bus->getGenerationReal(),
           *std::min_element(v.begin(), v.end()));
}

#endif
