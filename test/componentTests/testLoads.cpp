/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "fileInput/loadModelReaderHelper.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/blocks/LeadLagBlock.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/loads/ApproximatingLoad.h"
#include "griddyn/loads/CIMLoad.h"
#include "griddyn/loads/CompositeLoad.h"
#include "griddyn/loads/ElectronicLoad.h"
#include "griddyn/loads/FDepLoad.h"
#include "griddyn/loads/FileLoad.h"
#include "griddyn/loads/GridLabDLoad.h"
#include "griddyn/loads/IEELLoad.h"
#include "griddyn/loads/LoadTemplateAdapters.h"
#include "griddyn/loads/MotorDLoad.h"
#include "griddyn/loads/MotorLoad5.h"
#include "griddyn/loads/MotorProtectionGroups.h"
#include "griddyn/loads/SourceLoad.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/loads/ThreePhaseLoad.h"
#include "griddyn/loads/WECCMotor3.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/simulation/Diagnostics.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace griddyn;
using namespace griddyn::loads;

static constexpr std::string_view loadTestDirectory{GRIDDYN_TEST_DIRECTORY "/load_tests/"};
static constexpr std::string_view gridlabdTestDirectory{GRIDDYN_TEST_DIRECTORY "/gridlabD_tests/"};

static std::string makeLoadTestPath(std::string_view fileName)
{
    return std::string{loadTestDirectory} + std::string{fileName};
}

static std::string makeGridlabdTestPath(std::string_view fileName)
{
    return std::string{gridlabdTestDirectory} + std::string{fileName};
}

TEST(IEELLoadTests, SelectsTheSimplestEquivalentLoadAndEvaluatesItsEquation)
{
    const auto checkConversion = [](const IEELParameters& parameters,
                                    IEELRepresentation expectedRepresentation,
                                    const std::function<double(double, double)>& expectedP,
                                    const std::function<double(double, double)>& expectedQ) {
        auto simulation = std::make_unique<GridDynSimulation>();
        auto* bus = new AcBus("bus");
        bus->add(new ZipLoad(0.6, 0.25, "raw_load"));
        simulation->add(bus);

        LoadTemplateManager templates;
        templates.setTemplate(LoadTemplateScope::System, 0, makeIEELALLoadTemplate(parameters));
        applyLoadTemplatesFromReader(*simulation, templates);
        auto* load = bus->getLoad(0);
        ASSERT_NE(load, nullptr);
        EXPECT_EQ(load->getName(), "raw_load");
        if (expectedRepresentation == IEELRepresentation::ZIP) {
            EXPECT_NE(dynamic_cast<ZipLoad*>(load), nullptr);
        } else if (expectedRepresentation == IEELRepresentation::FDEP) {
            EXPECT_NE(dynamic_cast<FDepLoad*>(load), nullptr);
        } else {
            EXPECT_NE(dynamic_cast<IEELLoad*>(load), nullptr);
        }

        const IOdata inputs{1.1, 0.0, 1.04};
        EXPECT_NEAR(load->getRealPower(inputs, emptyStateData, cLocalSolverMode),
                    expectedP(1.1, 1.04),
                    1e-12);
        EXPECT_NEAR(load->getReactivePower(inputs, emptyStateData, cLocalSolverMode),
                    expectedQ(1.1, 1.04),
                    1e-12);
    };

    IEELParameters zipParameters;
    zipParameters.coefficients = {0.2, 0.3, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0};
    zipParameters.exponents = {0.0, 1.0, 2.0, 0.0, 0.0, 2.0};
    EXPECT_EQ(classifyIEEL(zipParameters), IEELRepresentation::ZIP);
    checkConversion(
        zipParameters,
        IEELRepresentation::ZIP,
        [](double voltage, double) {
            return 0.6 * (0.2 + (0.3 * voltage) + (0.5 * voltage * voltage));
        },
        [](double voltage, double) { return 0.25 * voltage * voltage; });

    IEELParameters fdepParameters;
    fdepParameters.coefficients = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0};
    fdepParameters.exponents = {1.5, 0.0, 0.0, 2.5, 0.0, 0.0};
    EXPECT_EQ(classifyIEEL(fdepParameters), IEELRepresentation::FDEP);
    checkConversion(
        fdepParameters,
        IEELRepresentation::FDEP,
        [](double voltage, double frequency) { return 0.6 * std::pow(voltage, 1.5) * frequency; },
        [](double voltage, double) { return 0.25 * std::pow(voltage, 2.5); });

    IEELParameters generalParameters;
    generalParameters.coefficients = {0.4, 0.6, 0.0, 0.3, 0.7, 0.0, 0.25, 0.4};
    generalParameters.exponents = {0.5, 1.5, 0.0, 0.2, 2.5, 0.0};
    EXPECT_EQ(classifyIEEL(generalParameters), IEELRepresentation::IEEL);
    checkConversion(
        generalParameters,
        IEELRepresentation::IEEL,
        [](double voltage, double frequency) {
            return 0.6 * ((0.4 * std::pow(voltage, 0.5)) + (0.6 * std::pow(voltage, 1.5))) *
                (1.0 + (0.25 * (frequency - 1.0)));
        },
        [](double voltage, double frequency) {
            return 0.25 * ((0.3 * std::pow(voltage, 0.2)) + (0.7 * std::pow(voltage, 2.5))) *
                (1.0 + (0.4 * (frequency - 1.0)));
        });
}

