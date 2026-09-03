/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "core/coreDefinitions.hpp"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Generator.h"
#include "griddyn/exciters/ExciterDC1A.h"
#include "griddyn/exciters/ExciterDC2A.h"
#include "griddyn/exciters/ExciterESST3A.h"
#include "griddyn/exciters/ExciterESST4B.h"
#include "griddyn/exciters/ExciterEXAC1.h"
#include "griddyn/exciters/ExciterEXAC4.h"
#include "griddyn/exciters/ExciterEXPIC1.h"
#include "griddyn/exciters/ExciterEXST1.h"
#include "griddyn/exciters/ExciterIEEEtype1.h"
#include "griddyn/exciters/StaticExciterRectifier.h"
#include "griddyn/genmodels/GenModelGENSAL.h"
#include "solvers/SolverMode.hpp"
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
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
        if (exciterName.starts_with("fmi") || (exciterName == "esst3a") ||
            (exciterName == "esst4b") || (exciterName == "exst1") || (exciterName == "exac1") ||
            (exciterName == "exac2") || (exciterName == "exac4") || (exciterName == "expic1")) {
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

void verifyDefaultPsseSaturation(Exciter& exciter,
                                 const std::array<std::pair<double, double>, 2>& saturationPoints)
{
    EXPECT_DOUBLE_EQ(exciter.get("e1"), saturationPoints[0].first);
    EXPECT_DOUBLE_EQ(exciter.get("se1"), saturationPoints[0].second);
    EXPECT_DOUBLE_EQ(exciter.get("e2"), saturationPoints[1].first);
    EXPECT_DOUBLE_EQ(exciter.get("se2"), saturationPoints[1].second);
    exciter.set("te", 1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    std::vector<double> state(4, 0.0);
    std::vector<double> stateDerivative(state.size(), 0.0);
    std::vector<double> derivative(state.size(), 0.0);
    const auto expectFieldDerivative = [&](double fieldVoltage, double expectedDerivative) {
        state[0] = fieldVoltage;
        exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
        exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
        EXPECT_NEAR(derivative[0], expectedDerivative, 1e-12);
    };
    expectFieldDerivative(1.0, -1.0);
    for (const auto& [fieldVoltage, saturationFactor] : saturationPoints) {
        expectFieldDerivative(fieldVoltage, -fieldVoltage * (1.0 + saturationFactor));
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

TEST(ExciterModelTests, Esst4bFactoryInitializationAndPerturbedEquations)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "esst4b"));
    auto* exciter = dynamic_cast<exciters::ExciterESST4B*>(object.get());
    ASSERT_NE(exciter, nullptr);
    exciter->set("tr", 0.02);
    exciter->set("kpr", 10.0);
    exciter->set("kir", 2.0);
    exciter->set("vrmax", 99.0);
    exciter->set("vrmin", -99.0);
    exciter->set("ta", 0.1);
    exciter->set("kpm", 2.0);
    exciter->set("kim", 3.0);
    exciter->set("vmmax", 99.0);
    exciter->set("vmmin", -99.0);
    exciter->set("kg", 0.1);
    exciter->set("kp", 3.67);
    exciter->set("ki", 435.0 / 1000.0);
    exciter->set("vbmax", 20.0);
    exciter->set("kc", 0.01);
    exciter->set("xl", 0.0098);
    exciter->set("thetap", 3.33);
    exciter->dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.01;
    inputs[exciterIdInLocation] = -0.65;
    inputs[exciterIqInLocation] = 0.5;
    inputs[exciterVdInLocation] = -0.3;
    inputs[exciterVqInLocation] = 0.95;
    inputs[exciterXadIfdInLocation] = 1.1;
    IOdata fieldSet(4, 0.0);
    exciter->dynInitializeB(inputs, {2.0}, fieldSet);

    std::vector<double> state{2.1, 1.02, 0.5, 0.4, 0.3};
    std::vector<double> dstate(state.size(), 0.0);
    exciter->setState(0.0, state.data(), dstate.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVssInLocation] = 0.01;
    std::vector<double> derivative(state.size(), 0.0);
    exciter->derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -1.0, 1e-12);
    EXPECT_NEAR(derivative[2], 0.0, 1e-12);
    EXPECT_NEAR(derivative[3], 1.0, 1e-12);
    EXPECT_NEAR(derivative[4], 0.57, 1e-12);

    std::unique_ptr<CoreObject> cloned(exciter->clone());
    auto* clone = dynamic_cast<exciters::ExciterESST4B*>(cloned.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("kpr"), 10.0);
}

