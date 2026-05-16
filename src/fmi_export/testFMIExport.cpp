/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
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
#include <string_view>

namespace {

constexpr std::string_view fmiTestDirectory{GRIDDYN_TEST_DIRECTORY "/fmi_export_tests/"};

// NOLINTNEXTLINE(misc-multiple-inheritance)
class FmiExportTests: public gridDynSimulationTestFixture, public ::testing::Test {};

void generateFMU(const std::string& target, const std::string& inputFile)
{
    auto builder = std::make_unique<griddyn::fmi::FmuBuilder>();

    builder->InitializeFromString("--buildfmu=\"" + target + "\" \"" + inputFile + "\"");

    builder->MakeFmu();

    EXPECT_TRUE(exists(target));
}

}  // namespace

using griddyn::coreTime;
using griddyn::gridBus;
using griddyn::gridDynSimulation;
using griddyn::loadFile;
using griddyn::make_owningPtr;
using griddyn::readerInfo;
using griddyn::loads::ThreePhaseLoad;
using std::filesystem::exists;
using std::filesystem::path;
using std::filesystem::remove;
using std::filesystem::remove_all;

TEST_F(FmiExportTests, TestFmiEvents)
{
    auto fmiCon = make_owningPtr<griddyn::fmi::FmiCoordinator>();

    gds = std::make_unique<gridDynSimulation>();
    gds->add(fmiCon.get());

    auto* coordinatorObject = gds->find("fmiCoordinator");
    ASSERT_NE(coordinatorObject, nullptr);
    EXPECT_EQ(coordinatorObject->getID(), fmiCon->getID());

    readerInfo readerInformation;
    loadFmiExportReaderInfoDefinitions(readerInformation);
    const std::string inputFile = std::string{fmiTestDirectory} + "fmi_export_fmiinput.xml";
    loadFile(gds.get(), inputFile, &readerInformation, "xml");

    const auto& inputEvents = fmiCon->getInputs();
    ASSERT_EQ(inputEvents.size(), 1U);

    EXPECT_EQ(inputEvents[0].second.name, "power");
}

TEST_F(FmiExportTests, TestFmiOutput)
{
    auto fmiCon = make_owningPtr<griddyn::fmi::FmiCoordinator>();

    gds = std::make_unique<gridDynSimulation>();
    gds->add(fmiCon.get());

    auto* coordinatorObject = gds->find("fmiCoordinator");
    ASSERT_NE(coordinatorObject, nullptr);
    EXPECT_EQ(coordinatorObject->getID(), fmiCon->getID());

    readerInfo readerInformation;
    loadFmiExportReaderInfoDefinitions(readerInformation);
    const std::string inputFile = std::string{fmiTestDirectory} + "fmi_export_fmioutput.xml";
    loadFile(gds.get(), inputFile, &readerInformation, "xml");

    const auto& outputEvents = fmiCon->getOutputs();
    ASSERT_EQ(outputEvents.size(), 1U);

    EXPECT_EQ(outputEvents[0].second.name, "load");
}