TEST(ElectronicLoadTests, IndependentCurvesFrequencyAndVoltageRecovery)
{
    auto simulation = std::make_unique<GridDynSimulation>();
    auto* source = new AcBus("source");
    source->set("type", "swing");
    source->set("voltage", 1.0);
    source->add(new DynamicGenerator("slack_generator"));

    auto* loadBus = new AcBus("electronic_load_bus");
    loadBus->set("type", "pq");
    auto* electronic = new ElectronicLoad("electronic_load");
    electronic->setLoad(0.4, 0.1);
    electronic->set("pfel", 0.0);  // Use the assigned Q base.
    electronic->set("vd1", 0.7);
    electronic->set("vd2", 0.5);
    electronic->set("frcel", 0.8);
    electronic->set("a1", 0.25);
    electronic->set("a3", 0.75);
    electronic->set("n1", 2.0);
    electronic->set("a4", 0.6);
    electronic->set("a6", 0.4);
    electronic->set("n4", 1.0);
    electronic->set("pfrq", 0.2);
    electronic->set("qfrq", -0.3);
    loadBus->add(electronic);
    simulation->add(source);
    simulation->add(loadBus);

    auto* line = new AcLine(0.0, 0.015, "source_to_electronic_load");
    line->updateBus(source, 1);
    line->updateBus(loadBus, 2);
    simulation->add(line);

    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_TRUE(electronic->checkFlag(USES_BUS_FREQUENCY));
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);

    const double testFrequency = 1.02;
    const double voltage = 1.04;
    const double pCurve = (0.25 * voltage * voltage) + 0.75;
    const double qCurve = (0.6 * voltage) + 0.4;
    EXPECT_NEAR(electronic->getRealPower({voltage, 0.0, testFrequency},
                                         emptyStateData,
                                         cDaeSolverMode),
                0.4 * pCurve * (1.0 + (0.2 * (testFrequency - 1.0))),
                1e-12);
    EXPECT_NEAR(electronic->getReactivePower({voltage, 0.0, testFrequency},
                                             emptyStateData,
                                             cDaeSolverMode),
                0.1 * qCurve * (1.0 - (0.3 * (testFrequency - 1.0))),
                1e-12);

    // Track a low-voltage minimum, then verify partial reconnection follows Frcel.
    electronic->timestep(0.1, {0.55}, cLocalSolverMode);
    const double pAtLowVoltage = 0.4 * ((0.25 * 0.55 * 0.55) + 0.75) * 0.25;
    EXPECT_NEAR(electronic->getRealPower(0.55), pAtLowVoltage, 1e-12);

    electronic->timestep(0.2, {0.65}, cLocalSolverMode);
    const double pDuringRecovery = 0.4 * ((0.25 * 0.65 * 0.65) + 0.75) * 0.65;
    EXPECT_NEAR(electronic->getRealPower(0.65), pDuringRecovery, 1e-12);

    electronic->timestep(0.3, {0.8}, cLocalSolverMode);
    const double pAfterRecovery = 0.4 * ((0.25 * 0.8 * 0.8) + 0.75) * 0.85;
    EXPECT_NEAR(electronic->getRealPower(0.8), pAfterRecovery, 1e-12);
}

TEST(LoadTemplateTests, KeepsFixedAndControlledShunts)
{
    auto simulation = std::make_unique<GridDynSimulation>();
    auto* bus = new AcBus("bus");
    auto* ordinaryLoad = new ZipLoad(0.6, 0.25, "ordinary");
    auto* fixedShunt = new ZipLoad(0.0, 0.15, "fixed_shunt");
    fixedShunt->setFixedShunt();
    auto* switchedShunt = new Svd("switched_shunt");
    bus->add(ordinaryLoad);
    bus->add(fixedShunt);
    bus->add(switchedShunt);
    simulation->add(bus);

    IEELParameters parameters;
    parameters.coefficients = {0.5, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0};
    parameters.exponents = {0.5, 1.5, 0.0, 1.0, 0.0, 0.0};
    LoadTemplateManager templates;
    templates.setTemplate(LoadTemplateScope::System, 0, makeIEELALLoadTemplate(parameters));

    ASSERT_NO_THROW(applyLoadTemplatesFromReader(*simulation, templates));
    EXPECT_NE(dynamic_cast<IEELLoad*>(bus->getLoad(0)), nullptr);
    EXPECT_EQ(bus->getLoad(1), fixedShunt);
    EXPECT_EQ(bus->getLoad(2), switchedShunt);
}

TEST(LoadTemplateTests, PreservesDemandOfDisconnectedLoads)
{
    const auto checkConversion = [](LoadTemplateFactory factory) {
        auto simulation = std::make_unique<GridDynSimulation>();
        auto* bus = new AcBus("bus");
        auto* original = new ZipLoad(0.6, 0.25, "offline_load");
        original->disable();
        original->disconnect();
        bus->add(original);
        simulation->add(bus);

        LoadTemplateManager templates;
        templates.setTemplate(LoadTemplateScope::System, 0, std::move(factory));
        applyLoadTemplatesFromReader(*simulation, templates);

        auto* replacement = bus->getLoad(0);
        ASSERT_NE(replacement, nullptr);
        EXPECT_FALSE(replacement->isEnabled());
        EXPECT_FALSE(replacement->isConnected());
        replacement->enable();
        replacement->reconnect();
        EXPECT_NEAR(replacement->getRealPower(1.0), 0.6, 1e-12);
        EXPECT_NEAR(replacement->getReactivePower(1.0), 0.25, 1e-12);
    };

    IEELParameters ieel;
    ieel.coefficients = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0};
    checkConversion(makeIEELALLoadTemplate(ieel));

    WSCCParameters wscc;
    wscc.p3 = 1.0;
    wscc.q3 = 1.0;
    checkConversion(makeWSCCLoadTemplate(wscc));
}

class LoadTests: public GridLoadTestFixture, public ::testing::Test {};

class CIM5SaturationAccess final: public CIM5 {
  public:
    using CIM5::CIM5;
    using CIM5::saturationFactor;
    using CIM5::saturationFactorDerivatives;
};

class CIM6TorqueAccess final: public CIM6 {
  public:
    using CIM6::CIM6;
    using CIM6::dmechds;
    using CIM6::mechPower;
};

TEST(CIMLoadTests, SaturationParametersProduceOpenIpslScaledQuadraticFactor)
{
    CIM5SaturationAccess motor;
    EXPECT_DOUBLE_EQ(motor.get("e1"), 1.0);
    EXPECT_DOUBLE_EQ(motor.get("se2"), 0.6);
    const double factor = motor.saturationFactor(1.2, 0.0);
    EXPECT_NEAR(factor, 0.5, 1e-12);
    double derivativeErpp;
    double derivativeEmpp;
    motor.saturationFactorDerivatives(1.2, 0.0, derivativeErpp, derivativeEmpp);
    EXPECT_TRUE(std::isfinite(derivativeErpp));
    EXPECT_NEAR(derivativeEmpp, 0.0, 1e-12);
    EXPECT_THROW(motor.set("mtype", 3.0), InvalidParameterValue);
    EXPECT_THROW(motor.set("mtype", 1.5), InvalidParameterValue);
}