TEST(ExciterModelTests, StaticRectifierMatchesOpenIpslPiecewiseCurve)
{
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVqInLocation] = 1.0;
    const auto evaluate = [&inputs](double normalizedCurrent) {
        inputs[exciterXadIfdInLocation] = normalizedCurrent;
        return exciters::detail::computeRectifierData(inputs, 1.0, 0.0, 1.0, 0.0, 0.0, 10.0);
    };

    auto rectifier = evaluate(0.0);
    EXPECT_DOUBLE_EQ(rectifier.voltage, 1.0);
    EXPECT_DOUBLE_EQ(rectifier.derivatives[4], 0.0);
    rectifier = evaluate(0.2);
    EXPECT_NEAR(rectifier.voltage, 1.0 - ((577.0 / 1000.0) * 0.2), 1e-14);
    EXPECT_NEAR(rectifier.derivatives[4], -(577.0 / 1000.0), 1e-14);
    rectifier = evaluate(0.5);
    EXPECT_NEAR(rectifier.voltage, std::sqrt(0.5), 1e-14);
    rectifier = evaluate(0.8);
    EXPECT_NEAR(rectifier.voltage, (1732.0 / 1000.0) * 0.2, 1e-14);
    rectifier = evaluate(1.1);
    EXPECT_DOUBLE_EQ(rectifier.voltage, 0.0);
}

TEST_F(ExciterTests, Esst4bWithGenrouHasConsistentResidualAndJacobian)
{
    gds = readSimXMLFile(std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml"));
    auto* generator = gds->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* machine = new genmodels::GenModelGENSAL();
    machine->set("h", 4.0);
    machine->set("xl", 0.12);
    machine->set("xd", 1.41);
    machine->set("xq", 1.35);
    machine->set("xdp", 0.30);
    machine->set("xpp", 0.20);
    machine->set("tdop", 5.0);
    machine->set("tdopp", 0.05);
    machine->set("tqopp", 0.10);
    generator->add(machine);
    auto* exciter = new exciters::ExciterESST4B();
    exciter->set("vrmax", 99.0);
    exciter->set("vrmin", -99.0);
    exciter->set("vmmax", 99.0);
    exciter->set("vmmin", -99.0);
    generator->add(exciter);
    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST(ExciterModelTests, Esst4bUsesOpenIpslBoundedIntegratorStates)
{
    exciters::ExciterESST4B exciter;
    exciter.set("tr", 0.1);
    exciter.set("kpr", 10.0);
    exciter.set("kir", 2.0);
    exciter.set("vrmax", 1.0);
    exciter.set("vrmin", -1.0);
    exciter.set("ta", 0.1);
    exciter.set("kpm", 2.0);
    exciter.set("kim", 3.0);
    exciter.set("vmmax", 1.0);
    exciter.set("vmmin", -1.0);
    exciter.set("kg", 0.1);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVqInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    inputs[exciterVoltageInLocation] = 0.99;
    // State order is [Efd, Vmeas, xR, VA, xM]. At the positive integrator
    // bounds, outward error must be blocked even though neither total PI
    // output has reached its own output limit.
    std::vector<double> state{1.0, 0.99, 0.1, 0.2, 0.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    EXPECT_DOUBLE_EQ(derivative[4], 0.0);

    // An inward error releases both bounded integrators.
    state[1] = 1.01;
    state[3] = 0.0;
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], -0.02, 1e-14);
    EXPECT_NEAR(derivative[4], -0.3, 1e-14);
}

TEST(ExciterModelTests, Exst1MatchesAndesInitializationAndPerturbedEquations)
{
    exciters::ExciterEXST1 exciter;
    exciter.set("tr", 0.031);
    exciter.set("vimax", 0.41);
    exciter.set("vimin", -0.37);
    exciter.set("tc", 0.12);
    exciter.set("tb", 0.23);
    exciter.set("ka", 41.0);
    exciter.set("ta", 0.034);
    exciter.set("vrmax", 7.2);
    exciter.set("vrmin", -4.3);
    exciter.set("kc", 0.17);
    exciter.set("kf", 0.08);
    exciter.set("tf", 1.3);
    exciter.dynInitializeA(0.0, 0);

    // The operating point is the EXST1 at bus 2 in the frozen ANDES 2.0.0
    // IEEE 14-bus case. Only the controller parameters are made nontrivial
    // here so every perturbed block equation is independently observable.
    constexpr double andesTerminalVoltage = 1.0300000000000578;
    constexpr double andesFieldVoltage = 1.9709041251739947;
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = andesTerminalVoltage;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = andesFieldVoltage;
    IOdata desiredOutput{andesFieldVoltage};
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, desiredOutput, fieldSet);

    const auto& initializedState = exciter.getStates();
    ASSERT_EQ(initializedState.size(), 5U);
    EXPECT_NEAR(initializedState[0], andesFieldVoltage, 1e-12);
    EXPECT_NEAR(initializedState[1], andesTerminalVoltage, 1e-12);
    EXPECT_NEAR(initializedState[2], andesFieldVoltage / 41.0, 1e-12);
    EXPECT_NEAR(initializedState[3], andesFieldVoltage, 1e-12);
    EXPECT_NEAR(initializedState[4], andesFieldVoltage, 1e-12);

    // State order is [Efd, LG_y, LL_x, LR_y, WF_x]. These captured values
    // come from direct evaluation of the frozen ANDES EXST1 block equations.
    std::vector<double> state{2.1, 1.01, 0.035, 2.2, 2.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.02;
    inputs[exciterVsetInLocation] = 1.01;
    inputs[exciterVssInLocation] = 0.015;
    inputs[exciterXadIfdInLocation] = 1.4;

    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.3225806451612906, 1e-12);
    EXPECT_NEAR(derivative[2], 0.19897017397253203, 1e-12);
    EXPECT_NEAR(derivative[3], 6.292154586613461, 1e-12);
    EXPECT_NEAR(derivative[4], 0.15384615384615397, 1e-12);

    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.1, 1e-12);
}

