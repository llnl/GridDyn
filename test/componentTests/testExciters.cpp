/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/coreDefinitions.hpp"
#include "core/objectFactory.hpp"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Generator.h"
#include "solvers/solverMode.hpp"
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define EXCITER_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/exciter_tests/"

using namespace griddyn;

namespace {

using exciter_parameter_map =
    std::map<std::string, std::vector<std::pair<std::string, double>>>;

// NOLINTNEXTLINE(misc-multiple-inheritance)
class ExciterTests: public gridDynSimulationTestFixture, public ::testing::Test {
};

void applyExciterParameters(coreObject* object,
                            const exciter_parameter_map& parameters,
                            const std::string& exciterName)
{
    const auto parameterIter = parameters.find(exciterName);
    if (parameterIter == parameters.end()) {
        return;
    }

    for (const auto& parameterValue : parameterIter->second) {
        object->set(parameterValue.first, parameterValue.second);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Generator* loadExciterCase(ExciterTests& fixture,
                           coreObjectFactory* factory,
                           const std::string& caseFileName,
                           const std::string& exciterName,
                           const exciter_parameter_map& parameters)
{
    fixture.gds = readSimXMLFile(caseFileName);
    auto* generator = fixture.gds->getGen(0);
    EXPECT_NE(generator, nullptr);
    if (generator == nullptr) {
        return nullptr;
    }

    fixture.gds->consolePrintLevel = print_level::no_print;
    auto* object = factory->createObject("exciter", exciterName);
    EXPECT_NE(object, nullptr) << "Failed to create exciter " << exciterName;
    if (object == nullptr) {
        return nullptr;
    }

    applyExciterParameters(object, parameters, exciterName);
    generator->add(object);
    return generator;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void verifyStabilityCase(ExciterTests& fixture,
                         const std::string& caseFileName,
                         const exciter_parameter_map& parameters,
                         double minVoltage0,
                         double maxVoltage0,
                         double minVoltage1,
                         double maxVoltage1,
                         const std::vector<std::string>& skippedExcters = {})
{
    auto factory = coreObjectFactory::instance();
    const auto exciterList = factory->getTypeNames("exciter");

    for (const auto& exciterName : exciterList) {
        if (exciterName.starts_with("fmi")) {
            continue;
        }
        if (std::find(skippedExcters.begin(), skippedExcters.end(), exciterName) !=
            skippedExcters.end()) {
            continue;
        }

        auto* generator =
            loadExciterCase(fixture, factory.get(), caseFileName, exciterName, parameters);
        ASSERT_NE(generator, nullptr);

        const int returnValue = fixture.gds->dynInitialize();
        fixture.requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
        EXPECT_EQ(returnValue, 0) << "Exciter " << exciterName << " dynInitialize issue";

        const int badResidual = runResidualCheck(fixture.gds, cDaeSolverMode, false);
        ASSERT_EQ(badResidual, 0) << "Exciter " << exciterName << " residual issue";
        const int badJacobian = runJacobianCheck(fixture.gds, cDaeSolverMode, false);
        ASSERT_EQ(badJacobian, 0) << "Exciter " << exciterName << " Jacobian issue";

        fixture.gds->run();
        if (fixture.gds->getSimulationTime() < 30.0) {
            fixture.gds->saveRecorders();
        }
        ASSERT_GE(fixture.gds->getSimulationTime(), 30.0)
            << "Exciter " << exciterName << " didn't complete";

        std::vector<double> voltages;
        fixture.gds->getVoltage(voltages);
        EXPECT_TRUE((voltages[0] > minVoltage0) && (voltages[0] < maxVoltage0))
            << "Exciter " << exciterName;
        EXPECT_TRUE((voltages[1] > minVoltage1) && (voltages[1] < maxVoltage1))
            << "Exciter " << exciterName;
    }
}

}  // namespace

TEST_F(ExciterTests, RootExciterTest)
{
    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_root_exciter.xml");

    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    const int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);

    const std::vector<double> initialState = gds->getState();

    gds->run();
    requireState(gridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    const std::vector<double> finalState = gds->getState();

    // check for stability
    const auto diff = gmlc::utilities::countDiffs(initialState, finalState, 0.0001);
    EXPECT_EQ(diff, 0U);
}

TEST_F(ExciterTests, BasicStabilityTest1)
{
    static const exciter_parameter_map parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");
    verifyStabilityCase(*this, fileName, parameters, 0.95, 1.00, 0.95, 1.000);
}

TEST_F(ExciterTests, BasicStabilityTest2)
{
    static const exciter_parameter_map parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability2.xml");
    verifyStabilityCase(*this, fileName, parameters, 1.00, 1.05, 0.99, 1.04);
}

TEST_F(ExciterTests, BasicStabilityTest3)
{
    static const exciter_parameter_map parameters{
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.3}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability3.xml");
    verifyStabilityCase(*this, fileName, parameters, 0.98, 1.02, 0.97, 1.02, {"dc1a", "sexs"});
}

TEST_F(ExciterTests, BasicStabilityTest4)
{
    static const exciter_parameter_map parameters{
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.3}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability4.xml");
    verifyStabilityCase(*this, fileName, parameters, 0.98, 1.02, 0.97, 1.02, {"dc1a", "sexs"});
}

#ifdef LOAD_CVODE
TEST_F(ExciterTests, ExciterTest2AlgDiffTests)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");

    auto cof = coreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " residual issue";
        mmatch = runDerivativeCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " derivative issue";
        mmatch = runAlgebraicCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " algebraic issue";
    }
}

TEST_F(ExciterTests, ExciterAlgDiffJacobianTests)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");

    auto cof = coreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.compare(0, 3, "fmi") == 0) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = print_level::no_print;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& pp : fnd->second) {
                obj->set(pp.first, pp.second);
            }
        }

        gen->add(obj);
        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Exciter " << excname << " dynInitialize issue";
        auto mmatch = runJacobianCheck(gds, cDynDiffSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " Jacobian dynDiff issue";
        mmatch = runJacobianCheck(gds, cDynAlgSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Exciter " << excname << " Jacobian dynAlg issue";
    }
}
#endif