TEST_F(FmiExportTests, TestFmiSimulation)
{
    auto fmiCon = make_owningPtr<griddyn::fmi::FmiCoordinator>();

    gds = std::make_unique<gridDynSimulation>();
    gds->add(fmiCon.get());

    auto* coordinatorObject = gds->find("fmiCoordinator");
    ASSERT_NE(coordinatorObject, nullptr);
    EXPECT_EQ(coordinatorObject->getID(), fmiCon->getID());

    readerInfo readerInformation;
    loadFmiExportReaderInfoDefinitions(readerInformation);
    const std::string inputFile = std::string{fmiTestDirectory} + "simulation.xml";
    loadFile(gds.get(), inputFile, &readerInformation, "xml");

    const auto& inputEvents = fmiCon->getInputs();
    ASSERT_EQ(inputEvents.size(), 1U);

    EXPECT_EQ(inputEvents[0].second.name, "power");

    const auto& outputEvents = fmiCon->getOutputs();
    ASSERT_EQ(outputEvents.size(), 1U);

    EXPECT_EQ(outputEvents[0].second.name, "load");

    gds->run(30);
    checkState(gridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
}

TEST_F(FmiExportTests, TestFmiRunner)
{
    auto runner = std::make_unique<griddyn::fmi::FmiRunner>("testsim",
                                                            std::string{fmiTestDirectory},
                                                            nullptr);

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

TEST_F(FmiExportTests, BuildGriddynFmu)
{
    generateFMU(std::string{fmiTestDirectory} + "griddyn.fmu",
                std::string{fmiTestDirectory} + "simulation.xml");
}

TEST_F(FmiExportTests, LoadGriddynFmu)
{
    const std::string fmuPath = std::string{fmiTestDirectory} + "griddyn.fmu";

    if (!exists(fmuPath)) {
        generateFMU(fmuPath, std::string{fmiTestDirectory} + "simulation.xml");
    }
    ASSERT_TRUE(exists(fmuPath)) << "unable to generate FMU";
    path gdDir(std::string{fmiTestDirectory} + "griddyn");
    ::fmiLibrary gdFmu(fmuPath);
    gdFmu.loadSharedLibrary();
    ASSERT_TRUE(gdFmu.isSoLoaded());

    const auto types = gdFmu.getTypes();
    EXPECT_EQ(types, "default");
    const auto version = gdFmu.getVersion();
    EXPECT_EQ(version, "2.0");

    auto coSimObject1 = gdFmu.createCoSimulationObject("gd1");

    auto coSimObject2 = gdFmu.createCoSimulationObject("gd2");

    EXPECT_TRUE(static_cast<bool>(coSimObject1));
    EXPECT_TRUE(static_cast<bool>(coSimObject2));

    coSimObject1->setMode(::fmuMode::initializationMode);
    coSimObject2->setMode(::fmuMode::initializationMode);

    EXPECT_EQ(coSimObject1->getCurrentMode(), ::fmuMode::initializationMode);
    ASSERT_EQ(coSimObject1->inputSize(), 1);
    ASSERT_EQ(coSimObject1->outputSize(), 1);

    double input = 0.6;
    coSimObject1->setInputs(&input);
    auto inputName = coSimObject1->getInputNames();
    ASSERT_EQ(inputName[0], "power");
    auto outputName = coSimObject1->getOutputNames();
    ASSERT_EQ(outputName[0], "load");
    coSimObject1->setMode(::fmuMode::stepMode);
    coSimObject2->setMode(::fmuMode::stepMode);
    EXPECT_EQ(coSimObject2->getCurrentMode(), ::fmuMode::stepMode);

    auto val = coSimObject1->getOutput(0);
    auto val2 = coSimObject2->getOutput(0);

    EXPECT_NEAR(val, 0.6, 0.000001);
    EXPECT_NEAR(val2, 0.5, 0.0000001);
    input = 0.7;
    coSimObject1->setInputs(&input);
    input = 0.3;
    coSimObject2->setInputs(&input);
    coSimObject1->doStep(0, 1.0, fmi2False);
    coSimObject2->doStep(0, 1.0, fmi2False);
    val = coSimObject1->getOutput(0);
    val2 = coSimObject2->getOutput(0);

    EXPECT_NEAR(val, 0.7, 0.000001);
    EXPECT_NEAR(val2, 0.3, 0.0000001);

    input = 0.3;
    coSimObject1->setInputs(&input);
    input = 0.7;
    coSimObject2->setInputs(&input);
    coSimObject1->doStep(1.0, 2.0, fmi2False);
    coSimObject2->doStep(1.0, 2.0, fmi2False);
    val = coSimObject1->getOutput(0);
    val2 = coSimObject2->getOutput(0);

    EXPECT_NEAR(val, 0.3, 0.000001);
    EXPECT_NEAR(val2, 0.7, 0.0000001);

    coSimObject1 = nullptr;
    coSimObject2 = nullptr;
    gdFmu.close();

    EXPECT_FALSE(gdFmu.isSoLoaded());
    std::error_code errorCode;
    remove_all(gdDir, errorCode);
    errorCode.clear();
    remove(fmuPath, errorCode);
}

TEST_F(FmiExportTests, TestFmiRunner2)
{
    auto runner = std::make_unique<griddyn::fmi::FmiRunner>("testsim",
                                                            std::string{fmiTestDirectory} +
                                                                "/three_phase_fmu",
                                                            nullptr);
    runner->simInitialize();
    runner->UpdateOutputs();

    auto* bus = static_cast<gridBus*>(runner->getSim()->getSubObject("bus", 11));

    auto* loadObject = dynamic_cast<griddyn::loads::ThreePhaseLoad*>(bus->getLoad(0));
    ASSERT_NE(loadObject, nullptr);

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
    for (const auto& inputField : currentInputs) {
        auto ivr = runner->findVR(inputField);
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
        auto actualValue = runner->Get(ivr);
        EXPECT_NEAR(actualValue, 100.0, 0.5);
    }

    const std::set<std::string> currentAngleInputs{"Bus11_IAngleA",
                                                   "Bus11_IAngleB",
                                                   "Bus11_IAngleC"};
    double phase = 0.0;
    for (const auto& inputField : currentAngleInputs) {
        auto ivr = runner->findVR(inputField);
        runner->Set(ivr, 0.0 + (phase * 120.0));

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
        auto actualValue = runner->Get(ivr);
        double diff = std::abs(actualValue - (0.0 + (phase * 120.0)));
        if (diff > 359.5) {
            diff -= 360.0;
        }
        phase += 1.0;
        EXPECT_NEAR(diff, 0.0, 0.1);
    }
}