TEST(ExciterModelTests, Exac1InitializesCorrectedTransducerAndAntiWindup)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.1);
    exciter.set("tb", 0.2);
    exciter.set("tc", 0.05);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.1);
    exciter.set("tf", 1.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 0.5);
    exciter.set("vrmin", -0.5);
    exciter.dynInitializeA(0.0, 0);
    exciter.setRootOffset(0, cLocalSolverMode);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 0.0;
    inputs[exciterVssInLocation] = 0.0;
    IOdata fieldSet(4, 0.0);
    // Use an in-limit operating point for initialization, then perturb all
    // blocks independently.  The sensed-voltage state is intentionally used.
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);
    const auto& initialized = exciter.getStates();
    ASSERT_EQ(initialized.size(), 6U);
    EXPECT_NEAR(initialized[1], 1.0, 1e-12);
    EXPECT_NEAR(initialized[4], 0.4, 1e-12);
    EXPECT_NEAR(initialized[5], 0.4, 1e-12);

    std::vector<double> state{0.9, 0.98, 0.1, 0.3, 1.1, 1.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.05;
    inputs[exciterVsetInLocation] = 1.02;
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.7, 1e-12);
    EXPECT_NEAR(derivative[2], -0.15, 1e-12);
    EXPECT_NEAR(derivative[3], 6.25, 1e-12);
    EXPECT_NEAR(derivative[4], -1.6, 1e-12);
    EXPECT_NEAR(derivative[5], 0.1, 1e-12);
    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.2, 1e-12);

    state[3] = 0.6;
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[3], 0.0);
    std::vector<double> roots(1, 0.0);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
}

