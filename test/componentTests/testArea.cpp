/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/controllers/AGControl.h"
#include "griddyn/controllers/Scheduler.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/relays/Sensor.h"
#include "griddyn/renewables/DistributedConverter.h"
#include "griddyn/simulation/GridSimulation.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>
// testP case for CoreObject object

#define AREA_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/area_tests/"

using namespace griddyn;

class AreaTests: public GridDynSimulationTestFixture, public ::testing::Test {};

class FixedFlowLink: public Link {
  public:
    double p1{0.0};
    double p2{0.0};

    [[nodiscard]] double getRealPower(id_type_t terminal) const override
    {
        return (terminal == 2) ? p2 : p1;
    }
};

class MutableFrequencySensor: public Sensor {
  public:
    double frequency = 1.0;
    explicit MutableFrequencySensor(const std::string& name): Sensor(name) { m_outputSize = 1; }
    [[nodiscard]] double getOutput(index_t /*outNum*/) const override { return frequency; }
};

TEST_F(AreaTests, AreaTest1)
{
    std::string fileName = std::string(AREA_TEST_DIRECTORY "area_test1.xml");

    gds = readSimXMLFile(fileName);
    requireState(GridDynSimulation::GridState::STARTUP);

    gds->pFlowInitialize();
    requireState(GridDynSimulation::GridState::INITIALIZED);

    int count;
    count = gds->getInt("totalareacount");
    EXPECT_EQ(count, 1);
    count = gds->getInt("totalbuscount");
    EXPECT_EQ(count, 9);
    // check the linkcount
    count = gds->getInt("totallinkcount");
    EXPECT_EQ(count, 9);

    gds->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    auto state = gds->getState();

    fileName = std::string(AREA_TEST_DIRECTORY "area_test0.xml");

    gds2 = readSimXMLFile(fileName);

    gds2->powerflow();
    requireState(GridDynSimulation::GridState::POWERFLOW_COMPLETE);

    auto st2 = gds2->getState();
    auto diffs = gmlc::utilities::countDiffs(state, st2, 0.00001);
    EXPECT_EQ(diffs, 0);
}

TEST_F(AreaTests, InterAreaTransferLoadsFromXml)
{
    gds = readSimXMLFile(std::string(AREA_TEST_DIRECTORY "area_transfer.xml"));
    ASSERT_NE(gds, nullptr);
    const auto* root = gds.get();
    const auto& transfers = root->getInterAreaTransfers();
    ASSERT_EQ(transfers.size(), 1U);
    EXPECT_EQ(transfers[0].fromAreaID, 101);
    EXPECT_EQ(transfers[0].toAreaID, 102);
    EXPECT_EQ(transfers[0].transferID, "T1");
    EXPECT_DOUBLE_EQ(transfers[0].scheduledMW, -37.5);
    EXPECT_EQ(transfers[0].fromArea, root->findByUserID("area", 101));
    EXPECT_EQ(transfers[0].toArea, root->findByUserID("area", 102));
}

TEST_F(AreaTests, PairwiseTieFlowAndScheduleUseAreaOutflowSign)
{
    GridArea network("network");
    auto* areaOne = new GridArea("areaOne");
    auto* areaTwo = new GridArea("areaTwo");
    auto* areaThree = new GridArea("areaThree");
    areaOne->setUserID(101);
    areaTwo->setUserID(102);
    areaThree->setUserID(103);
    network.add(areaOne);
    network.add(areaTwo);
    network.add(areaThree);

    auto* busOne = new GridBus("busOne");
    auto* busTwo = new GridBus("busTwo");
    auto* busThree = new GridBus("busThree");
    areaOne->add(busOne);
    areaTwo->add(busTwo);
    areaThree->add(busThree);

    auto* forwardLink = new FixedFlowLink();
    forwardLink->updateBus(busOne, 1);
    forwardLink->updateBus(busTwo, 2);
    forwardLink->p1 = 0.4;
    forwardLink->p2 = -0.39;
    network.add(forwardLink);

    auto* reverseLink = new FixedFlowLink();
    reverseLink->updateBus(busTwo, 1);
    reverseLink->updateBus(busOne, 2);
    reverseLink->p1 = -0.19;
    reverseLink->p2 = 0.2;
    network.add(reverseLink);

    auto* unrelatedLink = new FixedFlowLink();
    unrelatedLink->updateBus(busOne, 1);
    unrelatedLink->updateBus(busThree, 2);
    unrelatedLink->p1 = 0.7;
    network.add(unrelatedLink);

    network.setInterAreaTransfer(101, 102, "A", 50.0);
    network.setInterAreaTransfer(102, 101, "B", 10.0);

    EXPECT_NEAR(areaOne->getTieFlowReal(102), 0.6, 1e-12);
    EXPECT_NEAR(areaTwo->getTieFlowReal(101), -0.58, 1e-12);
    EXPECT_NEAR(areaOne->getScheduledTieFlowReal(102), 0.4, 1e-12);
    EXPECT_NEAR(areaTwo->getScheduledTieFlowReal(101), -0.4, 1e-12);
    EXPECT_EQ(areaOne->getScheduledTieFlowReal(103), kNullVal);
    EXPECT_NEAR(areaOne->getBoundaryTieFlowReal(), 1.3, 1e-12);
    EXPECT_NEAR(areaTwo->getBoundaryTieFlowReal(), -0.58, 1e-12);
}

