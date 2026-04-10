/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../../test/gtestHelper.h"
#include "core/coreOwningPtr.hpp"
#include "fileInput/readerInfo.h"
#include "fmi/fmi_import/fmiImport.h"
#include "fmi/fmi_import/fmiObjects.h"
#include "fmi_export/fmiCollector.h"
#include "fmi_export/fmiCoordinator.h"
#include "fmi_export/fmiEvent.h"
#include "fmi_export/fmiRunner.h"
#include "fmi_export/fmuBuilder.h"
#include "fmi_export/loadFMIExportObjects.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/loads/ThreePhaseLoad.h"
#include "griddyn/simulation/diagnostics.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <string>

static const std::string fmi_test_directory(GRIDDYN_TEST_DIRECTORY "/fmi_export_tests/");

using griddyn::coreTime;
using griddyn::gridBus;
using griddyn::gridDynSimulation;
using griddyn::gridDynSimulationTestFixture;
using griddyn::kNullLocation;
using griddyn::loadFile;
using griddyn::make_owningPtr;
using griddyn::readerInfo;
using griddyn::fmi::fmiLibrary;
using griddyn::fmi::fmuMode;
using griddyn::loads::ThreePhaseLoad;
using std::filesystem::exists;
using std::filesystem::path;
using std::filesystem::remove;
using std::filesystem::remove_all;

class FmiExportTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(FmiExportTests, TestFmiEvents)
{
    auto fmiCon = make_owningPtr<fmi::fmiCoordinator>();

    gds = std::make_unique<gridDynSimulation>();
    gds->add(fmiCon.get());

    auto fmc = gds->find("fmiCoordinator");
    ASSERT_NE(fmc, nullptr);
    EXPECT_EQ(fmc->getID(), fmiCon->getID());

    readerInfo ri;
    loadFmiExportReaderInfoDefinitions(ri);
    std::string file = fmi_test_directory + "fmi_export_fmiinput.xml";
    loadFile(gds.get(), file, &ri, "xml");

    auto& ev = fmiCon->getInputs();
    ASSERT_EQ(ev.size(), 1U);

    EXPECT_EQ(ev[0].second.name, "power");
}

TEST_F(FmiExportTests, TestFmiOutput)
{
    auto fmiCon = make_owningPtr<fmi::fmiCoordinator>();

    gds = std::make_unique<gridDynSimulation>();
    gds->add(fmiCon.get());

    auto fmc = gds->find("fmiCoordinator");
    ASSERT_NE(fmc, nullptr);
    EXPECT_EQ(fmc->getID(), fmiCon->getID());

    readerInfo ri;
    loadFmiExportReaderInfoDefinitions(ri);
    std::string file = fmi_test_directory + "fmi_export_fmioutput.xml";
    loadFile(gds.get(), file, &ri, "xml");

    auto& ev = fmiCon->getOutputs();
    ASSERT_EQ(ev.size(), 1U);

    EXPECT_EQ(ev[0].second.name, "load");
}