TEST(ExciterModelTests, Expic1MatchesGridKitPerturbedEquations)
{
    exciters::ExciterEXPIC1 exciter;
    exciter.set("tr", 0.1);
    exciter.set("ka", 20.0);
    exciter.set("ta1", 0.02);
    exciter.set("vr1", 4.0);
    exciter.set("vr2", -3.0);
    exciter.set("ta2", 0.2);
    exciter.set("ta3", 0.03);
    exciter.set("ta4", 0.4);
    exciter.set("vrmax", 5.0);
    exciter.set("vrmin", -5.0);
    exciter.set("kf", 0.1);
    exciter.set("tf1", 0.5);
    exciter.set("tf2", 0.6);
    exciter.set("efdmax", 5.0);
    exciter.set("efdmin", -4.0);
    exciter.set("ke", 1.0);
    exciter.set("te", 0.5);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.8}, fieldSet);
    EXPECT_EQ(exciter.localStateNames(), (stringVec{"efd", "et", "xa", "vr1", "vr", "vf1", "vf"}));
    const auto& initialized = exciter.getStates();
    ASSERT_EQ(initialized.size(), 7U);
    EXPECT_DOUBLE_EQ(initialized[0], 0.8);
    EXPECT_DOUBLE_EQ(initialized[1], 1.0);
    EXPECT_DOUBLE_EQ(initialized[2], 0.8);
    EXPECT_DOUBLE_EQ(initialized[3], 0.8);
    EXPECT_DOUBLE_EQ(initialized[4], 0.8);
    EXPECT_DOUBLE_EQ(initialized[5], 0.8);
    EXPECT_DOUBLE_EQ(initialized[6], 0.0);

    // Local state order is [Efd, ET, xA, xR1, VR, VF1, VF].  These values
    // directly evaluate the PI, cascaded-filter, and feedback equations in
    // the GridKit EXPIC1 block diagram away from equilibrium.
    std::vector<double> state{0.9, 0.98, 0.7, 0.6, 0.5, 0.4, 0.03};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.02;
    inputs[exciterVsetInLocation] = 1.01;
    inputs[exciterVssInLocation] = 0.02;

    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[0], -0.8, 1e-12);
    EXPECT_NEAR(derivative[1], 0.4, 1e-12);
    EXPECT_NEAR(derivative[2], 0.4, 1e-12);
    EXPECT_NEAR(derivative[3], 0.54, 1e-12);
    EXPECT_NEAR(derivative[4], 0.2905, 1e-12);
    EXPECT_NEAR(derivative[5], 0.2, 1e-12);
    EXPECT_NEAR(derivative[6], -1.0 / 60.0, 1e-12);

    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(residual[index], derivative[index], 1e-12) << index;
    }
}

TEST(ExciterModelTests, Expic1UsesRawSaturationCoefficients)
{
    exciters::ExciterEXPIC1 exciter;
    exciter.set("ke", 0.0);
    exciter.set("te", 1.0);
    exciter.set("e1", 1.0);
    exciter.set("se1", 0.1);
    exciter.set("e2", 2.0);
    exciter.set("se2", 0.4);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.5}, fieldSet);

    std::vector<double> state{2.0, 0.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    // S_E(2)=0.4, hence -(K_E+S_E)E_fd = -0.8. Fitting E*S_E as
    // the saturation data would incorrectly produce -1.6 here.
    EXPECT_NEAR(derivative[0], -0.8, 1e-12);
}

TEST(ExciterModelTests, Expic1AppliesFinalRegulatorLimitToFieldAndFeedback)
{
    exciters::ExciterEXPIC1 exciter;
    exciter.set("ka", 20.0);
    exciter.set("ta2", 0.2);
    exciter.set("ta3", 0.03);
    exciter.set("ta4", 0.4);
    exciter.set("vrmax", 0.5);
    exciter.set("vrmin", -0.5);
    exciter.set("kf", 0.1);
    exciter.set("tf1", 0.5);
    exciter.set("tf2", 0.6);
    exciter.set("ke", 1.0);
    exciter.set("te", 0.5);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);

    std::vector<double> state{0.4, 0.4, 0.4, 0.8, 0.4, 0.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[0], 0.2, 1e-12);
    EXPECT_NEAR(derivative[4], 0.2, 1e-12);

    state[1] = 4.1;
    inputs[exciterVsetInLocation] = 1.1;
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    inputs[exciterVsetInLocation] = 0.9;
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);
}