TEST(CIMLoadTests, Cim6MechanicalTorqueAndDerivativeFollowConfiguredCurve)
{
    CIM6TorqueAccess motor;
    motor.set("tnom", 2.0);
    motor.set("a", 0.1);
    motor.set("b", 0.2);
    motor.set("c0", 0.3);
    motor.set("d", 0.4);
    motor.set("e", 3.0);

    constexpr double slip = 0.2;
    constexpr double omega = 1.0 - slip;
    EXPECT_NEAR(motor.mechPower(slip),
                2.0 * ((0.1 * omega * omega) + (0.2 * omega) + 0.3 + (0.4 * omega * omega * omega)),
                1e-12);
    EXPECT_NEAR(motor.dmechds(slip), -(2.0 * ((0.2 * omega) + 0.2 + (1.2 * omega * omega))), 1e-12);
}

TEST_F(LoadTests, BasicLoadTest)
{
    ld1 = new ZipLoad(1.1, -0.3);
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
    ld1 = new ZipLoad(1.0, 0.0);
    std::vector<double> voltageSamples;
    std::vector<double> powerSamples;
    ld1->set("vpqmin", 0.75);
    ld1->set("vpqmax", 1.25);

    for (int voltageIndex = 0; voltageIndex <= 1500; ++voltageIndex) {
        const double voltageTest = static_cast<double>(voltageIndex) * 0.001;
        voltageSamples.push_back(voltageTest);
        powerSamples.push_back(ld1->getRealPower(voltageTest));
    }
    voltageSamples.push_back(1.5);
    EXPECT_NEAR(std::abs(powerSamples[400] -
                         (voltageSamples[400] * voltageSamples[400] / (0.75 * 0.75))),
                0.0,
                0.001);
    EXPECT_NEAR(std::abs(powerSamples[1350] -
                         (voltageSamples[1350] * voltageSamples[1350] / (1.25 * 1.25))),
                0.0,
                0.001);
    EXPECT_NEAR(std::abs(powerSamples[800] - 1.0), 0.0, 0.001);
    EXPECT_NEAR(std::abs(powerSamples[1249] - 1.0), 0.0, 0.001);
}

TEST_F(LoadTests, RampLoadTest)
{
    ld1 = new RampLoad();
    auto ldT = dynamic_cast<RampLoad*>(ld1);
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
    ld1 = new SourceLoad(SourceLoad::SourceType::RANDOM);
    auto ldT = static_cast<SourceLoad*>(ld1);
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
    EXPECT_GE(otime, CoreTime(8.0));
    EXPECT_LE(otime, CoreTime(11.0));
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
    ld1 = new SourceLoad(SourceLoad::SourceType::RANDOM);
    auto ldT = static_cast<SourceLoad*>(ld1);
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
    ld1 = new SourceLoad(SourceLoad::SourceType::PULSE);
    auto ldT = static_cast<SourceLoad*>(ld1);
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
    ld1 = new FileLoad();
    auto ldT = static_cast<FileLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    std::string fileName = makeLoadTestPath("FileLoadInfo.bin");
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

    ld1 = new FileLoad();
    ldT = static_cast<FileLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);

    ld1->set("file", fileName);
    ldT->set("mode", "interpolate");
    ld1->pFlowInitializeA(timeZero, 0U);

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
    std::string fileName = makeLoadTestPath("testLoad.bin");
    ld1 = new FileLoad("fload", fileName);
    auto ldT = static_cast<FileLoad*>(ld1);
    ASSERT_NE(ldT, nullptr);
    ldT->set("column", "yp");
    ldT->set("scaling", 1.0);
    ldT->set("qratio", 0.3);
    gmlc::utilities::TimeSeries<> Tdata(fileName);
    ldT->pFlowInitializeA(timeZero, 0U);

    double val = ldT->getRealPower();
    auto tod = Tdata.data()[0];
    EXPECT_NEAR(val, tod, (std::abs(tod) * 1e-6) + 1e-12);
}

#ifndef GRIDDYN_ENABLE_MPI
TEST_F(LoadTests, GridDynLoadTest1)
{
    std::string fileName = makeGridlabdTestPath("IEEE_13_mod.xml");

    auto gds = readSimXMLFile(fileName);

    auto bus = gds->getBus(1);
    auto gld = dynamic_cast<GridLabDLoad*>(bus->getLoad());

    ASSERT_NE(gld, nullptr);

    gds->run();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}
#endif

TEST_F(LoadTests, MotorTest1)
{
    std::string fileName = makeLoadTestPath("motorload_test1.xml");

    auto gds = readSimXMLFile(fileName);

    GridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<MotorLoad*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);

    gds->dynInitialize();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode);
    gds->run();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, MotorTest3)
{
    std::string fileName = makeLoadTestPath("motorload_test3.xml");

    auto gds = readSimXMLFile(fileName);

    GridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<MotorLoad3*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode, 1e-8);
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    gds->run();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, MotorTest3Stall)
{
    std::string fileName = makeLoadTestPath("motorload_test3_stall.xml");

    auto gds = readSimXMLFile(fileName);

    GridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<MotorLoad3*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode, 1e-8);
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
    gds->run(2.5);
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    EXPECT_TRUE(mtld->checkFlag(MotorLoad::STALLED));
    gds->run();
    EXPECT_FALSE(mtld->checkFlag(MotorLoad::STALLED));
}