TEST_F(AreaTests, AGCLoadsAsAreaOwnedSampledController)
{
    gds = readSimXMLFile(std::string(AREA_TEST_DIRECTORY "area_agc.xml"));
    ASSERT_NE(gds, nullptr);
    auto* area = dynamic_cast<GridArea*>(gds->findByUserID("area", 101));
    ASSERT_NE(area, nullptr);
    ASSERT_NE(area->getAGControl(), nullptr);
    EXPECT_EQ(area->getAGControl()->getParent(), area);
    EXPECT_DOUBLE_EQ(area->getAGControl()->get("sampleinterval"), 6.0);
    auto copy = std::unique_ptr<GridArea>(static_cast<GridArea*>(area->clone()));
    ASSERT_NE(copy->getAGControl(), nullptr);
    EXPECT_NE(copy->getAGControl(), area->getAGControl());
    EXPECT_DOUBLE_EQ(copy->getAGControl()->get("sampleinterval"), 6.0);
}

TEST_F(AreaTests, AGCRejectsNonFiniteAndUnrepresentableSampleIntervals)
{
    AGControl agc("agc");
    EXPECT_THROW(agc.set("sampleinterval", std::numeric_limits<double>::quiet_NaN()),
                 InvalidParameterValue);
    EXPECT_THROW(agc.set("sampleinterval", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    EXPECT_THROW(agc.set("sampleinterval", std::numeric_limits<double>::max()),
                 InvalidParameterValue);
}

TEST_F(AreaTests, AGCUsesEventQueueSampleTimes)
{
    GridSimulation network("sampled-network");
    network.setScheduledNetInterchangeMW(0.0);
    auto* agc = new AGControl("agc");
    network.add(agc);
    agc->set("bias", -10.0);
    IOdata initializedOutput;
    agc->dynInitializeA(0.0, 0);
    agc->dynInitializeB({}, {}, initializedOutput);
    agc->set("frequency", 0.999);
    network.timestep(3.9, {}, cPflowSolverMode);
    EXPECT_DOUBLE_EQ(agc->getACE(), 0.0);
    network.timestep(4.0, {}, cPflowSolverMode);
    EXPECT_NEAR(agc->getACE(), -6.0, 1e-10);
    agc->set("frequency", 1.0);
    network.timestep(7.9, {}, cPflowSolverMode);
    EXPECT_NEAR(agc->getACE(), -6.0, 1e-10);
    network.timestep(8.0, {}, cPflowSolverMode);
    EXPECT_NEAR(agc->getACE(), 0.0, 1e-10);
}

TEST_F(AreaTests, AGCUsesBilateralScheduleAndRejectsMissingSensor)
{
    GridArea network("network");
    auto* area = new GridArea("controlled");
    auto* other = new GridArea("other");
    area->setUserID(101);
    other->setUserID(102);
    network.add(area);
    network.add(other);
    network.setInterAreaTransfer(101, 102, "schedule", 25.0);
    auto* agc = new AGControl("agc");
    area->add(agc);
    auto* unrelatedScheduler = new SchedulerReg("unrelated");
    other->add(unrelatedScheduler);
    EXPECT_THROW(agc->add(unrelatedScheduler), InvalidParameterValue);
    agc->set("frequencysensor", "missing");
    EXPECT_THROW(agc->dynInitializeA(0.0, 0), InvalidParameterValue);
    agc->set("frequencysensor", "");
    agc->dynInitializeA(0.0, 0);
    IOdata initializedOutput;
    agc->dynInitializeB({}, {}, initializedOutput);
    EXPECT_NEAR(agc->getACE(), -25.0, 1e-10);
}

TEST_F(AreaTests, AGCUpdatesOnlyOnSampleAndDispatchesACE)
{
    GridArea network("network");
    auto* area = new GridArea("controlled");
    auto* other = new GridArea("other");
    area->setUserID(101);
    other->setUserID(102);
    network.add(area);
    network.add(other);
    auto* bus = new GridBus("bus");
    auto* otherBus = new GridBus("otherBus");
    area->add(bus);
    other->add(otherBus);
    auto* link = new FixedFlowLink();
    link->updateBus(bus, 1);
    link->updateBus(otherBus, 2);
    link->p1 = 0.6;
    network.add(link);
    area->setScheduledNetInterchangeMW(50.0);

    auto* sensor = new MutableFrequencySensor("frequency");
    area->add(sensor);
    auto* agc = new AGControl("agc");
    area->add(agc);
    agc->set("frequencysensor", "frequency");
    agc->set("tf", 0.0);
    agc->set("tr", 0.0);
    agc->set("ki", 0.0);
    agc->set("bias", -10.0);
    auto* scheduler = new SchedulerReg("participating");
    area->add(scheduler);
    scheduler->regSettings(true, 1.0, 1.0);

    IOdata initializedOutput;
    agc->dynInitializeA(0.0, 0);
    agc->dynInitializeB({}, {}, initializedOutput);
    ASSERT_EQ(initializedOutput.size(), 1U);
    EXPECT_DOUBLE_EQ(agc->getACE(), 10.0);
    EXPECT_DOUBLE_EQ(scheduler->getRegTarget(), 0.0);
    EXPECT_EQ(agc->diffSize(cDaeSolverMode), 0);

    sensor->frequency = 0.999;
    agc->updateA(3.9);
    EXPECT_DOUBLE_EQ(agc->getACE(), 10.0);
    EXPECT_DOUBLE_EQ(scheduler->getRegTarget(), 0.0);
    agc->updateA(4.0);
    EXPECT_NEAR(agc->getACE(), 4.0, 1e-10);
    EXPECT_NEAR(scheduler->getRegTarget(), -0.04, 1e-10);
    agc->updateB();

    link->p1 = 0.4;
    sensor->frequency = 1.0;
    agc->updateA(7.9);
    EXPECT_NEAR(scheduler->getRegTarget(), -0.04, 1e-10);
    auto* nestedArea = new GridArea("nested");
    area->add(nestedArea);
    auto* secondScheduler = new SchedulerReg("secondParticipant");
    nestedArea->add(secondScheduler);
    secondScheduler->set("rating", 50.0);
    secondScheduler->regSettings(true, 1.0, 1.0);
    agc->updateA(8.0);
    EXPECT_NEAR(agc->getACE(), -10.0, 1e-10);
    EXPECT_NEAR(scheduler->getReg(), -0.04, 1e-10);
    EXPECT_NEAR(scheduler->getRegTarget(), 0.1 * 2.0 / 3.0, 1e-10);
    EXPECT_NEAR(secondScheduler->getRegTarget(), 0.1 / 3.0, 1e-10);

    agc->updateB();
    agc->set("kp", 10.0);
    link->p1 = 0.0;
    agc->updateA(12.0);
    EXPECT_NEAR(agc->getRegulation(), 1.5, 1e-10);
    EXPECT_NEAR(scheduler->getReg(), 0.1 * 2.0 / 3.0, 1e-10);
    EXPECT_NEAR(scheduler->getRegTarget(), 1.0, 1e-10);
    EXPECT_NEAR(secondScheduler->getRegTarget(), 0.5, 1e-10);
}

TEST_F(AreaTests, SchedulerRegDeliversTargetsAtConfiguredRamp)
{
    GridArea area("area");
    auto* scheduler = new SchedulerReg("regulation");
    area.add(scheduler);
    scheduler->set("ramp", 0.01);
    scheduler->regSettings(true, 1.0, 1.0);
    IOdata initializedOutput;
    scheduler->dynInitializeA(0.0, 0);
    scheduler->dynInitializeB({}, {0.0}, initializedOutput);
    scheduler->updateA(0.0);
    scheduler->setReg(0.1);
    scheduler->updateA(4.0);
    EXPECT_NEAR(scheduler->getReg(), 0.04, 1e-10);
    scheduler->updateA(8.0);
    EXPECT_NEAR(scheduler->getReg(), 0.08, 1e-10);
    scheduler->updateA(12.0);
    EXPECT_NEAR(scheduler->getReg(), 0.1, 1e-10);
    scheduler->setReg(-0.1);
    scheduler->updateA(16.0);
    EXPECT_NEAR(scheduler->getReg(), 0.06, 1e-10);
}

TEST_F(AreaTests, AGCCreatesISWSchedulerOnlyWithoutParticipants)
{
    GridArea area("controlled");
    area.setScheduledNetInterchangeMW(0.0);
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    auto* generator = new DynamicGenerator("unit");
    generator->set("pset", 0.4);
    generator->set("pmax", 0.9);
    generator->set("pmin", 0.1);
    bus->add(generator);
    auto* agc = new AGControl("agc");
    area.add(agc);

    agc->dynInitializeA(0.0, 0);
    auto* scheduler = dynamic_cast<SchedulerReg*>(generator->find("pset"));
    ASSERT_NE(scheduler, nullptr);
    EXPECT_EQ(scheduler->getParent(), generator);
    EXPECT_TRUE(scheduler->getRegEnabled());
    EXPECT_NEAR(scheduler->getRegUpAvailable(), 0.5, 1e-12);
    EXPECT_NEAR(scheduler->getRegDownAvailable(), 0.3, 1e-12);
    agc->dynInitializeA(0.0, 0);
    EXPECT_EQ(generator->find("pset"), scheduler);

    IOdata initializedOutput;
    agc->dynInitializeB({}, {}, initializedOutput);
    agc->set("tf", 0.0);
    agc->set("tr", 0.0);
    agc->set("ki", 0.0);
    agc->set("frequency", 0.999);
    agc->updateA(4.0);
    EXPECT_GT(scheduler->getRegTarget(), 0.0);
}

TEST_F(AreaTests, AGCCreatesISWSchedulerForRenewableGenerator)
{
    GridArea area("controlled");
    area.setScheduledNetInterchangeMW(0.0);
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    auto* generator = new RenewableGenerator("pv");
    generator->set("pset", 0.4);
    generator->set("pmax", 0.9);
    generator->set("pmin", 0.1);
    generator->add(new PVD1);
    bus->add(generator);
    auto* agc = new AGControl("agc");
    area.add(agc);

    agc->dynInitializeA(0.0, 0);
    auto* scheduler = dynamic_cast<SchedulerReg*>(generator->find("pset"));
    ASSERT_NE(scheduler, nullptr);
    EXPECT_EQ(scheduler->getParent(), generator);
    EXPECT_TRUE(scheduler->getRegEnabled());
    EXPECT_NEAR(scheduler->getRegUpAvailable(), 0.5, 1e-12);
    agc->dynInitializeA(0.0, 0);
    EXPECT_EQ(generator->find("pset"), scheduler);

    generator->dynInitializeA(0.0, 0);
    IOdata fields;
    generator->dynInitializeB({1.0, 0.0, 1.0}, {0.4, 0.0}, fields);
    generator->setOffset(0, cDaeSolverMode);
    std::vector<double> state(generator->stateSize(cDaeSolverMode));
    std::vector<double> rate(state.size(), 0.0);
    generator->guessState(0.0, state.data(), rate.data(), cDaeSolverMode);
    StateData data(0.0, state.data(), rate.data());
    data.stateSize = static_cast<count_t>(state.size());
    std::vector<double> residual(state.size());
    generator->residual({1.0, 0.0, 1.0}, data, residual.data(), cDaeSolverMode);
    const double initialCurrentRate = residual[2];

    agc->dynInitializeB({}, {}, fields);
    agc->set("tf", 0.0);
    agc->set("tr", 0.0);
    agc->set("ki", 0.0);
    agc->set("frequency", 0.999);
    agc->updateA(4.0);
    agc->updateA(8.0);
    EXPECT_GT(scheduler->getReg(), 0.0);
    data.time = 8.0;
    generator->residual({1.0, 0.0, 1.0}, data, residual.data(), cDaeSolverMode);
    EXPECT_GT(residual[2], initialCurrentRate);
}

TEST_F(AreaTests, AGCISWFallbackRespectsExplicitParticipant)
{
    GridArea area("controlled");
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    auto* generator = new DynamicGenerator("unit");
    bus->add(generator);
    auto* agc = new AGControl("agc");
    area.add(agc);
    auto* explicitParticipant = new SchedulerReg("explicit");
    area.add(explicitParticipant);
    explicitParticipant->regSettings(true, 1.0, 1.0);
    agc->dynInitializeA(0.0, 0);
    EXPECT_EQ(generator->find("pset"), nullptr);
}

TEST_F(AreaTests, AGCISWFallbackFindsUnlinkedGeneratorParticipant)
{
    GridArea area("controlled");
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    auto* generator = new DynamicGenerator("unit");
    bus->add(generator);
    auto* agc = new AGControl("agc");
    area.add(agc);
    auto* scheduler = new SchedulerReg("configured");
    scheduler->set("purpose", "pset");
    scheduler->regSettings(true, 1.0, 1.0);
    generator->add(scheduler);
    agc->dynInitializeA(0.0, 0);
    EXPECT_EQ(generator->find("pset"), scheduler);
}

TEST_F(AreaTests, AGCISWFallbackRejectsAmbiguousBus)
{
    GridArea area("controlled");
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    bus->add(new DynamicGenerator("unit1"));
    bus->add(new DynamicGenerator("unit2"));
    auto* agc = new AGControl("agc");
    area.add(agc);
    EXPECT_THROW(agc->dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST_F(AreaTests, AGCISWFallbackPreservesExistingPowerSource)
{
    GridArea area("controlled");
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    auto* generator = new DynamicGenerator("unit");
    bus->add(generator);
    auto* powerSource = new SchedulerReg("existing");
    powerSource->set("purpose", "pset");
    generator->add(powerSource);
    auto* agc = new AGControl("agc");
    area.add(agc);
    EXPECT_THROW(agc->dynInitializeA(0.0, 0), InvalidParameterValue);
    EXPECT_EQ(generator->find("pset"), powerSource);
}

TEST_F(AreaTests, AGCISWInvalidSensorDoesNotAttachScheduler)
{
    GridArea area("controlled");
    auto* bus = new GridBus("isw");
    area.add(bus);
    area.setInterchangeSlackBus("isw");
    auto* generator = new DynamicGenerator("unit");
    bus->add(generator);
    auto* agc = new AGControl("agc");
    area.add(agc);
    agc->set("frequencysensor", "missing");
    EXPECT_THROW(agc->dynInitializeA(0.0, 0), InvalidParameterValue);
    EXPECT_EQ(generator->find("pset"), nullptr);
}

TEST_F(AreaTests, AGCISWFallbackInitializesWithDynamicGenerator)
{
    gds = readSimXMLFile(std::string(AREA_TEST_DIRECTORY "area_agc_isw.xml"));
    ASSERT_NE(gds, nullptr);
    auto* area = dynamic_cast<GridArea*>(gds->findByUserID("area", 101));
    ASSERT_NE(area, nullptr);
    ASSERT_NE(area->getInterchangeSlackBus(), nullptr);
    ASSERT_EQ(gds->dynInitialize(), 0);
    auto* generator = dynamic_cast<DynamicGenerator*>(area->getInterchangeSlackBus()->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* scheduler = dynamic_cast<SchedulerReg*>(generator->find("pset"));
    ASSERT_NE(scheduler, nullptr);
    EXPECT_TRUE(scheduler->getRegEnabled());
    EXPECT_NEAR(scheduler->getOutput(), generator->get("pset"), 1e-6);
    auto* agc = area->getAGControl();
    ASSERT_NE(agc, nullptr);
    agc->set("tf", 0.0);
    agc->set("tr", 0.0);
    agc->set("ki", 0.0);
    agc->set("frequency", 0.999);
    gds->timestep(4.0, {}, cPflowSolverMode);
    EXPECT_GT(scheduler->getRegTarget(), 0.0);
    EXPECT_NEAR(scheduler->getReg(), 0.0, 1e-6);
    gds->timestep(8.0, {}, cPflowSolverMode);
    EXPECT_GT(scheduler->getReg(), 0.0);
    EXPECT_LE(scheduler->getReg(), 0.00401);
}

TEST_F(AreaTests, AreaTestAdd)
{
    auto area = std::make_unique<GridArea>("area1");

    auto bus1 = new GridBus("bus1");
    try {
        area->add(bus1);
        area->add(bus1);
    }
    catch (...) {
        FAIL();
    }

    auto bus2 = bus1->clone();
    try {
        area->add(bus2);
        // this is testing failure
        FAIL();
    }
    catch (const ObjectAddFailure& oaf) {
        EXPECT_EQ(oaf.who(), "area1");
    }
    bus2->setName("bus2");
    try {
        area->add(bus2);
        EXPECT_TRUE(isSameObject(bus2->getParent(), area.get()));
    }
    catch (...) {
        FAIL();
    }
}