TEST(ExciterModelTests, Expic1SupportsDocumentedBypassesAndValidation)
{
    exciters::ExciterEXPIC1 exciter;
    exciter.set("te", 0.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);
    EXPECT_EQ(exciter.localStateNames(), (stringVec{"efd", "xa"}));
    EXPECT_EQ(exciter.findIndex("et", cLocalSolverMode), kInvalidLocation);
    EXPECT_EQ(exciter.findIndex("vf", cLocalSolverMode), kInvalidLocation);

    std::vector<double> state{0.5, 0.6};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.1, 1e-12);
    EXPECT_DOUBLE_EQ(residual[1], 0.0);

    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "expic1"));
    auto* factoryExciter = dynamic_cast<exciters::ExciterEXPIC1*>(object.get());
    ASSERT_NE(factoryExciter, nullptr);
    EXPECT_DOUBLE_EQ(factoryExciter->get("ka"), 1.0);
    EXPECT_DOUBLE_EQ(factoryExciter->get("vrmax"), 1.0);
    EXPECT_DOUBLE_EQ(factoryExciter->get("vrmin"), -1.0);
    factoryExciter->set("kf", 0.27);
    std::unique_ptr<CoreObject> cloneObject(factoryExciter->clone());
    auto* clone = dynamic_cast<exciters::ExciterEXPIC1*>(cloneObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("kf"), 0.27);

    exciters::ExciterEXPIC1 partialRegulator;
    partialRegulator.set("ta2", 0.2);
    EXPECT_THROW(partialRegulator.dynInitializeA(0.0, 0), InvalidParameterValue);
    exciters::ExciterEXPIC1 partialFeedback;
    partialFeedback.set("kf", 0.1);
    EXPECT_THROW(partialFeedback.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(ExciterModelTests, Ieeet1UsesAndesTransducerAndQuadraticSaturation)
{
    exciters::ExciterIEEEtype1 exciter;
    exciter.set("tr", 0.1);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.1);
    exciter.set("tf", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("e1", 1.0);
    exciter.set("se1", 0.1);
    exciter.set("e2", 2.0);
    exciter.set("se2", 0.2);
    exciter.set("vrmax", 5.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);
    ASSERT_EQ(exciter.getStates().size(), 4U);
    EXPECT_DOUBLE_EQ(exciter.getStates()[3], 1.0);

    std::vector<double> state{0.7, 1.0, 0.05, 0.98};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    // ANDES ExcQuadSat gives S_e=0.1 E_fd^2 for these two points.
    EXPECT_NEAR(derivative[0], 0.502, 1e-12);
    EXPECT_NEAR(derivative[1], -5.84, 1e-12);
    EXPECT_NEAR(derivative[3], 0.2, 1e-12);
}

TEST(ExciterModelTests, CanonicalDcExciterFactoriesSupportVoltageTransducers)
{
    auto factory = CoreObjectFactory::instance();
    const std::array<std::string, 3> names{"esdc1a", "esdc2a", "exdc2"};
    for (const auto& name : names) {
        std::unique_ptr<CoreObject> object(factory->createObject("exciter", name));
        ASSERT_NE(object, nullptr) << name;
        object->set("tr", 0.1);
        object->set("e1", 1.0);
        object->set("se1", 0.1);
        object->set("e2", 2.0);
        object->set("se2", 0.2);
        auto* exciter = dynamic_cast<Exciter*>(object.get());
        ASSERT_NE(exciter, nullptr);
        exciter->dynInitializeA(0.0, 0);
        EXPECT_EQ(exciter->localStateNames().back(), "vmeas");
    }
}

TEST(ExciterModelTests, DcExciterDefaultsUsePsseTwoPointSaturation)
{
    exciters::ExciterDC1A dc1a;
    verifyDefaultPsseSaturation(dc1a, {{{2.3, 0.1}, {3.1, 0.33}}});

    exciters::ExciterDC2A dc2a;
    verifyDefaultPsseSaturation(dc2a, {{{2.29, 0.117}, {3.05, 0.279}}});
}

TEST(ExciterModelTests, Exac1ZeroTrBypassesVoltageMeasurementState)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.2);
    exciter.set("tc", 0.05);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.1);
    exciter.set("tf", 1.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 5.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);
    EXPECT_EQ(exciter.getStates().size(), 5U);
    EXPECT_EQ(exciter.localStateNames(), (stringVec{"efd", "ll", "va", "ve", "wf"}));
    EXPECT_EQ(exciter.findIndex("vmeas", cLocalSolverMode), kInvalidLocation);

    std::vector<double> state{0.4, 0.1, 0.3, 1.1, 1.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.05;
    inputs[exciterVsetInLocation] = 1.02;
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.5, 1e-12);
    EXPECT_NEAR(derivative[2], 4.5, 1e-12);
    EXPECT_NEAR(derivative[3], -1.6, 1e-12);
    EXPECT_NEAR(derivative[4], 0.1, 1e-12);
}