TEST_F(LoadTests, Cim5SaturationInitializesAndIntegrates)
{
    auto simulation = readSimXMLFile(makeLoadTestPath("cim5_saturation.xml"));
    auto* bus = simulation->getBus(1);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(dynamic_cast<CIM5*>(bus->getLoad()), nullptr);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    simulation->run();
    requireStates(simulation->currentProcessState(),
                  GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, Cim6SaturationInitializesAndIntegrates)
{
    auto simulation = readSimXMLFile(makeLoadTestPath("cim6_saturation.xml"));
    auto* bus = simulation->getBus(1);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(dynamic_cast<CIM6*>(bus->getLoad()), nullptr);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    simulation->run();
    requireStates(simulation->currentProcessState(),
                  GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, WeccMotor3UsesSpeedExponentTorqueAndDirectParameters)
{
    WECCMotor3 motor;
    motor.set("Tmo", 1.2);
    motor.set("Etrq", 2.0);
    motor.set("Ls", 2.5);
    motor.set("Lp", 0.2);
    motor.set("Lpp", 0.15);
    motor.set("Tpo", 0.44);
    motor.set("Tppo", 0.0026);

    EXPECT_NEAR(motor.mechanicalTorqueAtSpeed(0.8), 1.2 * 0.8 * 0.8, 1e-12);
    EXPECT_NEAR(motor.get("ls"), 2.5, 1e-12);
    EXPECT_NEAR(motor.get("lp"), 0.2, 1e-12);
    EXPECT_NEAR(motor.get("lpp"), 0.15, 1e-12);
    EXPECT_NEAR(motor.get("tpo"), 0.44, 1e-12);
    EXPECT_NEAR(motor.get("tppo"), 0.0026, 1e-12);
}

TEST_F(LoadTests, WeccMotor3InitializesTorqueAndHasConsistentJacobian)
{
    auto simulation = std::make_unique<GridDynSimulation>();
    auto* source = new AcBus("source");
    source->set("type", "swing");
    source->set("voltage", 1.0);
    source->add(new DynamicGenerator("slack_generator"));
    auto* motorBus = new AcBus("motor_bus");
    motorBus->set("type", "pq");
    auto* motor = new WECCMotor3("motor");
    motor->set("p", 0.2);
    motor->set("lfm", 0.85);
    motor->set("h", 0.5);
    motor->set("rs", 0.03);
    motor->set("ls", 1.8);
    motor->set("lp", 0.19);
    motor->set("lpp", 0.14);
    motor->set("tpo", 0.2);
    motor->set("tppo", 0.0026);
    motor->set("etrq", 2.0);
    motor->set("vtr1", 0.8);
    motor->set("ttr1", 0.02);
    motor->set("ftr1", 0.25);
    motorBus->add(motor);
    simulation->add(source);
    simulation->add(motorBus);
    auto* line = new AcLine(0.0, 0.015, "source_to_motor");
    line->updateBus(source, 1);
    line->updateBus(motorBus, 2);
    simulation->add(line);

    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_TRUE(motorBus->isConnected());
    EXPECT_NEAR(motor->get("p"), 0.2, 1e-12);
    EXPECT_NEAR(motor->get("pmot"), motor->get("p"), 1e-12);
    EXPECT_NEAR(motor->get("scale"), motor->get("p") / motor->get("lfm"), 1e-12);
    EXPECT_GT(motor->getRealPower(), 0.0);
    EXPECT_GT(motor->getReactivePower(), 0.0);
    EXPECT_GT(motor->rotorSpeed(), 0.0);
    EXPECT_LT(motor->rotorSpeed(), 1.0);
    const double motorBasePower = motor->getRealPower() / motor->get("scale");
    const double reactivePower = motor->getReactivePower() / motor->get("scale");
    const double expectedElectricalTorque = motorBasePower -
        (motor->get("r") * ((motorBasePower * motorBasePower) + (reactivePower * reactivePower)) /
         (motorBus->getVoltage(emptyStateData, cLocalSolverMode) *
          motorBus->getVoltage(emptyStateData, cLocalSolverMode)));
    EXPECT_NEAR(motor->get("tmo") * std::pow(motor->rotorSpeed(), motor->get("etrq")),
                expectedElectricalTorque,
                1e-6);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
}

TEST_F(LoadTests, WeccMotor3UsesAllocatedPowerForItsBaseAndHonorsExplicitRating)
{
    for (const bool explicitRating : {false, true}) {
        SCOPED_TRACE(explicitRating ? "explicit motor rating" : "LFm derived rating");
        auto simulation = std::make_unique<GridDynSimulation>();
        auto* source = new AcBus("source");
        source->set("type", "swing");
        source->set("voltage", 1.0);
        source->add(new DynamicGenerator("slack_generator"));
        auto* motorBus = new AcBus("motor_bus");
        motorBus->set("type", "pq");
        auto* motor = new WECCMotor3("motor");
        // CompositeLoad uses setLoad(), which bypasses MotorLoad's p setter.
        motor->setLoad(0.2, 0.04);
        motor->set("lfm", 0.9);
        motor->set("rs", 0.03);
        motor->set("ls", 2.5);
        motor->set("lp", 0.2);
        motor->set("lpp", 0.15);
        motor->set("tpo", 0.44);
        motor->set("tppo", 0.0026);
        motor->set("h", 0.5);
        if (explicitRating) {
            motor->set("mbase", 40.0);
        }
        motorBus->add(motor);
        simulation->add(source);
        simulation->add(motorBus);
        auto* line = new AcLine(0.0, 0.015, "source_to_motor");
        line->updateBus(source, 1);
        line->updateBus(motorBus, 2);
        simulation->add(line);

        ASSERT_EQ(simulation->powerflow(), 0);
        ASSERT_TRUE(motorBus->isConnected());
        EXPECT_NEAR(motor->get("pmot"), 0.2, 1e-12);
        EXPECT_NEAR(motor->get("base"), explicitRating ? 40.0 : 100.0 * 0.2 / 0.9, 1e-10);
        EXPECT_NEAR(motor->get("scale"), explicitRating ? 0.4 : 0.2 / 0.9, 1e-12);
        EXPECT_GT(motor->getRealPower(), 0.0);
        EXPECT_NEAR(motor->getRealPower(), 0.2, 0.01);
        EXPECT_GT(motor->getReactivePower(), 0.0);
        EXPECT_GT(motor->rotorSpeed(), 0.0);
        EXPECT_LT(motor->rotorSpeed(), 1.0);
        ASSERT_EQ(simulation->dynInitialize(), 0);
        EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
        EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    }
}

TEST_F(LoadTests, MotorProtectionGroupsTripCumulativeFractionsAndRecloseAfterDelay)
{
    MotorProtectionGroups protections;
    protections.setStage(0, 0.80, 0.10, 0.20, 0.90, 0.20);
    protections.setStage(1, 0.60, 0.05, 0.70, 0.80, 0.05);
    protections.initialize(0.0, 1.0);
    EXPECT_DOUBLE_EQ(protections.onlineFraction(), 1.0);

    EXPECT_FALSE(protections.rootTrigger(0, 0.0, 0.70));
    EXPECT_FALSE(protections.rootTrigger(1, 0.09, 0.70));
    EXPECT_TRUE(protections.rootTrigger(1, 0.10, 0.70));
    EXPECT_DOUBLE_EQ(protections.onlineFraction(), 0.80);

    EXPECT_FALSE(protections.rootTrigger(4, 0.20, 0.50));
    EXPECT_TRUE(protections.rootTrigger(5, 0.25, 0.50));
    EXPECT_DOUBLE_EQ(protections.onlineFraction(), 0.30);

    EXPECT_FALSE(protections.rootTrigger(6, 0.30, 0.85));
    EXPECT_TRUE(protections.rootTrigger(7, 0.35, 0.85));
    EXPECT_DOUBLE_EQ(protections.onlineFraction(), 0.80);

    EXPECT_FALSE(protections.rootTrigger(2, 0.40, 0.95));
    EXPECT_TRUE(protections.rootTrigger(3, 0.60, 0.95));
    EXPECT_DOUBLE_EQ(protections.onlineFraction(), 1.0);
}

TEST_F(LoadTests, MotorDCharacteristicMatchesRunStallAndFrequencyEquations)
{
    constexpr double activePowerBase = 0.85;
    constexpr double compPF = 0.95;
    constexpr double rStall = 0.05;
    constexpr double xStall = 0.20;
    constexpr double zSquared = (rStall * rStall) + (xStall * xStall);
    constexpr double gStall = rStall / zSquared;
    constexpr double bStall = -xStall / zSquared;
    const double reactivePowerBase = activePowerBase * std::tan(std::acos(compPF));
    const double reactivePowerAtRunVoltage = reactivePowerBase - (6.0 * std::pow(1.0 - 0.86, 2.0));

    const auto nominal =
        MotorDLoad::characteristicPower(activePowerBase, compPF, 1.0, 0.0, 0.45, gStall, bStall);
    EXPECT_NEAR(nominal.p, activePowerBase, 1e-12);
    EXPECT_NEAR(nominal.q, reactivePowerBase, 1e-12);

    const auto undervoltage =
        MotorDLoad::characteristicPower(activePowerBase, compPF, 0.80, -0.01, 0.45, gStall, bStall);
    const double pRun = activePowerBase + (12.0 * std::pow(0.86 - 0.80, 3.2));
    const double qRun = reactivePowerAtRunVoltage + (11.0 * std::pow(0.86 - 0.80, 2.5));
    EXPECT_NEAR(undervoltage.p, pRun * 0.99, 1e-12);
    EXPECT_NEAR(undervoltage.q, qRun * 1.033, 1e-12);

    const auto stalled =
        MotorDLoad::characteristicPower(activePowerBase, compPF, 0.30, -0.05, 0.45, gStall, bStall);
    EXPECT_NEAR(stalled.p, gStall * 0.30 * 0.30, 1e-12);
    EXPECT_NEAR(stalled.q, -bStall * 0.30 * 0.30, 1e-12);

    EXPECT_DOUBLE_EQ(MotorDLoad::inverseStallCycles(0.45), 2.0);
    EXPECT_DOUBLE_EQ(MotorDLoad::inverseStallCycles(0.49), 3.0);
    EXPECT_DOUBLE_EQ(MotorDLoad::inverseStallCycles(0.51), 6.0);
    EXPECT_DOUBLE_EQ(MotorDLoad::inverseStallCycles(0.53), 9.0);
    EXPECT_DOUBLE_EQ(MotorDLoad::inverseStallCycles(0.55), 12.0);
    EXPECT_DOUBLE_EQ(MotorDLoad::inverseStallCycles(0.565), 1.0e6);
    EXPECT_DOUBLE_EQ(MotorDLoad::thermalOnlineFraction(0.5, 1.0, 2.0), 1.0);
    EXPECT_DOUBLE_EQ(MotorDLoad::thermalOnlineFraction(1.5, 1.0, 2.0), 0.5);
    EXPECT_DOUBLE_EQ(MotorDLoad::thermalOnlineFraction(2.0, 1.0, 2.0), 0.0);
}

TEST_F(LoadTests, MotorDContactorAndInverseTimeProtectionOperate)
{
    {
        auto simulation = readSimXMLFile(makeLoadTestPath("motor_d_performance.xml"));
        auto* motorD = dynamic_cast<MotorDLoad*>(simulation->getBus(1)->getLoad());
        ASSERT_NE(motorD, nullptr);
        motorD->set("tv", 0.0);
        ASSERT_EQ(simulation->dynInitialize(), 0);

        const double fullLoadPower = motorD->getRealPower(1.0);
        motorD->timestep(0.01, {0.60}, cLocalSolverMode);
        EXPECT_NEAR(motorD->getRealPower(1.0), 0.50 * fullLoadPower, 1e-10);
        motorD->timestep(0.02, {0.80}, cLocalSolverMode);
        EXPECT_NEAR(motorD->getRealPower(1.0), 0.50 * fullLoadPower, 1e-10);
        motorD->timestep(0.03, {0.90}, cLocalSolverMode);
        EXPECT_NEAR(motorD->getRealPower(1.0), 0.75 * fullLoadPower, 1e-10);
        motorD->timestep(0.04, {1.00}, cLocalSolverMode);
        EXPECT_NEAR(motorD->getRealPower(1.0), fullLoadPower, 1e-10);
    }
    {
        auto simulation = readSimXMLFile(makeLoadTestPath("motor_d_performance.xml"));
        auto* motorD = dynamic_cast<MotorDLoad*>(simulation->getBus(1)->getLoad());
        ASSERT_NE(motorD, nullptr);
        motorD->set("tv", 0.0);
        motorD->set("tstall", -1.0);
        ASSERT_EQ(simulation->dynInitialize(), 0);

        motorD->timestep(0.01, {0.40}, cLocalSolverMode);
        motorD->timestep(0.03, {0.40}, cLocalSolverMode);
        EXPECT_FALSE(motorD->isStalled());
        motorD->timestep(0.05, {0.40}, cLocalSolverMode);
        EXPECT_TRUE(motorD->isStalled());
    }
}

TEST_F(LoadTests, MotorDLoadInitializesAndHasConsistentJacobian)
{
    auto simulation = readSimXMLFile(makeLoadTestPath("motor_d_performance.xml"));
    auto* bus = simulation->getBus(1);
    ASSERT_NE(bus, nullptr);
    auto* motorD = dynamic_cast<MotorDLoad*>(bus->getLoad());
    ASSERT_NE(motorD, nullptr);

    EXPECT_NEAR(motorD->getRealPower(1.0), motorD->get("p"), 1e-12);
    const double expectedQ = motorD->get("p") * std::tan(std::acos(motorD->get("comppf")));
    EXPECT_NEAR(motorD->getReactivePower(1.0), expectedQ, 1e-12);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    const IOdata motorInputs{1.0, 0.0, 1.0};
    double truncatedState = 0.0;
    StateData truncatedStateData(0.0, &truncatedState);
    truncatedStateData.stateSize = 1;
    EXPECT_NEAR(motorD->getRealPower(motorInputs, truncatedStateData, cDaeSolverMode),
                motorD->getRealPower(1.0),
                1e-12);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(), 0);
    EXPECT_FALSE(motorD->isStalled());
    EXPECT_FALSE(motorD->isUndervoltageTripped());
}

TEST_F(LoadTests, CompositeMotorComponentsHaveConsistentDaeJacobianAndRun)
{
    auto simulation = std::make_unique<GridDynSimulation>();
    auto* source = new AcBus("source");
    source->set("type", "swing");
    source->set("voltage", 1.0);
    source->add(new DynamicGenerator("slack_generator"));

    auto* loadBus = new AcBus("composite_load_bus");
    loadBus->set("type", "pq");
    auto* composite = new loads::CompositeLoad("composite_motor_load");
    composite->setLoad(0.4, 0.08);

    auto* motor3 = new WECCMotor3("three_phase_motor");
    motor3->set("lfm", 0.85);
    motor3->set("h", 0.5);
    motor3->set("rs", 0.03);
    motor3->set("ls", 2.5);
    motor3->set("lp", 0.2);
    motor3->set("lpp", 0.15);
    motor3->set("tpo", 0.44);
    motor3->set("tppo", 0.0026);
    motor3->set("etrq", 2.0);
    motor3->set("vtr1", 0.8);
    motor3->set("ttr1", 0.02);
    motor3->set("ftr1", 0.25);

    auto* motorD = new MotorDLoad("single_phase_motor");
    motorD->set("lfm", 0.85);
    motorD->set("comppf", 0.97);
    motorD->set("vstall", 0.65);
    motorD->set("tstall", 0.033);
    motorD->set("frst", 0.25);
    motorD->set("tv", 0.02);

    composite->add(motor3);
    composite->set("fraction1", 0.5);
    composite->add(motorD);
    composite->set("fraction2", 0.5);
    loadBus->add(composite);
    simulation->add(source);
    simulation->add(loadBus);

    auto* line = new AcLine(0.0, 0.015, "source_to_composite_load");
    line->updateBus(source, 1);
    line->updateBus(loadBus, 2);
    simulation->add(line);

    ASSERT_EQ(simulation->powerflow(), 0);
    ASSERT_TRUE(loadBus->isConnected());
    EXPECT_NEAR(motor3->get("pmot"), 0.2, 1e-12);
    EXPECT_NEAR(motor3->get("scale"), 0.2 / motor3->get("lfm"), 1e-12);
    EXPECT_GT(motor3->getRealPower(), 0.0);
    ASSERT_EQ(composite->componentCount(), 2U);
    EXPECT_EQ(composite->component(0), motor3);
    EXPECT_EQ(composite->component(1), motorD);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.05), 0);
    EXPECT_TRUE(std::isfinite(motorD->get("p")));
}