TEST_F(FmiExportTests, TestFmiSimulation)
{
    auto fmiCon = make_owningPtr<fmi::fmiCoordinator>();

    gds = std::make_unique<gridDynSimulation>();
    gds->add(fmiCon.get());

    auto fmc = gds->find("fmiCoordinator");
    ASSERT_NE(fmc, nullptr);
    EXPECT_EQ(fmc->getID(), fmiCon->getID());

    readerInfo ri;
    loadFmiExportReaderInfoDefinitions(ri);
    std::string file = fmi_test_directory + "simulation.xml";
    loadFile(gds.get(), file, &ri, "xml");

    auto& ev = fmiCon->getInputs();
    ASSERT_EQ(ev.size(), 1U);

    EXPECT_EQ(ev[0].second.name, "power");

    auto& ev2 = fmiCon->getOutputs();
    ASSERT_EQ(ev2.size(), 1U);

    EXPECT_EQ(ev2[0].second.name, "load");

    gds->run(30);
    checkState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

TEST_F(FmiExportTests, TestFmiRunner)
{
    auto runner = std::make_unique<fmi::fmiRunner>("testsim", fmi_test_directory, nullptr);

    runner->simInitialize();
    runner->UpdateOutputs();

    auto ld_vr = runner->findVR("load");
    auto out = runner->Get(ld_vr);
    EXPECT_NEAR(out, 0.5, 0.000001);

    auto in_vr = runner->findVR("power");
    runner->Set(in_vr, 0.6);
    runner->Step(1.0);
    out = runner->Get(ld_vr);
    EXPECT_NEAR(out, 0.6, 0.00001);
    runner->Set(in_vr, 0.7);
    runner->Step(2.0);
    out = runner->Get(ld_vr);
    EXPECT_NEAR(out, 0.7, 0.00001);
    runner->Finalize();
}

namespace {
void generateFMU(const std::string& target, const std::string& inputfile)
{
    auto builder = std::make_unique<fmi::fmuBuilder>();

    builder->InitializeFromString("--buildfmu=\"" + target + "\" \"" + inputfile + "\"");

    builder->MakeFmu();

    EXPECT_TRUE(exists(target));
}
}  // namespace

TEST_F(FmiExportTests, BuildGriddynFmu)
{
    generateFMU(fmi_test_directory + "griddyn.fmu", fmi_test_directory + "simulation.xml");
}

TEST_F(FmiExportTests, LoadGriddynFmu)
{
    std::string fmu = fmi_test_directory + "griddyn.fmu";

    if (!exists(fmu)) {
        generateFMU(fmu, fmi_test_directory + "simulation.xml");
    }
    ASSERT_TRUE(exists(fmu)) << "unable to generate FMU";
    path gdDir(fmi_test_directory + "griddyn");
    fmiLibrary gdFmu(fmu);
    gdFmu.loadSharedLibrary();
    ASSERT_TRUE(gdFmu.isSoLoaded());

    auto types = gdFmu.getTypes();
    EXPECT_EQ(types, "default");
    auto ver = gdFmu.getVersion();
    EXPECT_EQ(ver, "2.0");

    auto b = gdFmu.createCoSimulationObject("gd1");

    auto b2 = gdFmu.createCoSimulationObject("gd2");

    EXPECT_TRUE(static_cast<bool>(b));
    EXPECT_TRUE(static_cast<bool>(b2));

    b->setMode(fmuMode::initializationMode);
    b2->setMode(fmuMode::initializationMode);

    EXPECT_EQ(b->getCurrentMode(), fmuMode::initializationMode);
    ASSERT_EQ(b->inputSize(), 1);
    ASSERT_EQ(b->outputSize(), 1);

    double input = 0.6;
    b->setInputs(&input);
    auto inputName = b->getInputNames();
    ASSERT_EQ(inputName[0], "power");
    auto outputName = b->getOutputNames();
    ASSERT_EQ(outputName[0], "load");
    b->setMode(fmuMode::stepMode);
    b2->setMode(fmuMode::stepMode);
    EXPECT_EQ(b2->getCurrentMode(), fmuMode::stepMode);

    auto val = b->getOutput(0);
    auto val2 = b2->getOutput(0);

    EXPECT_NEAR(val, 0.6, 0.000001);
    EXPECT_NEAR(val2, 0.5, 0.0000001);
    input = 0.7;
    b->setInputs(&input);
    input = 0.3;
    b2->setInputs(&input);
    b->doStep(0, 1.0, false);
    b2->doStep(0, 1.0, false);
    val = b->getOutput(0);
    val2 = b2->getOutput(0);

    EXPECT_NEAR(val, 0.7, 0.000001);
    EXPECT_NEAR(val2, 0.3, 0.0000001);

    input = 0.3;
    b->setInputs(&input);
    input = 0.7;
    b2->setInputs(&input);
    b->doStep(1.0, 2.0, false);
    b2->doStep(1.0, 2.0, false);
    val = b->getOutput(0);
    val2 = b2->getOutput(0);

    EXPECT_NEAR(val, 0.3, 0.000001);
    EXPECT_NEAR(val2, 0.7, 0.0000001);

    b = nullptr;
    b2 = nullptr;
    gdFmu.close();

    EXPECT_FALSE(gdFmu.isSoLoaded());
    try {
        remove_all(gdDir);
    }
    catch (...) {
    }
    try {
        remove(fmu);
    }
    catch (...) {
    }
}

TEST_F(FmiExportTests, TestFmiRunner2)
{
    auto runner = std::make_unique<fmi::fmiRunner>("testsim",
                                                   fmi_test_directory + "/three_phase_fmu",
                                                   nullptr);
    runner->simInitialize();
    runner->UpdateOutputs();

    auto bus = static_cast<gridBus*>(runner->getSim()->getSubObject("bus", 11));

    auto ld = dynamic_cast<loads::ThreePhaseLoad*>(bus->getLoad(0));
    ASSERT_NE(ld, nullptr);

    auto ret = runner->Step(10.0);
    EXPECT_EQ(ret, coreTime(10.0));

    auto va_vr = runner->findVR("Bus11_VA");
    auto vb_vr = runner->findVR("Bus11_VB");
    auto vc_vr = runner->findVR("Bus11_VC");
    EXPECT_NE(va_vr, kNullLocation);
    EXPECT_NE(vb_vr, kNullLocation);
    EXPECT_NE(vc_vr, kNullLocation);
    auto val1 = runner->Get(va_vr);
    auto val2 = runner->Get(vb_vr);
    auto val3 = runner->Get(vc_vr);
    EXPECT_GT(val1, 3500.0);
    EXPECT_LT(val1, 5200.0);
    EXPECT_GT(val2, 3500.0);
    EXPECT_LT(val2, 5200.0);
    EXPECT_GT(val3, 3500.0);
    EXPECT_LT(val3, 5200.0);
    auto val4 = runner->Get(runner->findVR("Bus11_VAngleA"));
    auto val5 = runner->Get(runner->findVR("Bus11_VAngleB"));
    auto val6 = runner->Get(runner->findVR("Bus11_VAngleC"));
    EXPECT_GT(val4, -2000.0);
    EXPECT_GT(val5, -2000.0);
    EXPECT_GT(val6, -2000.0);
    auto time = 20.0;
    const std::set<std::string> currentInputs{"Bus11_IA", "Bus11_IB", "Bus11_IC"};
    for (auto& ifld : currentInputs) {
        auto ivr = runner->findVR(ifld);
        runner->Set(ivr, 100.0);
        ret = runner->Step(time);
        time += 10.0;

        auto val1b = runner->Get(va_vr);
        auto val2b = runner->Get(vb_vr);
        auto val3b = runner->Get(vc_vr);
        EXPECT_GT(std::abs(val1b - val1), 0.00001);
        EXPECT_GT(std::abs(val2b - val2), 0.00001);
        EXPECT_GT(std::abs(val3b - val3), 0.00001);
        val1 = val1b;
        val2 = val2b;
        val3 = val3b;
        auto gb = runner->Get(ivr);
        EXPECT_NEAR(gb, 100.0, 0.5);
    }

    const std::set<std::string> currentAngleInputs{"Bus11_IAngleA",
                                                   "Bus11_IAngleB",
                                                   "Bus11_IAngleC"};
    double phase = 0.0;
    for (auto& ifld : currentAngleInputs) {
        auto ivr = runner->findVR(ifld);
        runner->Set(ivr, 0.0 + phase * 120.0);

        ret = runner->Step(time);
        time += 10.0;

        auto val1b = runner->Get(va_vr);
        auto val2b = runner->Get(vb_vr);
        auto val3b = runner->Get(vc_vr);
        EXPECT_GT(std::abs(val1b - val1), 0.00001);
        EXPECT_GT(std::abs(val2b - val2), 0.00001);
        EXPECT_GT(std::abs(val3b - val3), 0.00001);
        val1 = val1b;
        val2 = val2b;
        val3 = val3b;
        auto gb = runner->Get(ivr);
        double diff = std::abs(gb - (0.0 + phase * 120.0));
        if (diff > 359.5) {
            diff -= 360.0;
        }
        phase += 1.0;
        EXPECT_NEAR(diff, 0.0, 0.1);
    }
}