TEST(ExciterModelTests, Exac4HardLimitsAndFactoryClone)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "exac4"));
    auto* exciter = dynamic_cast<exciters::ExciterEXAC4*>(object.get());
    ASSERT_NE(exciter, nullptr);
    exciter->set("tr", 0.02);
    exciter->set("tb", 0.2);
    exciter->set("tc", 0.1);
    exciter->set("ka", 20.0);
    exciter->set("ta", 0.05);
    exciter->set("vimax", 0.1);
    exciter->set("vimin", -0.1);
    exciter->set("vrmax", 0.5);
    exciter->set("vrmin", -0.5);
    exciter->dynInitializeA(0.0, 0);
    exciter->setRootOffset(0, cLocalSolverMode);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter->dynInitializeB(inputs, {0.25}, fieldSet);
    std::vector<double> state{0.25, 0.8, 0.0, 0.75};
    std::vector<double> dstate(state.size(), 0.0);
    exciter->setState(0.0, state.data(), dstate.data(), cLocalSolverMode);
    std::vector<double> residual(state.size(), 0.0);
    exciter->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.25, 1e-12);
    std::vector<double> roots(2, 0.0);
    exciter->rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    EXPECT_LT(roots[1], 0.0);
    std::unique_ptr<CoreObject> cloned(exciter->clone());
    auto* clonedExciter = dynamic_cast<exciters::ExciterEXAC4*>(cloned.get());
    ASSERT_NE(clonedExciter, nullptr);
    EXPECT_DOUBLE_EQ(clonedExciter->get("vimax"), 0.1);
}

TEST(ExciterModelTests, Exst1HardLimitersAndRootsUseRegulatorOutput)
{
    exciters::ExciterEXST1 exciter;
    exciter.set("tr", 0.02);
    exciter.set("vimin", -0.1);
    exciter.set("vimax", 0.1);
    exciter.set("tc", 0.05);
    exciter.set("tb", 0.2);
    exciter.set("ka", 20.0);
    exciter.set("ta", 0.05);
    exciter.set("vrmin", -0.5);
    exciter.set("vrmax", 0.5);
    exciter.set("kc", 0.1);
    exciter.set("kf", 1.0);
    exciter.set("tf", 1.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.setRootOffset(0, cLocalSolverMode);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 0.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.25}, fieldSet);

    // LR_y exceeds the upper field bound while WF_y is zero. This is the
    // intentional divergence from frozen ANDES, which tests WF_y instead.
    // EXST1 hard limiters do not freeze any differential state.
    std::vector<double> state{1.0, 1.0, 0.0, 2.0, 2.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);

    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], -0.5, 1e-12);

    std::vector<double> roots(2, 0.0);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    EXPECT_LT(roots[1], 0.0);

    // A washout excursion still activates the input limiter, but must not
    // activate the output limiter while LR_y remains inside its bounds.
    state = {0.25, 1.0, 0.0, 0.25, -1.75};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], -0.5, 1e-12);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.0, 1e-12);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    EXPECT_GT(roots[1], 0.0);

    state = {0.25, 1.0, 0.0, 0.25, 0.25};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    EXPECT_GT(roots[1], 0.0);
}