TEST_F(LoadTests, PartitionedDynamicMotorAndCimModelChecks)
{
    for (const auto fileName : {"motorload_test1.xml",
                                "motorload_test3.xml",
                                "cim5_saturation.xml",
                                "cim6_saturation.xml",
                                "fdepLoad.xml"}) {
        SCOPED_TRACE(fileName);
        auto simulation = readSimXMLFile(makeLoadTestPath(fileName));
        if (std::string_view(fileName) == "fdepLoad.xml") {
            auto* fdep = dynamic_cast<FDepLoad*>(simulation->getBus(2)->getLoad());
            ASSERT_NE(fdep, nullptr);
            fdep->set("betap", 1.0);
            fdep->set("betaq", 1.0);
            fdep->add(new griddyn::blocks::LeadLagBlock(2.0, 0.0, 1.0, "frequency_filter"));
        }
        simulation->set("dynamicsolvermethod", "partitioned");
        simulation->set("defdyndiff", "basicode");
        simulation->set("timestep", 0.005);

        ASSERT_EQ(simulation->dynInitialize(), 0);
        ASSERT_EQ(simulation->run(0.005), 0);
        const auto algMode = simulation->getSolverMode("dynalg");
        const auto diffMode = simulation->getSolverMode("dyndiff");
        {
            SCOPED_TRACE("algebraic partition residual");
            EXPECT_EQ(runResidualCheck(simulation, algMode, false), 0);
        }
        {
            SCOPED_TRACE("differential partition residual");
            EXPECT_EQ(runResidualCheck(simulation, diffMode, false), 0);
        }
        {
            SCOPED_TRACE("partitioned algebraic update");
            EXPECT_EQ(runAlgebraicCheck(simulation, algMode, false), 0);
        }
        {
            SCOPED_TRACE("partitioned derivative");
            EXPECT_EQ(runDerivativeCheck(simulation, diffMode, false), 0);
        }
        {
            SCOPED_TRACE("algebraic partition Jacobian");
            EXPECT_EQ(runJacobianCheck(simulation, algMode, false), 0);
        }
        {
            SCOPED_TRACE("differential partition Jacobian");
            EXPECT_EQ(runJacobianCheck(simulation, diffMode, false), 0);
        }

        ASSERT_EQ(simulation->run(0.05), 0);
        for (const auto mode : {algMode, diffMode}) {
            for (const double value : simulation->getState(mode)) {
                EXPECT_TRUE(std::isfinite(value));
                EXPECT_LT(std::abs(value), 10.0);
            }
        }
    }
}

