/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactory.hpp"
#include "core/coreDefinitions.hpp"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Generator.h"
#include "griddyn/exciters/ExciterESST3A.h"
#include "solvers/SolverMode.hpp"
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define EXCITER_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/exciter_tests/"

using namespace griddyn;

namespace {

using exciter_parameter_map = std::map<std::string, std::vector<std::pair<std::string, double>>>;

// NOLINTNEXTLINE(misc-multiple-inheritance)
class ExciterTests: public GridDynSimulationTestFixture, public ::testing::Test {};

void applyExciterParameters(CoreObject* object,
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
                           CoreObjectFactory* factory,
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

    fixture.gds->consolePrintLevel = PrintLevel::NO_PRINT;
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
    auto factory = CoreObjectFactory::instance();
    const auto exciterList = factory->getTypeNames("exciter");

    for (const auto& exciterName : exciterList) {
        if (exciterName.starts_with("fmi") || (exciterName == "esst3a")) {
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
        fixture.requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
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
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    const std::vector<double> initialState = gds->getState();

    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    const std::vector<double> finalState = gds->getState();

    // check for stability
    const auto diff = gmlc::utilities::countDiffs(initialState, finalState, 0.0001);
    EXPECT_EQ(diff, 0U);
}

TEST(ExciterModelTests, Esst3aMatchesAndesPerturbedEquations)
{
    exciters::ExciterESST3A exciter;
    exciter.set("tr", 0.02);
    exciter.set("vimax", 0.2);
    exciter.set("vimin", -0.2);
    exciter.set("km", 8.0);
    exciter.set("tc", 1.0);
    exciter.set("tb", 5.0);
    exciter.set("ka", 20.0);
    exciter.set("ta", 0.1);
    exciter.set("vrmax", 99.0);
    exciter.set("vrmin", -99.0);
    exciter.set("kg", 1.0);
    exciter.set("kp", 3.67);
    exciter.set("ki", 435.0 / 1000.0);
    exciter.set("vbmax", 5.48);
    exciter.set("kc", 0.01);
    exciter.set("xl", 0.0098);
    exciter.set("vgmax", 3.86);
    exciter.set("thetap", 3.33);
    exciter.set("tm", 0.4);
    exciter.set("vmmax", 99.0);
    exciter.set("vmmin", 0.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.01;
    inputs[exciterIdInLocation] = -0.65;
    inputs[exciterIqInLocation] = 0.5;
    inputs[exciterVdInLocation] = -0.3;
    inputs[exciterVqInLocation] = 0.95;
    inputs[exciterXadIfdInLocation] = 1.1;
    IOdata desiredOutput{2.0};
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, desiredOutput, fieldSet);

    // Perturbed values use the state order [Efd, Vmeas, LL, VR, VM].
    std::vector<double> state{2.1, 1.02, 0.08, 2.0, 0.55};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);

    // Direct evaluation of ANDES ESST3AModel with GridDyn's documented dq
    // sign conversion at the potential-source input.
    EXPECT_NEAR(derivative[1], -1.0, 1e-12);
    EXPECT_NEAR(derivative[2], 0.00264112443871697, 1e-12);
    EXPECT_NEAR(derivative[3], -3.47177511225661, 1e-12);
    EXPECT_NEAR(derivative[4], -3.375, 1e-12);

    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.0446694541104898, 1e-12);
}

TEST_F(ExciterTests, Esst3aSupportsSynchronousGeneratorFamilies)
{
    const std::string fileName =
        std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml");
    const std::vector<std::string> generatorModels{
        "basic", "3", "4", "5", "5.2", "5.3", "6", "6.2", "8"};
    auto factory = CoreObjectFactory::instance();

    for (const auto& modelName : generatorModels) {
        gds = readSimXMLFile(fileName);
        auto* generator = gds->getGen(0);
        ASSERT_NE(generator, nullptr);

        auto* generatorModel = factory->createObject("genmodel", modelName);
        ASSERT_NE(generatorModel, nullptr) << "Generator model " << modelName;
        generator->add(generatorModel);

        auto* exciter = factory->createObject("exciter", "esst3a");
        ASSERT_NE(exciter, nullptr);
        // Broad limits keep this compatibility test focused on signal
        // availability and coupling rather than model-specific limiter data.
        exciter->set("vimin", -2.0);
        exciter->set("vimax", 2.0);
        exciter->set("vrmin", -20.0);
        exciter->set("vrmax", 20.0);
        exciter->set("vmmin", 0.0);
        exciter->set("vmmax", 20.0);
        exciter->set("vbmax", 20.0);
        generator->add(exciter);

        ASSERT_EQ(gds->dynInitialize(), 0) << "Generator model " << modelName;
        EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0)
            << "Generator model " << modelName;
        EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0)
            << "Generator model " << modelName;
    }
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

#ifdef GRIDDYN_ENABLE_CVODE
TEST_F(ExciterTests, ExciterTest2AlgDiffTests)
{
    static const std::map<std::string, std::vector<std::pair<std::string, double>>> parameters{
        {"basic", {{"ta", 0.2}, {"ka", 11.0}}},
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml");

    auto cof = CoreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.starts_with("fmi") || (excname == "esst3a")) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = PrintLevel::NO_PRINT;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& parameterValue : fnd->second) {
                obj->set(parameterValue.first, parameterValue.second);
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

    auto cof = CoreObjectFactory::instance();

    auto exclist = cof->getTypeNames("exciter");

    // exclist.insert(exclist.begin(), "none");
    for (auto& excname : exclist) {
        if (excname.starts_with("fmi") || (excname == "esst3a")) {
            continue;
        }
        gds = readSimXMLFile(fileName);
        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        gds->consolePrintLevel = PrintLevel::NO_PRINT;
        auto obj = cof->createObject("exciter", excname);
        ASSERT_NE(obj, nullptr) << "Failed to create exciter " << excname;
        auto fnd = parameters.find(excname);

        if (fnd != parameters.end()) {
            for (auto& parameterValue : fnd->second) {
                obj->set(parameterValue.first, parameterValue.second);
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