TEST(ExciterModelTests, Exst1FactoryCloneAndParameterValidation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "exst1"));
    auto* exciter = dynamic_cast<exciters::ExciterEXST1*>(object.get());
    ASSERT_NE(exciter, nullptr);
    exciter->set("kf", 0.27);
    std::unique_ptr<CoreObject> cloned(exciter->clone());
    auto* clonedExciter = dynamic_cast<exciters::ExciterEXST1*>(cloned.get());
    ASSERT_NE(clonedExciter, nullptr);
    EXPECT_DOUBLE_EQ(clonedExciter->get("kf"), 0.27);

    exciters::ExciterEXST1 invalidExciter;
    EXPECT_ANY_THROW(invalidExciter.set("tf", 0.0));
    EXPECT_DOUBLE_EQ(invalidExciter.get("tf"), 1.0);

    // Limit ordering is intentionally checked only after all sequential
    // parameter assignments have completed.
    invalidExciter.set("vimax", -0.2);
    invalidExciter.set("vimin", -0.1);
    EXPECT_ANY_THROW(invalidExciter.dynInitializeA(0.0, 0));
}

TEST(ExciterModelTests, Esst3aValidatesIndividualParametersInSetters)
{
    exciters::ExciterESST3A exciter;
    EXPECT_ANY_THROW(exciter.set("km", 0.0));
    EXPECT_GT(exciter.get("km"), 0.0);

    // Cross-parameter ordering remains a final model validation.
    exciter.set("vmmax", -0.2);
    exciter.set("vmmin", -0.1);
    EXPECT_ANY_THROW(exciter.dynInitializeA(0.0, 0));
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

TEST_F(ExciterTests, Exst1CouplesToSynchronousGeneratorFamiliesAndHasAnalyticJacobian)
{
    const std::string fileName =
        std::string(GRIDDYN_TEST_DIRECTORY "/genmodel_tests/test_model1.xml");
    const std::vector<std::string> generatorModels{
        "basic", "3", "4", "5", "5.2", "5.3", "6", "6.2", "8", "genrou"};
    auto factory = CoreObjectFactory::instance();

    for (const auto& modelName : generatorModels) {
        gds = readSimXMLFile(fileName);
        auto* generator = gds->getGen(0);
        ASSERT_NE(generator, nullptr);

        auto* generatorModel = factory->createObject("genmodel", modelName);
        ASSERT_NE(generatorModel, nullptr) << "Generator model " << modelName;
        generator->add(generatorModel);

        auto* exciter = factory->createObject("exciter", "exst1");
        ASSERT_NE(exciter, nullptr);
        exciter->set("vimin", -2.0);
        exciter->set("vimax", 2.0);
        exciter->set("vrmin", -20.0);
        exciter->set("vrmax", 20.0);
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
        {"esdc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"esdc2a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"exdc2", {{"ta", 0.1}, {"ka", 6.0}}},
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
        {"esdc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"esdc2a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"exdc2", {{"ta", 0.1}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability2.xml");
    verifyStabilityCase(*this, fileName, parameters, 1.00, 1.05, 0.99, 1.04);
}

TEST_F(ExciterTests, BasicStabilityTest3)
{
    static const exciter_parameter_map parameters{
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.3}, {"ka", 6.0}}},
        {"esdc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"esdc2a", {{"ta", 0.3}, {"ka", 6.0}}},
        {"exdc2", {{"ta", 0.3}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability3.xml");
    verifyStabilityCase(
        *this, fileName, parameters, 0.98, 1.02, 0.97, 1.02, {"dc1a", "esdc1a", "sexs"});
}

TEST_F(ExciterTests, BasicStabilityTest4)
{
    static const exciter_parameter_map parameters{
        {"dc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"dc2a", {{"ta", 0.3}, {"ka", 6.0}}},
        {"esdc1a", {{"ta", 0.1}, {"ka", 6.0}}},
        {"esdc2a", {{"ta", 0.3}, {"ka", 6.0}}},
        {"exdc2", {{"ta", 0.3}, {"ka", 6.0}}},
    };

    const std::string fileName = std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability4.xml");
    verifyStabilityCase(
        *this, fileName, parameters, 0.98, 1.02, 0.97, 1.02, {"dc1a", "esdc1a", "sexs"});
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
        if (excname.starts_with("fmi") || (excname == "esst3a") || (excname == "exst1") ||
            (excname == "esst4b") || (excname == "exac1") || (excname == "exac2") ||
            (excname == "exac4")) {
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
        if (excname.starts_with("fmi") || (excname == "esst3a") || (excname == "exst1") ||
            (excname == "esst4b") || (excname == "exac1") || (excname == "exac2") ||
            (excname == "exac4")) {
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