#ifdef ENABLE_IN_DEVELOPMENT_CASES
#    ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(LoadTests, MotorTest5)
{
    std::string fileName = std::string(LOAD_TEST_DIRECTORY "motorload_test5.xml");
    readerConfig::setPrintMode(0);
    auto gds = readSimXMLFile(fileName);

    GridBus* bus = gds->getBus(1);
    auto mtld = dynamic_cast<MotorLoad5*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::INITIALIZED);
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode);
    gds->run();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}
#    endif
#endif

TEST_F(LoadTests, FdepTest)
{
    std::string fileName = makeLoadTestPath("fdepLoad.xml");
    readerConfig::setPrintMode(0);
    auto gds = readSimXMLFile(fileName);

    GridBus* bus = gds->getBus(2);
    auto mtld = dynamic_cast<FDepLoad*>(bus->getLoad());

    ASSERT_NE(mtld, nullptr);
    gds->pFlowInitialize();
    runJacobianCheck(gds, cPflowSolverMode);
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);
    runJacobianCheck(gds, cDaeSolverMode);
    gds->run();
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}

TEST_F(LoadTests, FdepLoadOptionalFrequencyFilter)
{
    std::string fileName = makeLoadTestPath("fdepLoad.xml");
    readerConfig::setPrintMode(0);
    auto gds = readSimXMLFile(fileName);

    auto* bus = gds->getBus(2);
    auto* fload = dynamic_cast<FDepLoad*>(bus->getLoad(0));
    ASSERT_NE(fload, nullptr);

    fload->set("betap", 1.0);
    fload->set("betaq", 1.0);
    fload->add(new griddyn::blocks::LeadLagBlock(2.0, 0.0, 1.0, "frequency_filter"));

    ASSERT_NE(fload->getFrequencyFilter(), nullptr);
    gds->pFlowInitialize();
    gds->dynInitialize();

    EXPECT_EQ(fload->algSize(cDaeSolverMode), 1);
    EXPECT_EQ(fload->diffSize(cDaeSolverMode), 1);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode), 0);

    const double initializedFrequency = fload->getFrequencyFilter()->getBlockOutput();
    EXPECT_NEAR(initializedFrequency, bus->getFreq(), 1e-10);

    EXPECT_EQ(gds->run(), 0);
    requireStates(gds->currentProcessState(), GridDynSimulation::GridState::DYNAMIC_COMPLETE);

    const double initialFrequency = fload->getFrequencyFilter()->getBlockOutput();

    const double inputFrequency = initialFrequency + 0.1;
    const double expectedFrequency =
        inputFrequency + ((initialFrequency - inputFrequency) * std::exp(-0.5));
    fload->timestep(gds->getSimulationTime() + 1.0,
                    {bus->getVoltage(), bus->getAngle(), inputFrequency},
                    cLocalSolverMode);

    EXPECT_NEAR(fload->getFrequencyFilter()->getBlockOutput(), expectedFrequency, 1e-10);
    EXPECT_NEAR(fload->getRealPower(1.0), fload->getRealPower(1.0, expectedFrequency), 1e-10);
    EXPECT_NEAR(fload->getReactivePower(1.0),
                fload->getReactivePower(1.0, expectedFrequency),
                1e-10);
}

