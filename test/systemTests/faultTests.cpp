/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/objectFactory.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/gridBus.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/links/acLine.h"
#include "griddyn/relays/breaker.h"
#include "griddyn/relays/fuse.h"
#include "griddyn/simulation/diagnostics.h"
#include <cmath>

#include <gtest/gtest.h>

/** these test cases test out the various generator components ability to handle faults
 */

using namespace griddyn;

static const std::string fault_test_directory(GRIDDYN_TEST_DIRECTORY "/fault_tests/");

class FaultTests: public gridDynSimulationTestFixture, public ::testing::Test {
};

TEST_F(FaultTests, FaultTest1)
{
    std::string fileName = fault_test_directory + "fault_test1.xml";

    auto cof = coreObjectFactory::instance();
    coreObject* obj = nullptr;

    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        gds = readSimXMLFile(fileName);
        gds->consolePrintLevel = print_level::no_print;
        obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr);

        Generator* gen = gds->getGen(0);
        gen->add(obj);

        // run till just after the fault
        gds->run(1.01);
        if (gds->hasDynamics()) {
            EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
                << "Model " << gname << " failed to run past fault";
            auto mmatch = runJacobianCheck(gds, cDaeSolverMode);
            EXPECT_EQ(mmatch, 0) << "Model " << gname << " Jacobian failure after fault";
        } else {
            continue;
        }

        // run till just after the fault clears
        gds->run(1.2);

        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run past fault clear";
        auto mmatch = runJacobianCheck(gds, cDaeSolverMode);

        EXPECT_EQ(mmatch, 0) << "Model " << gname << " Jacobian failure";

        gds->run();
        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run to completion";

        std::vector<double> volts;
        gds->getVoltage(volts);

        EXPECT_GT(volts[1], 0.96) << "Model " << gname << " voltage issue v=" << volts[1];
    }
}

// testing with an exciter added
TEST_F(FaultTests, FaultTest2)
{
    std::string fileName = fault_test_directory + "fault_test1.xml";

    auto cof = coreObjectFactory::instance();
    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        gds = readSimXMLFile(fileName);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr);

        Generator* gen = gds->getGen(0);
        gen->add(obj);

        auto exc = cof->createObject("exciter", "type1");
        gen->add(exc);
        // run till just after the fault
        gds->run(1.01);

        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run past fault";
        auto mmatch = runJacobianCheck(gds, cDaeSolverMode);
        EXPECT_EQ(mmatch, 0) << "Model " << gname << " Jacobian failure after fault";

        // run till just after the fault clears
        gds->run(1.2);

        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run past fault clear";
        mmatch = runJacobianCheck(gds, cDaeSolverMode);

        EXPECT_EQ(mmatch, 0) << "Model " << gname << " Jacobian failure";

        gds->run();
        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run to completion";

        std::vector<double> volts;
        gds->getVoltage(volts);

        EXPECT_GT(volts[1], 0.96) << "Model " << gname << " voltage issue v=" << volts[1];
    }
}
//#endif

// testing with a governor added
TEST_F(FaultTests, FaultTest3)
{
    std::string fileName = fault_test_directory + "fault_test1.xml";

    auto cof = coreObjectFactory::instance();
    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        gds = readSimXMLFile(fileName);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr);

        Generator* gen = gds->getGen(0);
        gen->add(obj);

        auto exc = cof->createObject("exciter", "type1");
        gen->add(exc);

        auto gov = cof->createObject("governor", "tgov1");
        gen->add(gov);
        // run till just after the fault
        gds->run(1.01);

        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run past fault";
        auto mmatch = runJacobianCheck(gds, cDaeSolverMode);
        EXPECT_EQ(mmatch, 0) << "Model " << gname << " Jacobian failure after fault";

        // run till just after the fault clears
        gds->run(1.2);

        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run past fault clear";
        mmatch = runJacobianCheck(gds, cDaeSolverMode);

        EXPECT_EQ(mmatch, 0) << "Model " << gname << " Jacobian failure";

        gds->run();
        EXPECT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE)
            << "Model " << gname << " failed to run to completion";

        std::vector<double> volts;
        gds->getVoltage(volts);

        EXPECT_GT(volts[1], 0.96) << "Model " << gname << " voltage issue v=" << volts[1];
    }
}

TEST_F(FaultTests, DISABLED_GecoFaultCase)
{
    std::string fileName = fault_test_directory + "geco_fault_uncoupled.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::debug;
    int retval = gds->dynInitialize();

    EXPECT_EQ(retval, 0);

    int mmatch = runJacobianCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    mmatch = runResidualCheck(gds, cDaeSolverMode);

    gds->run();
    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    // simpleRunTestXML(fileName);
}

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultDynamic)
{
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF);

    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = fault_test_directory + "link_fault2.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::trace;
    gds->consolePrintLevel = print_level::warning;

    gds->run();

    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.75); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultFuse)
{
    // test a fuse
    std::string fileName = fault_test_directory + "link_fault_fuse.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    auto obj = dynamic_cast<fuse*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    gds->run();
    auto lobj = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    ASSERT_NE(lobj, nullptr);
    EXPECT_FALSE(lobj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultFuse2)
{
    // test whether fuses are working properly
    std::string fileName = fault_test_directory + "link_fault_fuse2.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::debug;
    auto obj = dynamic_cast<Link*>(gds->find("bus8_to_bus9"));
    ASSERT_NE(obj, nullptr);
    gds->run();

    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultFuse3)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = fault_test_directory + "link_fault_fuse3.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::debug;
    // auto obj = dynamic_cast<Link *>(gds->find("bus2_to_bus3"));
    gds->dynInitialize();
    int mmatch = JacobianCheck(gds, cDaeSolverMode);
    if (mmatch > 0) {
        printStateNames(gds, cDaeSolverMode);
    }
    // ASSERT_NE(obj, nullptr);
    gds->run();
    // EXPECT_FALSE(obj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultBreaker)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = fault_test_directory + "link_fault_breaker.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    auto obj = dynamic_cast<breaker*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    gds->run();
    auto lobj = dynamic_cast<Link*>(gds->find("bus2_to_bus3"));
    ASSERT_NE(lobj, nullptr);
    EXPECT_FALSE(lobj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultBreaker2)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = fault_test_directory + "link_fault_breaker2.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    auto obj = dynamic_cast<breaker*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    gds->run();
    auto lobj = dynamic_cast<Link*>(gds->find("bus8_to_bus9"));
    ASSERT_NE(lobj, nullptr);
    EXPECT_FALSE(lobj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultBreaker3)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = fault_test_directory + "link_fault_breaker3.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    auto obj = dynamic_cast<breaker*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    gds->run();
    auto lobj = dynamic_cast<Link*>(gds->find("bus8_to_bus9"));
    ASSERT_NE(lobj, nullptr);
    EXPECT_TRUE(lobj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(FaultTests, LinkTestFaultBreaker4)
{
    // test a bunch of different link parameters to make sure all the solve properly
    std::string fileName = fault_test_directory + "link_fault_breaker4.xml";

    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::warning;
    auto obj = dynamic_cast<breaker*>(gds->getRelay(0));
    ASSERT_NE(obj, nullptr);
    gds->run();
    auto lobj = dynamic_cast<Link*>(gds->find("bus8_to_bus9"));
    ASSERT_NE(lobj, nullptr);
    EXPECT_TRUE(lobj->isConnected());
    std::vector<double> v;
    gds->getVoltage(v);
    EXPECT_TRUE(std::all_of(v.begin(), v.end(), [](double a) { return (a > 0.80); }));

    ASSERT_EQ(gds->currentProcessState(), gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}
#endif