TEST_F(LoadTests, FdepLoadOutputAfterSolvedDynamicInitialization)
{
    auto gds = std::make_unique<GridDynSimulation>();
    auto* bus = new AcBus("bus");
    bus->set("type", "swing");
    bus->set("voltage", 1.08);
    bus->add(new griddyn::DynamicGenerator("slack_generator"));
    auto* fload = new FDepLoad(0.5, 0.2, "fload");
    fload->set("ap", 1.0);
    fload->set("aq", 0.0);
    fload->set("betap", 1.0);
    fload->set("betaq", 0.5);
    fload->add(new griddyn::blocks::LeadLagBlock(2.0, 0.0, 1.0, "frequency_filter"));
    bus->add(fload);
    gds->add(bus);

    ASSERT_EQ(gds->powerflow(), 0);
    const double solvedRealPower = fload->getRealPower();
    const double solvedReactivePower = fload->getReactivePower();

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_NEAR(fload->getOutput(POUT_LOCATION), solvedRealPower, 1.0e-10);
    EXPECT_NEAR(fload->getOutput(QOUT_LOCATION), solvedReactivePower, 1.0e-10);
}

TEST_F(LoadTests, SvdSwitchingHonorsIterationAndErrorGate)
{
    auto gds = std::make_unique<GridDynSimulation>();
    auto* bus = new AcBus("bus");
    bus->set("type", "swing");
    bus->set("voltage", 0.9);
    auto* shunt = new Svd("shunt");
    bus->add(shunt);
    gds->add(bus);
    shunt->configureAndesShunt({0.0}, {0.05, 0.05}, {1, 1}, 1.0, 0.05, 30.0, 0.0, 0.0);
    shunt->set("min_iter", 2.0);
    shunt->set("err_tol", 0.01);

    ASSERT_EQ(gds->pFlowInitialize(), 0);
    EXPECT_EQ(shunt->get("andesstep"), 0.0);

    EXPECT_EQ(shunt->powerFlowAdjust({0.9, 0.0, 1.0, 1.0, 1.0}, 0, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::NO_CHANGE);
    EXPECT_EQ(shunt->get("andesstep"), 0.0);

    EXPECT_EQ(shunt->powerFlowAdjust({0.9, 0.0, 1.0, 2.0, 1.0}, 0, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::JACOBIAN_CHANGE);
    EXPECT_EQ(shunt->get("andesstep"), 1.0);

    shunt->reset();
    EXPECT_EQ(shunt->powerFlowAdjust({0.9, 0.0, 1.0, 1.0, 0.005}, 0, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::JACOBIAN_CHANGE);
    EXPECT_EQ(shunt->get("andesstep"), 1.0);
}

TEST_F(LoadTests, SvdSteppedControlHonorsInitialLevel)
{
    auto gds = std::make_unique<GridDynSimulation>();
    auto* bus = new AcBus("bus");
    bus->set("type", "swing");
    bus->set("voltage", 0.9);
    auto* shunt = new Svd("shunt");
    bus->add(shunt);
    gds->add(bus);

    shunt->set("mode", "stepped");
    shunt->set("vmin", 0.95);
    shunt->set("vmax", 1.05);
    shunt->addBlock(2, -0.05);
    shunt->set("yq", -0.05);
    shunt->setInitialReactivePower(-0.05);

    ASSERT_EQ(gds->pFlowInitialize(), 0);
    EXPECT_EQ(shunt->get("step"), 1.0);
    EXPECT_EQ(shunt->powerFlowAdjust(noInputs, 0, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::PARAMETER_CHANGE);
    EXPECT_EQ(shunt->get("step"), 2.0);
    EXPECT_NEAR(shunt->get("yq"), -0.10, 1.0e-12);

    bus->set("voltage", 1.1);
    EXPECT_EQ(shunt->powerFlowAdjust(noInputs, 0, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::PARAMETER_CHANGE);
    EXPECT_EQ(shunt->get("step"), 1.0);
    EXPECT_NEAR(shunt->get("yq"), -0.05, 1.0e-12);
}

TEST_F(LoadTests, SvdContinuousControlHasConsistentPowerFlowJacobian)
{
    auto gds = std::make_unique<GridDynSimulation>();
    auto* slack = new AcBus("slack");
    auto* bus = new AcBus("bus");
    slack->set("type", "swing");
    slack->set("voltage", 1.0);
    bus->set("type", "pq");
    bus->set("voltage", 1.0);
    auto* shunt = new Svd("shunt");
    bus->add(shunt);
    gds->add(slack);
    gds->add(bus);
    auto* line = new AcLine(0.01, 0.1, "line");
    line->updateBus(slack, 1);
    line->updateBus(bus, 2);
    gds->add(line);

    shunt->set("mode", "continuous");
    shunt->set("vmin", 1.0);
    shunt->set("vmax", 1.0);
    shunt->addBlock(2, -0.05);
    shunt->set("yq", -0.05);

    ASSERT_EQ(gds->pFlowInitialize(), 0);
    EXPECT_EQ(shunt->algSize(cPflowSolverMode), 1U);
    EXPECT_EQ(runJacobianCheck(gds, cPflowSolverMode), 0);
}

TEST_F(LoadTests, ApproxloadTest1)
{
    ApproximatingLoad apload("apload1");

    ld1 = new ZipLoad("zload1");
    ld1->set("p", 0.4);
    ld1->set("q", 0.3);
    ld1->set("yp", 0.1);
    ld1->set("yq", -0.12);
    ld1->set("ip", 0.03);
    ld1->set("iq", 0.06);
    apload.add(ld1);
    apload.pFlowInitializeA(0, 0);
    apload.pFlowInitializeB();
    auto realPower = apload.get("p");
    auto reactivePower = apload.get("q");
    auto admittanceReal = apload.get("yp");
    auto admittanceReactive = apload.get("yq");
    auto currentReal = apload.get("ip");
    auto currentReactive = apload.get("iq");

    EXPECT_NEAR(realPower, 0.4, 0.001);
    EXPECT_NEAR(reactivePower, 0.3, 0.001);
    EXPECT_NEAR(admittanceReal, 0.1, 0.001);
    EXPECT_NEAR(admittanceReactive, -0.12, 0.001);
    EXPECT_NEAR(currentReal, 0.03, 0.001);
    EXPECT_NEAR(currentReactive, 0.06, 0.001);

    ld1->set("p", 0.5);
    ld1->set("q", -0.1);
    ld1->set("yp", 0.13);
    ld1->set("yq", -0.12);
    ld1->set("ip", 0);
    ld1->set("iq", 0.23);

    apload.updateA(2);
    apload.updateB();
    realPower = apload.get("p");
    reactivePower = apload.get("q");
    admittanceReal = apload.get("yp");
    admittanceReactive = apload.get("yq");
    currentReal = apload.get("ip");
    currentReactive = apload.get("iq");

    EXPECT_NEAR(realPower, 0.5, 0.001);
    EXPECT_NEAR(reactivePower, -0.1, 0.001);
    EXPECT_NEAR(admittanceReal, 0.13, 0.001);
    EXPECT_NEAR(admittanceReactive, -0.12, 0.001);
    EXPECT_NEAR(currentReal, 0.0, 1e-5);
    EXPECT_NEAR(currentReactive, 0.23, 0.001);
    ld1 = nullptr;
}

TEST_F(LoadTests, Simple3PhaseLoadTest)
{
    auto ld3 = std::make_unique<ThreePhaseLoad>();
    ld3->setPa(1.1);
    ld3->setPb(1.2);
    ld3->setPc(1.3);

    auto totalRealPower = ld3->getRealPower();
    EXPECT_NEAR(totalRealPower, 3.6, 1e-5);

    ld3->setQa(0.1);
    ld3->setQb(0.3);
    ld3->setQc(0.62);

    auto totalReactivePower = ld3->getReactivePower();
    EXPECT_NEAR(totalReactivePower, 1.02, 1e-5);

    ld3->setLoad(3.0);
    EXPECT_NEAR(ld3->get("pa"), 1.0, 1e-7);

    ld3->set("pb", 0.5);
    ld3->set("pc", 0.4);

    totalRealPower = ld3->getRealPower();
    EXPECT_NEAR(totalRealPower, 1.9, 1e-5);

    auto res = ld3->getRealPower3Phase();
    EXPECT_EQ(res.size(), 3U);
    EXPECT_NEAR(res[0], 1.0, 1e-7);
    EXPECT_NEAR(res[1], 0.5, 1e-7);
    EXPECT_NEAR(res[2], 0.4, 1e-7);

    auto res2 = ld3->getRealPower3Phase(PhaseType::PNZ);
    EXPECT_EQ(res2.size(), 3U);
    EXPECT_NEAR(res2[0], 1.9, 1e-5);

    ld3->setLoad(2.7, 0.3);
    res = ld3->getRealPower3Phase();
    EXPECT_NEAR(res[0], 0.9, 1e-7);
    EXPECT_NEAR(res[1], 0.9, 1e-7);
    EXPECT_NEAR(res[2], 0.9, 1e-7);

    res = ld3->getRealPower3Phase(PhaseType::PNZ);
    EXPECT_NEAR(res[0], 2.7, 1e-7);
    EXPECT_NEAR(res[1], 0.0, 1e-7);
    EXPECT_NEAR(res[2], 0.0, 1e-7);

    res = ld3->getReactivePower3Phase(PhaseType::PNZ);
    EXPECT_NEAR(res[0], 0.3, 1e-7);
    EXPECT_NEAR(res[1], 0.0, 1e-7);
    EXPECT_NEAR(res[2], 0.0, 1e-7);
}

TEST_F(LoadTests, Secondary3PhaseLoadTest)
{
    auto ld3 = std::make_unique<ThreePhaseLoad>();

    ld3->setLoad(5.0, 1.0);

    ld3->set("imaga", 3.0);
    auto phaseARealPower = ld3->get("pa");
    ld3->set("imaga", 6.0);
    auto phaseARealPower2 = ld3->get("pa");
    EXPECT_NEAR(phaseARealPower * 2.0, phaseARealPower2, 1e-5);
}
