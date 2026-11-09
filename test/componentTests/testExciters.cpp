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
#include "griddyn/exciters/ExciterAC7B.h"
#include "griddyn/exciters/ExciterAC8B.h"
#include "griddyn/exciters/ExciterDC1A.h"
#include "griddyn/exciters/ExciterDC2A.h"
#include "griddyn/exciters/ExciterESAC5A.h"
#include "griddyn/exciters/ExciterESAC6A.h"
#include "griddyn/exciters/ExciterESST1A.h"
#include "griddyn/exciters/ExciterESST2A.h"
#include "griddyn/exciters/ExciterESST3A.h"
#include "griddyn/exciters/ExciterESST4B.h"
#include "griddyn/exciters/ExciterEXAC1.h"
#include "griddyn/exciters/ExciterEXAC2.h"
#include "griddyn/exciters/ExciterEXAC4.h"
#include "griddyn/exciters/ExciterEXPIC1.h"
#include "griddyn/exciters/ExciterEXST1.h"
#include "griddyn/exciters/ExciterIEEET3.h"
#include "griddyn/exciters/ExciterIEEEX1.h"
#include "griddyn/exciters/ExciterIEEEtype1.h"
#include "griddyn/exciters/ExciterIEEEtype2.h"
#include "griddyn/exciters/ExciterSCRX.h"
#include "griddyn/exciters/StaticExciterRectifier.h"
#include "griddyn/genmodels/GenModelGENSAE.h"
#include "griddyn/genmodels/GenModelGENSAL.h"
#include "solvers/SolverMode.hpp"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
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
        if (exciterName.starts_with("fmi") || (exciterName == "esst1a") ||
            (exciterName == "esst3a") || (exciterName == "esst4b") || (exciterName == "exst1") ||
            (exciterName == "exac1") || (exciterName == "esac1a") || (exciterName == "esac2a") ||
            (exciterName == "exac2") || (exciterName == "exac4") || (exciterName == "expic1") ||
            (exciterName == "scrx") || (exciterName == "esac6a") || (exciterName == "ac7b") ||
            (exciterName == "ac8b") || (exciterName == "ieeet3") || (exciterName == "esst2a")) {
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

void expectExciterJacobian(Exciter& exciter,
                           const IOdata& inputs,
                           const std::vector<double>& state,
                           double tolerance = 2e-5)
{
    constexpr double step = 1e-6;
    const auto stateCount = static_cast<index_t>(state.size());
    ASSERT_EQ(exciter.stateSize(cDaeSolverMode), state.size());
    exciter.setOffset(0, cDaeSolverMode);
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    stateData.cj = 0.0;
    MatrixDataSparse<double> jacobian;
    IOlocs inputLocs(exciterInputCount, kNullLocation);
    constexpr index_t inputColumnBase = 20;
    for (index_t index = 0; index < exciterInputCount; ++index) {
        inputLocs[index] = inputColumnBase + index;
    }
    exciter.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);
    jacobian.compact();

    const auto residualAt = [&exciter, &stateDerivative](const IOdata& trialInputs,
                                                         const std::vector<double>& trialState) {
        StateData trialData(0.0, trialState.data(), stateDerivative.data());
        trialData.stateSize = static_cast<count_t>(trialState.size());
        std::vector<double> residual(trialState.size(), 0.0);
        exciter.residual(trialInputs, trialData, residual.data(), cDaeSolverMode);
        return residual;
    };
    for (index_t column = 0; column < stateCount; ++column) {
        auto plus = state;
        auto minus = state;
        plus[column] += step;
        minus[column] -= step;
        const auto plusResidual = residualAt(inputs, plus);
        const auto minusResidual = residualAt(inputs, minus);
        for (index_t row = 0; row < stateCount; ++row) {
            const double numerical = (plusResidual[row] - minusResidual[row]) / (2.0 * step);
            EXPECT_NEAR(jacobian.at(row, column), numerical, tolerance)
                << "state row " << row << " column " << column;
        }
    }
    for (index_t column = 0; column < exciterInputCount; ++column) {
        auto plus = inputs;
        auto minus = inputs;
        plus[column] += step;
        minus[column] -= step;
        const auto plusResidual = residualAt(plus, state);
        const auto minusResidual = residualAt(minus, state);
        for (index_t row = 0; row < stateCount; ++row) {
            const double numerical = (plusResidual[row] - minusResidual[row]) / (2.0 * step);
            EXPECT_NEAR(jacobian.at(row, inputColumnBase + column), numerical, tolerance)
                << "input row " << row << " column " << column;
        }
    }
}

void expectExciterDaeJacobian(Exciter& exciter,
                              const IOdata& inputs,
                              const std::vector<double>& state,
                              double cj,
                              double tolerance = 2e-5)
{
    constexpr double step = 1e-6;
    const auto stateCount = static_cast<index_t>(state.size());
    ASSERT_EQ(exciter.stateSize(cDaeSolverMode), state.size());
    exciter.setOffset(0, cDaeSolverMode);
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    stateData.cj = cj;
    MatrixDataSparse<double> jacobian;
    IOlocs inputLocs(exciterInputCount, kNullLocation);
    constexpr index_t inputColumnBase = 20;
    for (index_t index = 0; index < exciterInputCount; ++index) {
        inputLocs[index] = inputColumnBase + index;
    }
    exciter.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);
    jacobian.compact();

    const auto residualAt = [&exciter](const IOdata& trialInputs,
                                       const std::vector<double>& trialState,
                                       const std::vector<double>& trialDerivative) {
        StateData trialData(0.0, trialState.data(), trialDerivative.data());
        trialData.stateSize = static_cast<count_t>(trialState.size());
        std::vector<double> residual(trialState.size(), 0.0);
        exciter.residual(trialInputs, trialData, residual.data(), cDaeSolverMode);
        return residual;
    };

    const auto baseResidual = residualAt(inputs, state, stateDerivative);
    for (index_t column = 0; column < stateCount; ++column) {
        auto trialState = state;
        auto trialDerivative = stateDerivative;
        trialState[column] += step;
        trialDerivative[column] += cj * step;
        const auto trialResidual = residualAt(inputs, trialState, trialDerivative);
        for (index_t row = 0; row < stateCount; ++row) {
            const double numerical = (trialResidual[row] - baseResidual[row]) / step;
            EXPECT_NEAR(jacobian.at(row, column), numerical, tolerance)
                << "state row " << row << " column " << column;
        }
    }
    for (index_t column = 0; column < exciterInputCount; ++column) {
        auto trialInputs = inputs;
        trialInputs[column] += step;
        const auto trialResidual = residualAt(trialInputs, state, stateDerivative);
        for (index_t row = 0; row < stateCount; ++row) {
            const double numerical = (trialResidual[row] - baseResidual[row]) / step;
            EXPECT_NEAR(jacobian.at(row, inputColumnBase + column), numerical, tolerance)
                << "input row " << row << " column " << column;
        }
    }
}

void expectExciterRootMarginsStableUnderInputNoise(Exciter& exciter, std::string_view modelName)
{
    // GridDyn's default relative tolerance is 1e-6; exercise both sides at
    // ten times that scale so this catches more than solver-level jitter.
    constexpr double inputNoise = 1e-5;
    constexpr double minimumRootClearance = 1e-6;
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterPmechInLocation] = 0.8;
    inputs[exciterOmegaInLocation] = 1.0;
    inputs[exciterIdInLocation] = 0.2;
    inputs[exciterIqInLocation] = 0.1;
    inputs[exciterVdInLocation] = 0.1;
    inputs[exciterVqInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 0.4;
    inputs[exciterVssInLocation] = 0.02;
    if (modelName == "ac8b") {
        exciter.set("vpmax", 10.0);
        exciter.set("vpmin", -10.0);
        exciter.set("vrmax", 10.0);
        exciter.set("vrmin", -10.0);
        exciter.set("vfemax", 10.0);
        exciter.set("vemin", -1.0);
    }
    IOdata fieldSet(8, 0.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.dynInitializeB(inputs, {0.5}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);
    const auto rootCount = exciter.rootSize(cLocalSolverMode);
    if (rootCount == 0) {
        return;
    }

    const auto state = exciter.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    const StateData stateData(0.0, state.data(), stateDerivative.data());
    std::vector<double> roots(rootCount, 0.0);
    exciter.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
    for (std::size_t root = 0; root < roots.size(); ++root) {
        ASSERT_TRUE(std::isfinite(roots[root])) << modelName << " root " << root;
        ASSERT_GE(std::abs(roots[root]), minimumRootClearance)
            << modelName << " root " << root << " at the initialized point";
    }
    const auto initialRoots = roots;

    for (const index_t input : {exciterVoltageInLocation, exciterVsetInLocation}) {
        const double initialValue = inputs[input];
        for (const double delta : {-inputNoise, inputNoise}) {
            inputs[input] = initialValue + delta;
            exciter.rootTest(inputs, stateData, roots.data(), cLocalSolverMode);
            for (std::size_t root = 0; root < roots.size(); ++root) {
                EXPECT_TRUE(std::isfinite(roots[root])) << modelName << " root " << root;
                EXPECT_GT(roots[root] * initialRoots[root], 0.0)
                    << modelName << " root " << root << " input " << input << " delta " << delta;
                EXPECT_GE(std::abs(roots[root]), minimumRootClearance)
                    << modelName << " root " << root << " input " << input << " delta " << delta;
            }
        }
        inputs[input] = initialValue;
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

TEST_F(ExciterTests, Esst4bZeroTrAndTaBypassStatesWithConsistentResidualAndJacobian)
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
    exciter->set("tr", 0.0);
    exciter->set("ta", 0.0);
    exciter->set("vrmax", 99.0);
    exciter->set("vrmin", -99.0);
    exciter->set("vmmax", 99.0);
    exciter->set("vmmin", -99.0);
    generator->add(exciter);
    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(exciter->solverStateNames(), (stringVec{"efd", "vrint", "vmint"}));
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(ExciterTests, GensalAllowsEqualTransientAndSubtransientReactance)
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
    machine->set("xpp", 0.30);
    machine->set("tdop", 5.0);
    machine->set("tdopp", 0.05);
    machine->set("tqopp", 0.10);
    generator->add(machine);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(ExciterTests, Esst1aWithGensaeHasConsistentResidualAndJacobian)
{
    gds = readSimXMLFile(std::string(EXCITER_TEST_DIRECTORY "test_exciter_stability.xml"));
    auto* generator = gds->getGen(0);
    ASSERT_NE(generator, nullptr);

    auto* machine = new genmodels::GenModelGENSAE();
    machine->set("h", 4.28);
    machine->set("xl", 0.12);
    machine->set("xd", 1.84);
    machine->set("xq", 1.75);
    machine->set("xdp", 0.41);
    machine->set("xpp", 0.20);
    machine->set("tdop", 5.0);
    machine->set("tdopp", 0.07);
    machine->set("tqopp", 0.09);
    machine->set("s10", 0.11);
    machine->set("s12", 0.39);
    generator->add(machine);

    auto* exciter = new exciters::ExciterESST1A();
    exciter->set("tr", 0.1);
    exciter->set("tb", 0.2);
    exciter->set("tc", 0.05);
    exciter->set("tb1", 0.1);
    exciter->set("tc1", 0.02);
    exciter->set("ka", 20.0);
    exciter->set("ta", 0.25);
    exciter->set("vimax", 2.0);
    exciter->set("vimin", -2.0);
    exciter->set("vamax", 20.0);
    exciter->set("vamin", -20.0);
    exciter->set("vrmax", 20.0);
    exciter->set("vrmin", -20.0);
    generator->add(exciter);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runAlgebraicCheck(gds, cDaeSolverMode, false), 0);
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
    // State order is [Efd, Vmeas, xR, VA, xM]. The PI integrator states are
    // additive terms, so their values alone do not determine whether the
    // corresponding output is limited.
    std::vector<double> state{1.0, 0.99, 0.1, 0.2, 0.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], 0.02, 1e-14);
    EXPECT_NEAR(derivative[4], 0.3, 1e-14);

    // Once the actual PI outputs reach their respective upper limits, an
    // outward error is blocked.
    state = {1.0, 0.99, 1.0, 0.2, 1.0};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
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

TEST(ExciterModelTests, Esst4bAdjustsInitialUpperLimitByDefault)
{
    exciters::ExciterESST4B exciter;
    exciter.set("tr", 0.1);
    exciter.set("kpr", 10.0);
    exciter.set("kir", 2.0);
    exciter.set("vrmax", 0.05);
    exciter.set("vrmin", -1.0);
    exciter.set("ta", 0.1);
    exciter.set("kpm", 2.0);
    exciter.set("kim", 3.0);
    exciter.set("vmmax", 99.0);
    exciter.set("vmmin", -99.0);
    exciter.set("kg", 0.1);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVqInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);

    EXPECT_DOUBLE_EQ(exciter.get("vrmax"), 0.1);
    EXPECT_DOUBLE_EQ(exciter.getStates()[0], 1.0);
}

TEST(ExciterModelTests, Esst4bStrictInitialUpperLimitRejectsMismatch)
{
    exciters::ExciterESST4B exciter;
    exciter.set("tr", 0.1);
    exciter.set("kpr", 10.0);
    exciter.set("kir", 2.0);
    exciter.set("vrmax", 0.05);
    exciter.set("vrmin", -1.0);
    exciter.set("ta", 0.1);
    exciter.set("kpm", 2.0);
    exciter.set("kim", 3.0);
    exciter.set("vmmax", 99.0);
    exciter.set("vmmin", -99.0);
    exciter.set("kg", 0.1);
    exciter.dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVqInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);

    EXPECT_THROW(exciter.dynInitializeB(inputs, {1.0}, fieldSet), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(exciter.get("vrmax"), 0.05);
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
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "et", "xa", "vr1", "vr", "vf1", "vf"}));
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
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "xa"}));
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

TEST(ExciterModelTests, ScrxMatchesOpenIpslPerturbedEquationsAndSelectors)
{
    exciters::ExciterSCRX exciter;
    exciter.set("tatb", 0.2);
    exciter.set("tb", 0.5);
    exciter.set("k", 10.0);
    exciter.set("te", 0.25);
    exciter.set("emin", -10.0);
    exciter.set("emax", 10.0);
    exciter.set("rcrfd", 3.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 0.1;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "xll", "efdpre"}));

    // Error = 0.42, xLL = 0.2, and amplifier state = 0.5.  The OpenIPSL SCRX
    // lead-lag gives dxLL = 0.44 and VLL = 0.244; the lag then gives
    // dAmp = (10 * 0.244 - 0.5) / 0.25 = 7.76.
    std::vector<double> state{0.3, 0.2, 0.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 0.8;
    inputs[exciterVsetInLocation] = 1.12;

    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.44, 1e-12);
    EXPECT_NEAR(derivative[2], 7.76, 1e-12);

    std::vector<double> update(state.size(), 0.0);
    exciter.algebraicUpdate(inputs, emptyStateData, update.data(), cLocalSolverMode, 1.0);
    EXPECT_NEAR(update[0], 0.4, 1e-12);
    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.1, 1e-12);

    inputs[exciterXadIfdInLocation] = -0.2;
    exciter.algebraicUpdate(inputs, emptyStateData, update.data(), cLocalSolverMode, 1.0);
    EXPECT_NEAR(update[0], 0.6, 1e-12);

    inputs[exciterXadIfdInLocation] = 0.1;
    exciter.set("cswitch", 1.0);
    exciter.algebraicUpdate(inputs, emptyStateData, update.data(), cLocalSolverMode, 1.0);
    EXPECT_NEAR(update[0], 0.5, 1e-12);
}

TEST(ExciterModelTests, Esac5aMatchesAndesEquationsAndJacobian)
{
    exciters::ExciterESAC5A exciter;
    exciter.set("tr", 0.2);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.5);
    exciter.set("te", 0.8);
    exciter.set("kf", 0.2);
    exciter.set("tf1", 0.5);
    exciter.set("tf2", 0.4);
    exciter.set("tf3", 0.1);
    exciter.set("ke", 1.0);
    exciter.set("e1", 1.0);
    exciter.set("se1", 0.1);
    exciter.set("e2", 2.0);
    exciter.set("se2", 0.4);
    exciter.dynInitializeA(0.0, 0);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "vc", "vr", "xll", "xwf", "ve"}));

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.5}, fieldSet);
    const double initialRegulator = exciter.getStates()[2];
    std::vector<double> initializedDerivative(6, 0.0);
    exciter.derivative(inputs, emptyStateData, initializedDerivative.data(), cLocalSolverMode);
    for (index_t row = 1; row < 6; ++row) {
        EXPECT_NEAR(initializedDerivative[row], 0.0, 1e-12) << row;
    }

    // ANDES: LL_y = LL_x + TF3/TF2*(VR_y-LL_x),
    // WF_y = KF/TF1*(LL_y-WF_x), VFE = KE*INT_y + Se(INT_y).
    std::vector<double> state{0.65, 1.02, 0.7, 0.6, 0.4, 1.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.03;
    inputs[exciterVsetInLocation] = 1.01;
    inputs[exciterVssInLocation] = 0.02;
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    const double leadLag = 0.6 + ((0.1 / 0.4) * (0.7 - 0.6));
    const double feedback = (0.2 / 0.5) * (leadLag - 0.4);
    const double ratio = std::sqrt((1.0 * 0.1) / (2.0 * 0.4));
    const double saturationThreshold = (1.0 - (ratio * 2.0)) / (1.0 - ratio);
    const double saturationFactor = 0.8 / std::pow(2.0 - saturationThreshold, 2);
    const double saturation = saturationFactor * std::pow(1.5 - saturationThreshold, 2);
    EXPECT_NEAR(derivative[1], (1.03 - 1.02) / 0.2, 1e-12);
    EXPECT_NEAR(derivative[2],
                ((10.0 * (1.03 + (initialRegulator / 10.0) - 1.02 - feedback)) - 0.7) / 0.5,
                1e-12);
    EXPECT_NEAR(derivative[3], (0.7 - 0.6) / 0.4, 1e-12);
    EXPECT_NEAR(derivative[4], (leadLag - 0.4) / 0.5, 1e-12);
    EXPECT_NEAR(derivative[5], (0.7 - 1.5 - saturation) / 0.8, 1e-12);

    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 1.5 - 0.65, 1e-12);
    for (index_t row = 1; row < 6; ++row) {
        EXPECT_NEAR(residual[row], derivative[row], 1e-12) << row;
    }
    expectExciterDaeJacobian(exciter, inputs, state, 1.0);
}

TEST(ExciterModelTests, Esac5aStepMatchesClosedFormAndesResponse)
{
    exciters::ExciterESAC5A exciter;
    exciter.set("tr", 0.0);
    exciter.set("ka", 2.0);
    exciter.set("ta", 0.2);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.0);
    exciter.set("tf1", 1.0);
    exciter.set("tf2", 0.0);
    exciter.set("tf3", 1.0);  // ANDES bypasses TF3 when TF2 is zero.
    exciter.set("ke", 1.0);
    exciter.set("se1", 0.0);
    exciter.set("se2", 0.0);
    exciter.dynInitializeA(0.0, 0);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "vr", "xwf", "ve"}));
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    inputs[exciterVoltageInLocation] = 0.99;
    for (int count = 1; count <= 4000; ++count) {
        exciter.timestep(count * 0.0001, inputs, cLocalSolverMode);
    }
    const double time = 0.4;
    const double expectedRegulator = 1.02 - (0.02 * std::exp(-time / 0.2));
    const double expectedField = 1.0 +
        (0.02 * (1.0 - (((0.5 * std::exp(-time / 0.5)) - (0.2 * std::exp(-time / 0.2))) / 0.3)));
    EXPECT_NEAR(exciter.getStates()[1], expectedRegulator, 2e-5);
    EXPECT_NEAR(exciter.getStates()[3], expectedField, 2e-5);
    EXPECT_NEAR(exciter.getStates()[0], expectedField, 2e-5);
}

TEST(ExciterModelTests, Esac5aLimiterAndZeroLeadLagHaveConsistentJacobian)
{
    exciters::ExciterESAC5A exciter;
    exciter.set("tr", 0.0);
    exciter.set("tf2", 0.0);
    exciter.set("tf3", 0.7);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.1);
    exciter.set("tf1", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("se1", 0.0);
    exciter.set("se2", 0.0);
    exciter.set("vrmax", 2.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    expectExciterDaeJacobian(exciter, inputs, exciter.getStates(), 1.0);

    exciter.set("vrmax", 0.5);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 0.5);
    std::vector<double> derivative(exciter.getStates().size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    expectExciterDaeJacobian(exciter, inputs, exciter.getStates(), 1.0);

    inputs[exciterVsetInLocation] = 0.8;
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);
}

TEST(ExciterModelTests, Esac6aMatchesGridKitPerturbedEquations)
{
    exciters::ExciterESAC6A exciter;
    exciter.set("tr", 0.2);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("tk", 0.05);
    exciter.set("tb", 0.4);
    exciter.set("tc", 0.1);
    exciter.set("vamax", 3.0);
    exciter.set("vamin", -3.0);
    exciter.set("vrmax", 2.0);
    exciter.set("vrmin", -2.0);
    exciter.set("te", 0.5);
    exciter.set("vfelim", 0.02);
    exciter.set("kh", 0.2);
    exciter.set("vhmax", 2.0);
    exciter.set("th", 0.5);
    exciter.set("tj", 0.1);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.1);
    exciter.set("ke", 1.0);
    exciter.set("se1", 0.0);
    exciter.set("se2", 0.0);
    exciter.set("spdmlt", 1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterOmegaInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "vc", "xa", "xll", "ve", "vf"}));

    std::vector<double> state{1.0, 1.01, 0.4, 0.3, 0.98, 0.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 0.6312;
    inputs[exciterXadIfdInLocation] = 0.6;
    inputs[exciterOmegaInLocation] = 1.2;

    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], -0.05, 1e-12);
    // The GridKit diagram places the VFE limiter after the regulator lead-lags:
    // VFE - VFELIM -> KH -> 0..VHMAX -> (1+sTJ)/(1+sTH).  Its output is
    // subtracted from VA immediately before the voltage-scaled VR limiter.
    EXPECT_NEAR(derivative[2], -29.92, 1e-12);
    EXPECT_NEAR(derivative[3], -3.49, 1e-12);
    EXPECT_NEAR(derivative[4], -3.0596, 1e-12);
    EXPECT_NEAR(derivative[5], -0.592, 1e-12);

    std::vector<double> update(state.size(), 0.0);
    exciter.algebraicUpdate(inputs, emptyStateData, update.data(), cLocalSolverMode, 1.0);
    EXPECT_NEAR(update[0], 1.176, 1e-12);
    std::vector<double> residual(state.size(), 0.0);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.176, 1e-12);
    EXPECT_TRUE(exciter.supportsLimiterSignal(ExciterLimiterSignal::UNDER));
    EXPECT_FALSE(exciter.supportsLimiterSignal(ExciterLimiterSignal::OVER));
    inputs[exciterVuelInLocation] = 0.01;
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], -28.92, 1e-12);
    expectExciterJacobian(exciter, inputs, state);
}

TEST(ExciterModelTests, Esac6aUsesVoltageScaledRegulatorAndBoundedExciter)
{
    exciters::ExciterESAC6A exciter;
    exciter.set("ka", 1.0);
    exciter.set("ta", 1.0);
    exciter.set("vamax", 10.0);
    exciter.set("vamin", -10.0);
    exciter.set("vrmax", 2.0);
    exciter.set("vrmin", -2.0);
    exciter.set("te", 1.0);
    exciter.set("vfelim", 0.0);
    exciter.set("kh", 0.0);
    exciter.set("vhmax", 10.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 0.0);
    exciter.set("se1", 0.0);
    exciter.set("se2", 0.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.setRootOffset(0, cLocalSolverMode);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.1}, fieldSet);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "xa", "ve"}));

    std::vector<double> state{0.0, -1.0, -0.1};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    std::vector<double> roots(1, 1.0);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    exciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(exciter.getStates()[2], 0.0);

    // While VE is held at zero, the root follows the unconstrained integrator
    // drive.  A positive drive crosses that root and releases the lower limit.
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    state = {0.0, 1.0, 0.0};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    exciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[0], 2e-6, 1e-12);
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
        EXPECT_GT(roots[0], 0.0) << voltage;
    }
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_GT(derivative[2], 0.0);

    // At VT=0.5, the diagram's upper regulator limit is VT*VRMAX=1.0.
    inputs[exciterVoltageInLocation] = 0.5;
    state = {0.0, 5.0, 0.5};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], 1.0, 1e-12);
}

TEST(ExciterModelTests, Esac6aAdjustsInitialUpperLimitByDefault)
{
    exciters::ExciterESAC6A exciter;
    exciter.set("ka", 1.0);
    exciter.set("ta", 1.0);
    exciter.set("vamax", 0.05);
    exciter.set("vamin", -10.0);
    exciter.set("vrmax", 10.0);
    exciter.set("vrmin", -10.0);
    exciter.set("te", 1.0);
    exciter.set("kh", 0.0);
    exciter.set("vhmax", 10.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("se1", 0.0);
    exciter.set("se2", 0.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterOmegaInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.1}, fieldSet);

    EXPECT_DOUBLE_EQ(exciter.get("vamax"), 0.1);
    EXPECT_DOUBLE_EQ(exciter.getStates()[0], 0.1);
}

TEST(ExciterModelTests, Esac6aStrictInitialUpperLimitRejectsMismatch)
{
    exciters::ExciterESAC6A exciter;
    exciter.set("ka", 1.0);
    exciter.set("ta", 1.0);
    exciter.set("vamax", 0.05);
    exciter.set("vamin", -10.0);
    exciter.set("vrmax", 10.0);
    exciter.set("vrmin", -10.0);
    exciter.set("te", 1.0);
    exciter.set("kh", 0.0);
    exciter.set("vhmax", 10.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("se1", 0.0);
    exciter.set("se2", 0.0);
    exciter.dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterOmegaInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);

    EXPECT_THROW(exciter.dynInitializeB(inputs, {0.1}, fieldSet), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(exciter.get("vamax"), 0.05);
}

TEST(ExciterModelTests, ScrxDetectsClampsAndReleasesAmplifierLimits)
{
    exciters::ExciterSCRX exciter;
    exciter.set("tatb", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("k", 10.0);
    exciter.set("te", 0.1);
    exciter.set("emin", -1.0);
    exciter.set("emax", 1.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.setRootOffset(0, cLocalSolverMode);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.5}, fieldSet);

    std::vector<double> state{1.0, 1.1};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVsetInLocation] = 2.0;
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    std::vector<double> roots(1, 0.0);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    exciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 1.0);

    // Reverse the amplifier drive and verify that its release root is crossed.
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    inputs[exciterVsetInLocation] = 0.0;
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    exciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_NEAR(roots[0], 2e-6, 1e-12);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);

    // Exercise the symmetric lower-limit entry and release path as well.
    state = {-1.0, -1.1};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    exciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], -1.0);
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    inputs[exciterVsetInLocation] = 2.0;
    exciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    exciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_GT(derivative[1], 0.0);

    state = {0.9, 0.9};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 1.0);
}

TEST(ExciterModelTests, ScrxConsumesOptionalLimiterActions)
{
    exciters::ExciterSCRX exciter;
    EXPECT_TRUE(exciter.supportsLimiterSignal(ExciterLimiterSignal::UNDER));
    EXPECT_TRUE(exciter.supportsLimiterSignal(ExciterLimiterSignal::OVER));
    exciter.set("tatb", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("te", 0.0);
    exciter.set("k", 2.0);
    exciter.set("emin", -10.0);
    exciter.set("emax", 10.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.5}, fieldSet);
    inputs[exciterVuelInLocation] = 0.10;
    inputs[exciterVoelInLocation] = 0.05;
    std::vector<double> update(exciter.getStates().size(), 0.0);
    exciter.algebraicUpdate(inputs, emptyStateData, update.data(), cLocalSolverMode, 1.0);
    EXPECT_NEAR(update[0], 0.6, 1e-12);
    expectExciterJacobian(exciter, inputs, exciter.getStates());
}

TEST(ExciterModelTests, ScrxAdjustsInitialUpperLimitByDefault)
{
    exciters::ExciterSCRX exciter;
    exciter.set("tatb", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("k", 10.0);
    exciter.set("te", 0.1);
    exciter.set("emin", -1.0);
    exciter.set("emax", 1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {1.5}, fieldSet);

    EXPECT_DOUBLE_EQ(exciter.get("emax"), 1.5);
    EXPECT_DOUBLE_EQ(exciter.getStates()[0], 1.5);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 1.5);

    exciter.setRootOffset(0, cLocalSolverMode);
    auto state = exciter.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, 0.0);
    for (const double setpoint : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVsetInLocation] = setpoint;
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0);
    }

    state[1] = -1.0;
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    StateData lowerStateData(0.0, state.data(), stateDerivative.data());
    for (const double setpoint : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVsetInLocation] = setpoint;
        exciter.rootTest(inputs, lowerStateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0);
    }
}

TEST(ExciterModelTests, ScrxStrictInitialUpperLimitRejectsMismatch)
{
    exciters::ExciterSCRX exciter;
    exciter.set("tatb", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("k", 10.0);
    exciter.set("te", 0.1);
    exciter.set("emin", -1.0);
    exciter.set("emax", 1.0);
    exciter.dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);

    EXPECT_THROW(exciter.dynInitializeB(inputs, {1.5}, fieldSet), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(exciter.get("emax"), 1.0);
}

TEST(ExciterModelTests, Esac6aUsesPssEQuadraticSaturation)
{
    exciters::ExciterESAC6A exciter;
    exciter.set("ka", 1.0);
    exciter.set("ta", 1.0);
    exciter.set("vamax", 10.0);
    exciter.set("vamin", -10.0);
    exciter.set("vrmax", 10.0);
    exciter.set("vrmin", -10.0);
    exciter.set("te", 1.0);
    exciter.set("kh", 0.0);
    exciter.set("vhmax", 10.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 0.0);
    exciter.set("e1", 1.0);
    exciter.set("se1", 0.1);
    exciter.set("e2", 2.0);
    exciter.set("se2", 0.4);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(2, 0.0);
    exciter.dynInitializeB(inputs, {0.1}, fieldSet);

    std::vector<double> state{0.0, 0.0, 2.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], -0.8, 1e-12);
}

TEST(ExciterModelTests, ScrxAndEsac6aFactoryCloneAndValidation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> scrxObject(factory->createObject("exciter", "scrx"));
    auto* scrx = dynamic_cast<exciters::ExciterSCRX*>(scrxObject.get());
    ASSERT_NE(scrx, nullptr);
    scrx->set("rcrfd", 2.7);
    scrx->set("cswitch", "solid");
    std::unique_ptr<CoreObject> scrxCloneObject(scrx->clone());
    auto* scrxClone = dynamic_cast<exciters::ExciterSCRX*>(scrxCloneObject.get());
    ASSERT_NE(scrxClone, nullptr);
    EXPECT_DOUBLE_EQ(scrxClone->get("rcrfd"), 2.7);
    EXPECT_DOUBLE_EQ(scrxClone->get("cswitch"), 1.0);
    EXPECT_THROW(scrx->set("cswitch", 2.0), InvalidParameterValue);

    std::unique_ptr<CoreObject> esac6aObject(factory->createObject("exciter", "esac6a"));
    auto* esac6a = dynamic_cast<exciters::ExciterESAC6A*>(esac6aObject.get());
    ASSERT_NE(esac6a, nullptr);
    esac6a->set("kc", 0.23);
    esac6a->set("spdmlt", "on");
    std::unique_ptr<CoreObject> esac6aCloneObject(esac6a->clone());
    auto* esac6aClone = dynamic_cast<exciters::ExciterESAC6A*>(esac6aCloneObject.get());
    ASSERT_NE(esac6aClone, nullptr);
    EXPECT_DOUBLE_EQ(esac6aClone->get("kc"), 0.23);
    EXPECT_DOUBLE_EQ(esac6aClone->get("spdmlt"), 1.0);
    EXPECT_THROW(esac6a->set("spdmlt", 2.0), InvalidParameterValue);
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

TEST(ExciterModelTests, Ieeet1AdjustsInitialRegulatorUpperLimitByDefault)
{
    exciters::ExciterIEEEtype1 exciter;
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("kf", 0.0);
    exciter.set("tf", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 1.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {2.0}, fieldSet);

    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 2.0);
    std::vector<double> stateDerivative(exciter.getStates().size(), 0.0);
    StateData stateData(0.0, exciter.getStates().data(), stateDerivative.data());
    double root = 0.0;
    exciter.setRootOffset(0, cLocalSolverMode);
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, 1e-5);

    exciters::ExciterIEEEtype1 strictExciter;
    strictExciter.set("ka", 10.0);
    strictExciter.set("ta", 0.1);
    strictExciter.set("te", 1.0);
    strictExciter.set("kf", 0.0);
    strictExciter.set("tf", 1.0);
    strictExciter.set("ke", 1.0);
    strictExciter.set("vrmax", 1.0);
    strictExciter.set("vrmin", -5.0);
    strictExciter.dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);

    EXPECT_THROW(strictExciter.dynInitializeB(inputs, {2.0}, fieldSet), InvalidParameterValue);
}

TEST(ExciterModelTests, Ieeet1InitialLimitUsesOverdriveInsteadOfExpandedHeadroom)
{
    class InspectableIEEEtype1 final: public exciters::ExciterIEEEtype1 {
      public:
        [[nodiscard]] double rawUpperLimit() const { return Vrmax; }
    };
    InspectableIEEEtype1 exciter;
    exciter.set("ka", 400.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 0.9999);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);

    EXPECT_NEAR(exciter.getStates()[1], 1.0, 1e-12);
    EXPECT_NEAR(exciter.rawUpperLimit(), 1.0, 1e-12);
}

TEST(ExciterModelTests, Ieeet1SaturatedInitialStatePrototypeHasZeroResidualAndStableRoot)
{
    class SaturatedIEEEtype1 final: public exciters::ExciterIEEEtype1 {
      public:
        double activateInitialUpperLimit(double importedLimit)
        {
            const double overdrive = 0.2 * (m_state[1] - importedLimit);
            Vrmax = m_state[1];
            vBias += overdrive / Ka;
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            return overdrive;
        }
        [[nodiscard]] double upperLimit() const { return Vrmax; }
    };

    SaturatedIEEEtype1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("ka", 400.0);
    exciter.set("ta", 0.0354);
    exciter.set("ke", 0.0);
    exciter.set("te", 0.6134);
    exciter.set("kf", 0.1147);
    exciter.set("tf", 0.35);
    exciter.set("e1", 1.7204);
    exciter.set("se1", 0.502);
    exciter.set("e2", 2.2939);
    exciter.set("se2", 0.8115);
    exciter.set("vrmax", 1.0);
    exciter.set("vrmin", -1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    // SEARSMONT 1 Gen 2 exciter parameters and its required initial field.
    exciter.dynInitializeB(inputs, {2.4767083902046343}, fieldSet);
    const auto initialField = exciter.getStates()[0];
    const auto initialRegulator = exciter.getStates()[1];
    EXPECT_NEAR(initialRegulator, 2.2591369715940024, 1e-9);
    const double overdrive = exciter.activateInitialUpperLimit(1.0);
    exciter.setRootOffset(0, cLocalSolverMode);

    EXPECT_NEAR(exciter.upperLimit(), initialRegulator, 1e-12);
    EXPECT_NEAR(overdrive, 0.2 * (initialRegulator - 1.0), 1e-12);
    const auto state = exciter.getStates();
    EXPECT_DOUBLE_EQ(state[0], initialField);
    EXPECT_DOUBLE_EQ(state[1], initialRegulator);
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    std::vector<double> derivative(state.size(), 0.0);
    std::vector<double> residual(state.size(), 0.0);
    exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(derivative[index], 0.0, 1e-12) << index;
        EXPECT_NEAR(residual[index], 0.0, 1e-12) << index;
    }

    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, overdrive / 400.0);
    EXPECT_EQ(exciter.rootCheck(inputs, stateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::NO_CHANGE);
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
        exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
        for (std::size_t index = 0; index < state.size(); ++index) {
            EXPECT_TRUE(std::isfinite(derivative[index])) << index;
            EXPECT_NEAR(residual[index], derivative[index], 1e-12) << index;
        }
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
        EXPECT_EQ(
            exciter.rootCheck(inputs, stateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
            ChangeCode::NO_CHANGE)
            << voltage;
    }
    inputs[exciterVoltageInLocation] = 1.0;
    expectExciterDaeJacobian(exciter, inputs, state, 1.0);
}

TEST(ExciterModelTests, Ieeet1ActiveLimiterHasConsistentDaeJacobian)
{
    class InspectableIEEEtype1 final: public exciters::ExciterIEEEtype1 {
      public:
        [[nodiscard]] double heldBranchReleaseRoot(const std::vector<double>& state,
                                                   double voltage) const
        {
            return state[2] - ((state[0] * Kf) / Tf) + (Vref + vBias - voltage) - (state[1] / Ka) +
                ((0.001 * state[1]) / (Ka * Ta));
        }
    };

    InspectableIEEEtype1 exciter;
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("kf", 0.0);
    exciter.set("tf", 1.0);
    exciter.set("ke", 1.0);
    // Start inside the bound so tightening it exercises limit entry.
    exciter.set("vrmax", 2.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);

    exciter.set("vrmax", 0.5);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 0.5);

    exciter.setRootOffset(0, cLocalSolverMode);
    const auto state = exciter.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    const StateData stateData(0.0, state.data(), stateDerivative.data());
    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_NEAR(root,
                exciter.heldBranchReleaseRoot(state, inputs[exciterVoltageInLocation]) + 1e-5,
                1e-12);

    expectExciterDaeJacobian(exciter, inputs, exciter.getStates(), 1.0);
}

TEST(ExciterModelTests, Ieeet1InitializedUpperLimitIsQuietUnderSmallVoltageNoise)
{
    exciters::ExciterIEEEtype1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("kf", 0.0);
    exciter.set("tf", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 1.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    const auto state = exciter.getStates();
    ASSERT_EQ(state.size(), 3U);
    EXPECT_DOUBLE_EQ(state[1], 1.0);
    std::vector<double> derivative(state.size(), 0.0);
    std::vector<double> residual(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(derivative[index], 0.0, 1e-12) << index;
        EXPECT_NEAR(residual[index], 0.0, 1e-12) << index;
    }

    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, 1e-3);

    // A 10x-tolerance terminal-voltage perturbation should produce a finite
    // controller response without turning the already initialized limit into
    // a root event.  Check both sides of the operating point.
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
        exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        for (std::size_t index = 0; index < state.size(); ++index) {
            EXPECT_TRUE(std::isfinite(derivative[index])) << index;
            EXPECT_NEAR(residual[index], derivative[index], 1e-12) << index;
        }
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0);
    }

    auto lowerState = state;
    lowerState[1] = -5.0;
    StateData lowerStateData(0.0, lowerState.data(), stateDerivative.data());
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.rootTest(inputs, lowerStateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0);
    }

    inputs[exciterVoltageInLocation] = 1.0;
    expectExciterDaeJacobian(exciter, inputs, state, 1.0);
}

TEST(ExciterModelTests, Ieeet1NearLimitContactHoldsAndReinitializationClearsIt)
{
    exciters::ExciterIEEEtype1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("kf", 0.0);
    exciter.set("tf", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 1.0 + 7e-8);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    std::vector<double> derivative(exciter.getStates().size(), 0.0);
    std::vector<double> residual(derivative.size(), 0.0);
    const auto state = exciter.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    const StateData stateData(0.0, state.data(), stateDerivative.data());
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0, 1e-12);
    EXPECT_NEAR(residual[1], 0.0, 1e-12);
    double root = 0.0;
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
        EXPECT_EQ(exciter.rootCheck(
                      inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
                  ChangeCode::NO_CHANGE);
    }

    // A second initialization at an interior point must not inherit the held branch.
    inputs[exciterVoltageInLocation] = 1.0;
    exciter.set("vrmax", 2.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    inputs[exciterVoltageInLocation] = 0.999;
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_GT(derivative[1], 0.0);
}

TEST(ExciterModelTests, InitializedLimitRootsStayClearAcrossExciterFamilies)
{
    const std::vector<std::string_view> models{"ac7b",   "ac8b",   "dc1a",   "dc2a",   "esac1a",
                                               "esac5a", "esac6a", "esst1a", "esst2a", "esst3a",
                                               "esst4b", "exac1",  "exac2",  "exac4",  "expic1",
                                               "exst1",  "ieeet1", "ieeet2", "ieeet3", "ieeex1",
                                               "scrx",   "sexs"};
    const auto factory = CoreObjectFactory::instance();
    for (const auto modelName : models) {
        SCOPED_TRACE(modelName);
        std::unique_ptr<CoreObject> object(factory->createObject("exciter", modelName));
        ASSERT_NE(object, nullptr);
        auto* exciter = dynamic_cast<Exciter*>(object.get());
        ASSERT_NE(exciter, nullptr);
        expectExciterRootMarginsStableUnderInputNoise(*exciter, modelName);
    }
}

TEST(ExciterModelTests, Ieeet2AdjustsInitialRegulatorUpperLimitAndHonorsStrictMode)
{
    exciters::ExciterIEEEtype2 exciter;
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("tf", 1.0);
    exciter.set("tf2", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 1.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {2.0}, fieldSet);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 2.0);
    exciter.setOffset(0, cDaeSolverMode);
    std::vector<double> stateDerivative(exciter.getStates().size(), 0.0);
    stateDerivative[1] = 0.25;
    StateData stateData(0.0, exciter.getStates().data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(stateDerivative.size());
    std::vector<double> residual(stateDerivative.size(), 0.0);
    exciter.residual(inputs, stateData, residual.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(residual[1], -stateDerivative[1]);
    expectExciterDaeJacobian(exciter, inputs, exciter.getStates(), 1.5);
    double root = 0.0;
    exciter.setRootOffset(0, cLocalSolverMode);
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, 0.1);

    // Reinitialize from an interior point before checking a new limit entry.
    exciter.set("vrmax", 3.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.dynInitializeB(inputs, {2.0}, fieldSet);
    exciter.set("vrmax", 0.5);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 0.5);
    expectExciterDaeJacobian(exciter, inputs, exciter.getStates(), 1.0);

    exciters::ExciterIEEEtype2 strictExciter;
    strictExciter.set("ka", 10.0);
    strictExciter.set("ta", 0.1);
    strictExciter.set("te", 1.0);
    strictExciter.set("tf", 1.0);
    strictExciter.set("tf2", 1.0);
    strictExciter.set("ke", 1.0);
    strictExciter.set("vrmax", 1.0);
    strictExciter.set("vrmin", -5.0);
    strictExciter.dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);
    EXPECT_THROW(strictExciter.dynInitializeB(inputs, {2.0}, fieldSet), InvalidParameterValue);
}

TEST(ExciterModelTests, Ieeet2NearLimitContactHoldsUnderVoltageNoise)
{
    exciters::ExciterIEEEtype2 exciter;
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("tf1", 1.0);
    exciter.set("tf2", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("aex", 0.0);
    exciter.set("vrmax", 1.0 + 7e-8);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    std::vector<double> derivative(exciter.getStates().size(), 0.0);
    std::vector<double> residual(derivative.size(), 0.0);
    const auto state = exciter.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0, 1e-12);
    EXPECT_NEAR(residual[1], 0.0, 1e-12);
    double root = 0.0;
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
    }
}

void configureIeeex1(exciters::ExciterIEEEX1& exciter,
                     double leadLagDenominatorTime,
                     double leadLagNumeratorTime)
{
    exciter.set("tr", 0.1);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.2);
    exciter.set("tb", leadLagDenominatorTime);
    exciter.set("tc", leadLagNumeratorTime);
    exciter.set("vrmax", 2.0);
    exciter.set("vrmin", -2.0);
    exciter.set("ke", 1.0);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.1);
    exciter.set("tf", 1.0);
    exciter.set("e1", 1.0);
    exciter.set("se1", 0.1);
    exciter.set("e2", 2.0);
    exciter.set("se2", 0.4);
}

TEST(ExciterModelTests, Ieeex1MatchesReferenceBlockEquations)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "ieeex1"));
    auto* exciter = dynamic_cast<exciters::ExciterIEEEX1*>(object.get());
    ASSERT_NE(exciter, nullptr);
    // ANDES LeadLag(zero_out=true) bypasses when TB <= 0, even if TC is
    // nonzero. This is the common direct path in the NPCC PSS/E records.
    configureIeeex1(*exciter, 0.0, 0.7);
    exciter->dynInitializeA(0.0, 0);
    EXPECT_EQ(exciter->solverStateNames(), (stringVec{"ef", "vr", "rf", "vmeas"}));

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter->dynInitializeB(inputs, {0.4}, fieldSet);
    ASSERT_EQ(exciter->getStates().size(), 4U);
    EXPECT_NEAR(exciter->getStates()[0], 0.4, 1e-14);
    EXPECT_NEAR(exciter->getStates()[1], 0.4, 1e-14);
    EXPECT_NEAR(exciter->getStates()[2], 0.04, 1e-14);
    EXPECT_NEAR(exciter->getStates()[3], 1.0, 1e-14);

    std::vector<double> state{0.7, 1.0, 0.05, 0.98};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter->setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter->derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);

    // Independent evaluation of ExcQuadSat using the two absolute feedback
    // points E1*SE1=0.1 and E2*SE2=0.8.
    const double ratio = std::sqrt(0.1 / 0.8);
    const double saturationThreshold = (1.0 - (ratio * 2.0)) / (1.0 - ratio);
    const double saturationGain = 0.8 / std::pow(2.0 - saturationThreshold, 2);
    const double saturationFeedback = saturationGain * std::pow(0.7 - saturationThreshold, 2);
    EXPECT_NEAR(derivative[0], (1.0 - 0.7 - saturationFeedback) / 0.5, 1e-12);
    EXPECT_NEAR(derivative[1], -3.0, 1e-12);
    EXPECT_NEAR(derivative[2], 0.02, 1e-12);
    EXPECT_NEAR(derivative[3], 0.2, 1e-12);

    exciters::ExciterIEEEX1 leadLagExciter;
    configureIeeex1(leadLagExciter, 0.2, 0.05);
    leadLagExciter.dynInitializeA(0.0, 0);
    EXPECT_EQ(leadLagExciter.solverStateNames(), (stringVec{"ef", "vr", "x", "rf", "vmeas"}));
    leadLagExciter.dynInitializeB(inputs, {0.4}, fieldSet);
    state = {0.7, 1.0, 0.03, 0.05, 0.98};
    stateDerivative.assign(state.size(), 0.0);
    leadLagExciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    derivative.assign(state.size(), 0.0);
    leadLagExciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[0], (1.0 - 0.7 - saturationFeedback) / 0.5, 1e-12);
    EXPECT_NEAR(derivative[1], -3.375, 1e-12);
    EXPECT_NEAR(derivative[2], 0.05, 1e-12);
    EXPECT_NEAR(derivative[3], 0.02, 1e-12);
    EXPECT_NEAR(derivative[4], 0.2, 1e-12);

    StateData jacobianState(0.0, state.data(), stateDerivative.data());
    jacobianState.cj = 1.0;
    MatrixDataSparse<double> leadLagJacobian;
    IOlocs leadLagInputLocs(exciterInputCount, kNullLocation);
    leadLagInputLocs[exciterVoltageInLocation] = 10;
    leadLagExciter.jacobianElements(
        inputs, jacobianState, leadLagJacobian, leadLagInputLocs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(1, 0), -1.25);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(1, 1), -6.0);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(1, 2), 37.5);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(1, 3), 12.5);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(1, 4), -12.5);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(2, 0), -0.5);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(2, 2), -6.0);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(2, 3), 5.0);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(2, 4), -5.0);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(3, 0), 0.1);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(3, 3), -2.0);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(4, 4), -11.0);
    EXPECT_DOUBLE_EQ(leadLagJacobian.at(4, 10), 10.0);
}

TEST(ExciterModelTests, Ieeex1UsesTerminalVoltageLimitsAndConsistentHoldJacobian)
{
    exciters::ExciterIEEEX1 exciter;
    configureIeeex1(exciter, 0.0, 0.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.setRootOffset(0, cLocalSolverMode);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);

    // The sensed-voltage state is 0.8, but the IEEEX1 limiter equation uses
    // instantaneous VT=0.5, giving bounds +/-1.0 rather than +/-1.6.
    inputs[exciterVoltageInLocation] = 0.5;
    std::vector<double> state{0.4, 1.1, 0.04, 0.8};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 1.0);

    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    MatrixDataSparse<double> jacobian;
    IOlocs inputLocs(exciterInputCount, kNullLocation);
    inputLocs[exciterVoltageInLocation] = 10;
    exciter.jacobianElements(inputs, emptyStateData, jacobian, inputLocs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 1), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 10), 0.0);

    // A negative unconstrained regulator drive releases the upper limit.
    state = {0.4, 1.0, 0.04, 1.2};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);

    // Exercise the symmetric lower-bound entry and inward release.
    state = {0.4, -1.1, 0.04, 1.2};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], -1.0);
    state = {0.4, -1.0, 0.04, 0.8};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_GT(derivative[1], 0.0);
}

TEST(ExciterModelTests, Ieeex1RejectsSingularParameters)
{
    for (const auto& [parameter, value] :
         std::vector<std::pair<std::string, double>>{{"ka", 0.0},
                                                     {"ta", 0.0},
                                                     {"te", 0.0},
                                                     {"tf", 0.0},
                                                     {"tr", -0.1},
                                                     {"tb", -0.1},
                                                     {"tc", -0.1}}) {
        exciters::ExciterIEEEX1 invalid;
        configureIeeex1(invalid, 0.0, 0.0);
        invalid.set(parameter, value);
        EXPECT_THROW(invalid.dynInitializeA(0.0, 0), InvalidParameterValue) << parameter;
    }
}

TEST(ExciterModelTests, Esst1aMatchesStaticExciterBlockEquations)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "esst1a"));
    auto* exciter = dynamic_cast<exciters::ExciterESST1A*>(object.get());
    ASSERT_NE(exciter, nullptr);
    exciter->set("tr", 0.1);
    exciter->set("vimax", 0.3);
    exciter->set("vimin", -0.3);
    exciter->set("tb", 0.2);
    exciter->set("tc", 0.05);
    exciter->set("tb1", 0.1);
    exciter->set("tc1", 0.02);
    exciter->set("ka", 20.0);
    exciter->set("ta", 0.25);
    exciter->set("vamax", 2.0);
    exciter->set("vamin", -2.0);
    exciter->set("ilr", 0.3);
    exciter->set("klr", 2.0);
    exciter->set("vrmax", 5.0);
    exciter->set("vrmin", -5.0);
    exciter->set("kf", 0.1);
    exciter->set("tf", 0.5);
    exciter->set("kc", 0.2);
    exciter->set("uel", 1.0);
    exciter->set("vos", 1.0);
    exciter->dynInitializeA(0.0, 0);
    EXPECT_EQ(exciter->solverStateNames(), (stringVec{"efd", "vmeas", "ll1", "ll2", "va", "rf"}));

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 0.2;
    IOdata fieldSet(4, 0.0);
    exciter->dynInitializeB(inputs, {0.4}, fieldSet);
    ASSERT_EQ(exciter->getStates().size(), 6U);
    EXPECT_NEAR(exciter->getStates()[0], 0.4, 1e-13);
    EXPECT_NEAR(exciter->getStates()[1], 1.0, 1e-13);
    EXPECT_NEAR(exciter->getStates()[2], 0.02, 1e-13);
    EXPECT_NEAR(exciter->getStates()[3], 0.02, 1e-13);
    EXPECT_NEAR(exciter->getStates()[4], 0.4, 1e-13);
    EXPECT_NEAR(exciter->getStates()[5], 0.08, 1e-13);

    inputs[exciterXadIfdInLocation] = 0.5;
    inputs[exciterVssInLocation] = 0.1;
    std::vector<double> state{0.4, 0.98, 0.05, 0.03, 0.7, 0.1};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter->setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter->derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);

    // Independent evaluation of the documented ESST1A blocks. The current
    // limiter is 2*(0.5-0.3)=0.4 and V_U=0.7-0.4=0.3.
    EXPECT_NEAR(derivative[1], 0.2, 1e-13);
    EXPECT_NEAR(derivative[2], 0.65, 1e-13);
    EXPECT_NEAR(derivative[3], 0.525, 1e-13);
    EXPECT_NEAR(derivative[4], 0.44, 1e-13);
    EXPECT_NEAR(derivative[5], -0.08, 1e-13);

    inputs[exciterVsetInLocation] = 1.1;
    exciter->derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[4], 0.84, 1e-13);

    std::unique_ptr<CoreObject> cloneObject(exciter->clone());
    auto* clone = dynamic_cast<exciters::ExciterESST1A*>(cloneObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("tb1"), 0.1);
    EXPECT_DOUBLE_EQ(clone->get("vos"), 1.0);
}

TEST(ExciterModelTests, Esst1aSupportsBypassesAndRejectsUnavailableUelRoutes)
{
    exciters::ExciterESST1A bypass;
    bypass.set("tr", 0.0);
    bypass.set("tb", 0.0);
    bypass.set("tc", 0.5);
    bypass.set("tb1", 0.0);
    bypass.set("tc1", 0.3);
    EXPECT_NO_THROW(bypass.dynInitializeA(0.0, 0));

    exciters::ExciterESST1A unavailableUel;
    unavailableUel.set("uel", 2.0);
    EXPECT_THROW(unavailableUel.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(ExciterModelTests, Esst1aAmplifierAntiWindupEntersAndReleasesLimits)
{
    exciters::ExciterESST1A exciter;
    exciter.set("vamax", 1.0);
    exciter.set("vamin", -1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);

    std::vector<double> state{0.4, 1.0, 0.0, 0.0, 1.1, 0.033};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVsetInLocation] = 1.1;
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(exciter.getStates()[4], 1.0);

    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[4], 0.0);

    inputs[exciterVsetInLocation] = 0.9;
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[4], 0.0);
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
        EXPECT_EQ(exciter->solverStateNames().back(), "vmeas");
    }
}

TEST(ExciterModelTests, DcExciterDefaultsUsePsseTwoPointSaturation)
{
    exciters::ExciterDC1A dc1a;
    verifyDefaultPsseSaturation(dc1a, {{{2.3, 0.1}, {3.1, 0.33}}});

    exciters::ExciterDC2A dc2a;
    verifyDefaultPsseSaturation(dc2a, {{{2.29, 0.117}, {3.05, 0.279}}});
}

TEST(ExciterModelTests, Dc2aInitialRegulatorLimitUsesDefaultAndStrictPolicies)
{
    exciters::ExciterDC2A exciter;
    exciter.set("tr", 0.0);
    exciter.set("vrmax", 0.05);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {2.0}, fieldSet);
    EXPECT_GT(exciter.getStates()[1], 0.05);

    exciters::ExciterDC2A strictExciter;
    strictExciter.set("tr", 0.0);
    strictExciter.set("vrmax", 0.05);
    strictExciter.set("vrmin", -5.0);
    strictExciter.dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);
    EXPECT_THROW(strictExciter.dynInitializeB(inputs, {2.0}, fieldSet), InvalidParameterValue);
}

TEST(ExciterModelTests, Dc2aInitialLimitUsesOverdriveInsteadOfExpandedHeadroom)
{
    class InspectableDC2A final: public exciters::ExciterDC2A {
      public:
        [[nodiscard]] double rawUpperLimit() const { return Vrmax; }
    };
    InspectableDC2A exciter;
    exciter.set("ka", 50.0);
    exciter.set("tb", 0.11);
    exciter.set("tc", 0.5);
    exciter.set("vrmax", 0.9999);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);

    EXPECT_NEAR(exciter.getStates()[1], 1.0, 1e-12);
    EXPECT_NEAR(exciter.rawUpperLimit(), 1.0, 1e-12);
}

TEST(ExciterModelTests, Dc2aSaturatedInitialStatePrototypeHasZeroResidualAndStableRoot)
{
    class SaturatedDC2A final: public exciters::ExciterDC2A {
      public:
        double activateInitialUpperLimit(double importedLimit, double voltage)
        {
            const double requiredLimit = m_state[1] / voltage;
            const double overdrive = 0.2 * (requiredLimit - importedLimit) * voltage;
            Vrmax = requiredLimit;
            // The lead-lag state and its steady input must move together.
            m_state[2] += overdrive / Ka;
            vBias += overdrive / Ka;
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            return overdrive;
        }
        [[nodiscard]] double upperLimit() const { return Vrmax; }
    };

    SaturatedDC2A exciter;
    exciter.set("tr", 0.02);
    exciter.set("ka", 50.0);
    exciter.set("ta", 0.033333);
    exciter.set("tb", 0.11);
    exciter.set("tc", 0.5);
    exciter.set("ke", 1.0);
    exciter.set("te", 1.0);
    exciter.set("kf", 0.0);
    exciter.set("tf", 0.15);
    exciter.set("e1", 1.33);
    exciter.set("se1", 0.2122);
    exciter.set("e2", 1.7733);
    exciter.set("se2", 0.849);
    exciter.set("vrmax", 1.17);
    exciter.set("vrmin", -1.17);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    constexpr double initialVoltage = 1.03833485;
    inputs[exciterVoltageInLocation] = initialVoltage;
    IOdata fieldSet(4, 0.0);
    // DEXTER 3 2 exciter parameters and its required initial field.
    exciter.dynInitializeB(inputs, {2.4717080174719954}, fieldSet);
    const auto initialField = exciter.getStates()[0];
    const auto initialRegulator = exciter.getStates()[1];
    EXPECT_NEAR(initialRegulator / initialVoltage, 7.578180551303769, 1e-9);
    const double overdrive = exciter.activateInitialUpperLimit(1.17, initialVoltage);
    exciter.setRootOffset(0, cLocalSolverMode);

    EXPECT_NEAR(exciter.upperLimit() * initialVoltage, initialRegulator, 1e-12);
    EXPECT_NEAR(overdrive, 0.2 * (initialRegulator - (1.17 * initialVoltage)), 1e-12);
    const auto state = exciter.getStates();
    EXPECT_DOUBLE_EQ(state[0], initialField);
    EXPECT_DOUBLE_EQ(state[1], initialRegulator);
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    std::vector<double> derivative(state.size(), 0.0);
    std::vector<double> residual(state.size(), 0.0);
    exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(derivative[index], 0.0, 1e-12) << index;
        EXPECT_NEAR(residual[index], 0.0, 1e-12) << index;
    }

    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    // The production initializer already supplies the first overdrive;
    // activateInitialUpperLimit adds the same amount a second time here.
    EXPECT_NEAR(root, (2.0 * overdrive) + 1e-4, 1e-12);
    EXPECT_EQ(exciter.rootCheck(inputs, stateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::NO_CHANGE);
    for (const double voltage : {initialVoltage - 1e-5, initialVoltage + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
        exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
        for (std::size_t index = 0; index < state.size(); ++index) {
            EXPECT_TRUE(std::isfinite(derivative[index])) << index;
            EXPECT_NEAR(residual[index], derivative[index], 1e-12) << index;
        }
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
        EXPECT_EQ(
            exciter.rootCheck(inputs, stateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
            ChangeCode::NO_CHANGE)
            << voltage;
    }
    inputs[exciterVoltageInLocation] = initialVoltage;
    expectExciterDaeJacobian(exciter, inputs, state, 1.0);
}

TEST(ExciterModelTests, Dc2aInitializedUpperLimitIsQuietUnderSmallVoltageNoise)
{
    class InspectableDC2A final: public exciters::ExciterDC2A {
      public:
        [[nodiscard]] double rawUpperLimit() const { return Vrmax; }
        [[nodiscard]] double heldBranchDrive(const std::vector<double>& state, double voltage) const
        {
            return ((((Vref + vBias - voltage) - ((state[0] * Kf) / Tf)) + state[3]) * Ka * Tc /
                    Tb) +
                ((state[2] * (Tb - Tc) * Ka) / Tb) - state[1];
        }
    };

    InspectableDC2A exciter;
    exciter.set("tr", 0.0);
    exciter.set("vrmax", 0.05);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {2.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    const auto state = exciter.getStates();
    ASSERT_EQ(state.size(), 4U);
    EXPECT_GT(state[1], 0.05);
    EXPECT_NEAR(exciter.rawUpperLimit(), state[1], 1e-12);
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    std::vector<double> derivative(state.size(), 0.0);
    std::vector<double> residual(state.size(), 0.0);
    exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(derivative[index], 0.0, 1e-12) << index;
        EXPECT_NEAR(residual[index], 0.0, 1e-12) << index;
    }

    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_NEAR(root, exciter.heldBranchDrive(state, 1.0) + 1e-4, 1e-12);

    // Exercise both directions at ten times the normal relative tolerance.
    // The initial regulator output is held at its corrected voltage-dependent limit.
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
        for (std::size_t index = 0; index < derivative.size(); ++index) {
            EXPECT_TRUE(std::isfinite(derivative[index])) << index;
        }
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
    }

    // The same noise must also leave the lower-bound root on its original side.
    auto lowerState = state;
    lowerState[1] = -5.0;
    const StateData lowerStateData(0.0, lowerState.data(), stateDerivative.data());
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.rootTest(inputs, lowerStateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
    }

    // The initialized high limit remains held without an extra branch change.
    inputs[exciterVoltageInLocation] = 1.0;
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::NO_CHANGE);
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, 0.0);
}

TEST(ExciterModelTests, Dc2aNearLimitContactHoldsUnderVoltageNoise)
{
    exciters::ExciterDC2A exciter;
    exciter.set("tr", 0.0);
    exciter.set("vrmax", 1.0 + 7e-8);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    std::vector<double> derivative(exciter.getStates().size(), 0.0);
    std::vector<double> residual(derivative.size(), 0.0);
    const auto state = exciter.getStates();
    std::vector<double> stateDerivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), stateDerivative.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    exciter.derivative(inputs, stateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, stateData, residual.data(), cLocalSolverMode);
    EXPECT_LE(std::abs(derivative[0]), 1e-7);
    EXPECT_NEAR(derivative[1], 0.0, 1e-12);
    EXPECT_NEAR(residual[1], 0.0, 1e-12);
    double root = 0.0;
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0) << voltage;
        EXPECT_EQ(exciter.rootCheck(
                      inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
                  ChangeCode::NO_CHANGE);
    }
}

TEST(ExciterModelTests, Dc2aActiveLimiterHasConsistentDaeJacobian)
{
    exciters::ExciterDC2A exciter;
    exciter.set("tr", 0.0);
    // Start inside the bound so tightening it exercises limit entry.
    exciter.set("vrmax", 2.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);

    // Tighten the configured bound after initialization to put the already
    // initialized regulator into the active upper-limit branch.
    exciter.set("vrmax", 0.5);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    ASSERT_EQ(exciter.getStates().size(), 4U);
    EXPECT_DOUBLE_EQ(exciter.getStates()[1], 0.5);

    expectExciterDaeJacobian(exciter, inputs, exciter.getStates(), 1.0);
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
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "ll", "va", "ve", "wf"}));
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

TEST(ExciterModelTests, Exac1ZeroTbBypassesLeadLagState)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.0);
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

    EXPECT_EQ(exciter.getStates().size(), 4U);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "va", "ve", "wf"}));
    EXPECT_EQ(exciter.findIndex("ll", cLocalSolverMode), kInvalidLocation);

    std::vector<double> state{0.4, 0.3, 1.1, 1.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.05;
    inputs[exciterVsetInLocation] = 1.02;
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    for (const auto value : derivative) {
        EXPECT_TRUE(std::isfinite(value));
    }
}

TEST(ExciterModelTests, Exac1ZeroTaUsesAlgebraicRegulatorState)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.0);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.0);
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

    EXPECT_EQ(exciter.getStates().size(), 4U);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "va", "ve", "wf"}));
    EXPECT_EQ(exciter.findIndex("va", cLocalSolverMode), 1U);
    EXPECT_EQ(exciter.findIndex("ve", cLocalSolverMode), 2U);

    const std::vector<double> state{0.4, 0.4, 0.3, 0.2};
    expectExciterJacobian(exciter, inputs, state);
}

TEST(ExciterModelTests, Exac1LimiterRootHasBufferAtInitializedLimit)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 0.5);
    exciter.set("kf", 0.0);
    exciter.set("tf", 1.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 1.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    std::vector<double> stateDerivative(exciter.getStates().size(), 0.0);
    StateData stateData(0.0, exciter.getStates().data(), stateDerivative.data());
    double root = 0.0;
    exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
    EXPECT_GT(root, 1e-3);

    const auto state = exciter.getStates();
    std::vector<double> derivative(state.size(), 0.0);
    std::vector<double> residual(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(derivative[index], 0.0, 1e-12) << index;
        EXPECT_NEAR(residual[index], 0.0, 1e-12) << index;
    }

    // Exercise a small signal on either side while the regulator is held at
    // its initialized upper limit. The held branch should remain stable and
    // the release root should stay away from zero until its actual threshold.
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
        exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        for (std::size_t index = 0; index < state.size(); ++index) {
            EXPECT_TRUE(std::isfinite(derivative[index])) << index;
            EXPECT_NEAR(residual[index], derivative[index], 1e-12) << index;
        }
        exciter.rootTest(inputs, stateData, &root, cLocalSolverMode);
        EXPECT_GT(root, 0.0);
    }

    // At the exact bound the constrained and unconstrained one-sided
    // derivatives differ. Check the Jacobian after a clear outward drive has
    // selected the held branch, while staying away from its release surface.
    inputs[exciterVoltageInLocation] = 0.999;
    expectExciterDaeJacobian(exciter, inputs, state, 1.0);
    inputs[exciterVoltageInLocation] = 1.0;
}

TEST(ExciterModelTests, Exac1NearLimitContactHoldsAndReinitializationClearsIt)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.0);
    exciter.set("ka", 10.0);
    exciter.set("ta", 0.1);
    exciter.set("te", 1.0);
    exciter.set("kf", 0.0);
    exciter.set("tf", 1.0);
    exciter.set("ke", 1.0);
    exciter.set("vrmax", 1.0 + 7e-8);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    exciter.setRootOffset(0, cLocalSolverMode);

    std::vector<double> derivative(exciter.getStates().size(), 0.0);
    std::vector<double> residual(derivative.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    exciter.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0, 1e-12);
    EXPECT_NEAR(residual[1], 0.0, 1e-12);
    double root = 0.0;
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
        exciter.rootTest(inputs, emptyStateData, &root, cLocalSolverMode);
        EXPECT_NEAR(derivative[1], 0.0, 1e-12) << voltage;
        EXPECT_GT(root, 0.0) << voltage;
        EXPECT_EQ(exciter.rootCheck(
                      inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
                  ChangeCode::NO_CHANGE);
    }

    inputs[exciterVoltageInLocation] = 1.0;
    exciter.set("vrmax", 2.0);
    exciter.dynInitializeA(0.0, 0);
    exciter.dynInitializeB(inputs, {1.0}, fieldSet);
    inputs[exciterVoltageInLocation] = 0.999;
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_GT(derivative[1], 0.0);
}

TEST(ExciterModelTests, Exac1ZeroTbRequiresZeroTc)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.05);
    EXPECT_THROW(exciter.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(ExciterModelTests, Exac1ZeroFirstSaturationUsesCutoffFit)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.0);
    exciter.set("te", 1.0);
    exciter.set("ke", 0.0);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("e1", 3.0);
    exciter.set("se1", 0.0);
    exciter.set("e2", 4.0);
    exciter.set("se2", 0.8);
    exciter.set("vrmax", 5.0);
    exciter.set("vrmin", -5.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.5}, fieldSet);

    std::vector<double> state{0.5, 0.0, 3.0, 0.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], 0.0, 1e-12);

    state[2] = 4.0;
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[2], -3.2, 1e-12);
}

TEST(ExciterModelTests, Exac1ZeroFirstSaturationRequiresOrderedPoints)
{
    exciters::ExciterEXAC1 exciter;
    exciter.set("e1", 4.0);
    exciter.set("se1", 0.0);
    exciter.set("e2", 3.0);
    exciter.set("se2", 0.1);
    EXPECT_THROW(exciter.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST(ExciterModelTests, Exac2InitialRegulatorUsesHighGateGain)
{
    exciters::ExciterEXAC2 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.0);
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
    exciter.set("vamax", 0.5);
    exciter.set("vamin", -0.5);
    exciter.set("vlr", 0.0);
    exciter.set("kl", 4.0);
    exciter.set("kh", 0.0);
    exciter.set("kb", 1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);

    const auto& initialized = exciter.getStates();
    ASSERT_EQ(initialized.size(), 4U);
    EXPECT_NEAR(initialized[1], 0.4, 1e-12);

    std::vector<double> derivative(initialized.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.0, 1e-12);
    EXPECT_NEAR(derivative[2], 0.0, 1e-12);
    EXPECT_NEAR(derivative[3], 0.0, 1e-12);
}

TEST(ExciterModelTests, Exac2LimiterRootsReleaseAndProjectState)
{
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);

    // Exercise release from the upper VAMAX limit. The release request must
    // exceed the root buffer by a clear margin before the limiter changes
    // branch.
    exciters::ExciterEXAC2 upperExciter;
    upperExciter.set("tr", 0.0);
    upperExciter.set("tb", 0.0);
    upperExciter.set("tc", 0.0);
    upperExciter.set("ka", 10.0);
    upperExciter.set("ta", 0.1);
    upperExciter.set("te", 0.5);
    upperExciter.set("kf", 0.0);
    upperExciter.set("tf", 1.0);
    upperExciter.set("kc", 0.0);
    upperExciter.set("kd", 0.0);
    upperExciter.set("ke", 1.0);
    upperExciter.set("vrmax", 5.0);
    upperExciter.set("vrmin", -5.0);
    upperExciter.set("vamax", 0.4);
    upperExciter.set("vamin", -0.4);
    upperExciter.set("vlr", 0.0);
    upperExciter.set("kl", 4.0);
    upperExciter.set("kh", 0.0);
    upperExciter.set("kb", 1.0);
    upperExciter.dynInitializeA(0.0, 0);
    upperExciter.dynInitializeB(inputs, {0.4}, fieldSet);
    upperExciter.setRootOffset(0, cLocalSolverMode);

    std::vector<double> roots(1, 0.0);
    // A root return can carry a stale release direction while the state/input
    // pair copied back to the model is still driving outward.  The limiter
    // must remain latched, and the same root must be rearmed only after time
    // advances.  This mirrors the interpolation mismatch seen by IDA in the
    // 10k-bus disturbance.
    inputs[exciterVsetInLocation] = 1.0 + 1e-6;
    upperExciter.rootTrigger(0.0, inputs, {-1}, cLocalSolverMode);
    inputs[exciterVsetInLocation] = 1.0 - 3e-4;
    upperExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    inputs[exciterVsetInLocation] = 1.0 + 1e-6;
    EXPECT_EQ(upperExciter.rootCheck(
                  inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::NO_CHANGE);
    inputs[exciterVsetInLocation] = 1.0 - 3e-4;
    upperExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    const StateData advancedState(1e-6);
    upperExciter.rootTest(inputs, advancedState, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);

    inputs[exciterVsetInLocation] = 1.0 - 3e-4;
    upperExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    upperExciter.rootTrigger(0.0, inputs, {-1}, cLocalSolverMode);
    EXPECT_NEAR(upperExciter.getStates()[1], 0.4 - 1e-7, 1e-12);
    upperExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    EXPECT_EQ(upperExciter.rootCheck(
                  inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::NO_CHANGE);

    std::vector<double> derivative(upperExciter.getStates().size(), 0.0);
    upperExciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[1], 0.0);

    // Exercise entry and release from the lower VAMIN limit.
    exciters::ExciterEXAC2 lowerExciter;
    lowerExciter.set("tr", 0.0);
    lowerExciter.set("tb", 0.0);
    lowerExciter.set("tc", 0.0);
    lowerExciter.set("ka", 10.0);
    lowerExciter.set("ta", 0.1);
    lowerExciter.set("te", 0.5);
    lowerExciter.set("kf", 0.0);
    lowerExciter.set("tf", 1.0);
    lowerExciter.set("kc", 0.0);
    lowerExciter.set("kd", 0.0);
    lowerExciter.set("ke", 1.0);
    lowerExciter.set("vrmax", 5.0);
    lowerExciter.set("vrmin", -5.0);
    lowerExciter.set("vamax", 0.5);
    lowerExciter.set("vamin", -0.4);
    lowerExciter.set("vlr", 0.0);
    lowerExciter.set("kl", 4.0);
    lowerExciter.set("kh", 0.0);
    lowerExciter.set("kb", 1.0);
    lowerExciter.dynInitializeA(0.0, 0);
    lowerExciter.dynInitializeB(inputs, {0.4}, fieldSet);
    lowerExciter.setRootOffset(0, cLocalSolverMode);

    auto lowerState = lowerExciter.getStates();
    lowerState[1] = -0.4 - 3e-4;
    std::vector<double> lowerDerivative(lowerState.size(), 0.0);
    lowerExciter.setState(0.0, lowerState.data(), lowerDerivative.data(), cLocalSolverMode);
    inputs[exciterVsetInLocation] = 0.91998;
    lowerExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    lowerExciter.rootTrigger(0.0, inputs, {-1}, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(lowerExciter.getStates()[1], -0.4);

    inputs[exciterVsetInLocation] = 0.92002;
    lowerExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    lowerExciter.rootTrigger(0.0, inputs, {1}, cLocalSolverMode);
    EXPECT_NEAR(lowerExciter.getStates()[1], -0.4 + 1e-7, 1e-12);
    lowerExciter.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[0], 0.0);
    EXPECT_EQ(lowerExciter.rootCheck(
                  inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
              ChangeCode::NO_CHANGE);
    lowerExciter.derivative(inputs, emptyStateData, lowerDerivative.data(), cLocalSolverMode);
    EXPECT_GT(lowerDerivative[1], 0.0);
}

TEST(ExciterModelTests, Exac2BypassedStatesHaveConsistentResidualAndJacobian)
{
    exciters::ExciterEXAC2 exciter;
    exciter.set("tr", 0.0);
    exciter.set("tb", 0.0);
    exciter.set("tc", 0.0);
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
    exciter.set("vamax", 5.0);
    exciter.set("vamin", -5.0);
    exciter.set("vlr", 0.0);
    exciter.set("kl", 4.0);
    exciter.set("kh", 1.0);
    exciter.set("kb", 1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterXadIfdInLocation] = 0.0;
    const std::vector<double> state{0.4, 0.4, 0.2, 0.1};

    expectExciterJacobian(exciter, inputs, state);
}

TEST(ExciterModelTests, Exac2ZeroTrBypassesVoltageMeasurementState)
{
    exciters::ExciterEXAC2 exciter;
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
    exciter.set("vamax", 5.0);
    exciter.set("vamin", 0.0);
    exciter.set("vlr", 0.0);
    exciter.set("kl", 1.0);
    exciter.set("kh", 1.0);
    exciter.set("kb", 1.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.4}, fieldSet);

    EXPECT_EQ(exciter.getStates().size(), 5U);
    EXPECT_EQ(exciter.solverStateNames(), (stringVec{"efd", "ll", "va", "ve", "wf"}));
    EXPECT_EQ(exciter.findIndex("vmeas", cLocalSolverMode), kInvalidLocation);

    // With TR=0, the compact state order is [ll, va, ve, wf].  The high
    // gate therefore uses va=0.2 and the feedback uses ve=1.1.
    std::vector<double> state{0.4, 0.1, 0.2, 1.1, 1.0};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[exciterVoltageInLocation] = 1.05;
    inputs[exciterVsetInLocation] = 1.02;
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[3], -4.0, 1e-12);
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

TEST(ExciterModelTests, Ac8bMatchesPidAndRotatingExciterEquations)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("exciter", "ac8b"));
    auto* exciter = dynamic_cast<exciters::ExciterAC8B*>(object.get());
    ASSERT_NE(exciter, nullptr);
    exciter->set("tr", 0.1);
    exciter->set("kpr", 2.0);
    exciter->set("kir", 0.5);
    exciter->set("kdr", 0.1);
    exciter->set("tdr", 0.2);
    exciter->set("vpmax", 10.0);
    exciter->set("vpmin", -10.0);
    exciter->set("vrmax", 10.0);
    exciter->set("vrmin", -10.0);
    exciter->set("vfemax", 10.0);
    exciter->set("vemin", -1.0);
    exciter->set("ka", 3.0);
    exciter->set("ta", 0.4);
    exciter->set("te", 0.5);
    exciter->set("kc", 0.0);
    exciter->set("kd", 0.0);
    exciter->set("ke", 1.0);
    exciter->set("e1", 0.0);
    exciter->set("se1", 0.0);
    exciter->set("e2", 1.0);
    exciter->set("se2", 0.0);
    exciter->dynInitializeA(0.0, 0);
    EXPECT_EQ(exciter->solverStateNames(),
              (stringVec{"efd", "vmeas", "pidint", "pidder", "vr", "ve"}));

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.02;
    inputs[exciterVssInLocation] = 0.01;
    IOdata initializationInputs = inputs;
    initializationInputs[exciterVoltageInLocation] = 1.03;
    IOdata fieldSet(4, 0.0);
    exciter->dynInitializeB(initializationInputs, {0.18}, fieldSet);
    std::vector<double> state{0.75, 0.95, 0.1, 0.8, 0.7, 0.8};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter->setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter->derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.5, 1e-12);
    EXPECT_NEAR(derivative[2], 0.04, 1e-12);
    EXPECT_NEAR(derivative[3], -3.6, 1e-12);
    EXPECT_NEAR(derivative[4], -2.5, 1e-12);
    EXPECT_NEAR(derivative[5], -0.2, 1e-12);

    std::vector<double> residual(state.size(), 0.0);
    exciter->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    EXPECT_NEAR(residual[0], 0.05, 1e-12);
}

TEST(ExciterModelTests, Ac7bMatchesNestedRegulatorEquations)
{
    exciters::ExciterAC7B exciter;
    exciter.set("tr", 0.1);
    exciter.set("kpr", 2.0);
    exciter.set("kir", 0.5);
    exciter.set("kdr", 0.0);
    exciter.set("tdr", 0.0);
    exciter.set("vrmax", 10.0);
    exciter.set("vrmin", -10.0);
    exciter.set("kpa", 3.0);
    exciter.set("kia", 0.5);
    exciter.set("vamax", 10.0);
    exciter.set("vamin", -10.0);
    exciter.set("kp", 2.0);
    exciter.set("kl", 10.0);
    exciter.set("te", 0.5);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("kf1", 0.2);
    exciter.set("kf2", 0.3);
    exciter.set("kf3", 0.0);
    exciter.set("tf3", 0.0);
    exciter.set("vfemax", 1.0);
    exciter.set("vemin", -10.0);
    exciter.set("e1", 0.0);
    exciter.set("se1", 0.0);
    exciter.set("e2", 1.0);
    exciter.set("se2", 0.0);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.02;
    inputs[exciterVssInLocation] = 0.01;
    IOdata initializationInputs = inputs;
    initializationInputs[exciterVoltageInLocation] = 1.03;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(initializationInputs, {0.8}, fieldSet);
    std::vector<double> state{0.8, 0.95, 0.1, 0.08, 0.2, 0.8, 0.8};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.5, 1e-12);
    EXPECT_NEAR(derivative[2], 0.04, 1e-12);
    EXPECT_NEAR(derivative[3], 0.0, 1e-12);
    EXPECT_NEAR(derivative[4], -0.07, 1e-12);
    EXPECT_NEAR(derivative[5], 0.0, 1e-12);
    EXPECT_NEAR(derivative[6], -2.48, 1e-12);

    // VEMIN/VFEMAX constrain the VE integrator state, not I_C.  A large
    // inner-PI output therefore remains present in the differential drive.
    state[4] = 2.0;
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[6], 4.72, 1e-12);

    // PSLF ESAC7B uses Kp=0 for the unity-source option.  It must not
    // propagate a zero potential-source multiplier into initialization or
    // the rotating-exciter control input.
    exciter.set("kp", 0.0);
    exciter.set("kf1", 0.0);
    exciter.set("kf2", 0.0);
    inputs[exciterVoltageInLocation] = 0.5;
    initializationInputs = inputs;
    ASSERT_NO_THROW(exciter.dynInitializeB(initializationInputs, {0.8}, fieldSet));
    state = {0.8, 0.5, 0.8, 0.0, 0.6, 0.8, 0.8};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[6], 4.4, 1e-12);
}

TEST(ExciterModelTests, Ac8bUsesScaledSaturationAndFullPidAntiWindup)
{
    exciters::ExciterAC8B exciter;
    exciter.set("tr", 0.1);
    exciter.set("kpr", 2.0);
    exciter.set("kir", 0.5);
    exciter.set("kdr", 0.0);
    exciter.set("tdr", 0.0);
    exciter.set("vpmax", 0.2);
    exciter.set("vpmin", -0.2);
    exciter.set("vrmax", 10.0);
    exciter.set("vrmin", -10.0);
    exciter.set("vfemax", 10.0);
    exciter.set("vemin", -10.0);
    exciter.set("ka", 1.0);
    exciter.set("ta", 0.4);
    exciter.set("te", 0.5);
    exciter.set("kc", 0.0);
    exciter.set("kd", 0.0);
    exciter.set("ke", 1.0);
    exciter.set("e1", 2.0);
    exciter.set("se1", 0.1);
    exciter.set("e2", 3.0);
    exciter.set("se2", 0.3);
    exciter.dynInitializeA(0.0, 0);

    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    exciter.dynInitializeB(inputs, {0.1}, fieldSet);

    // The nonlinear equation uses S_E(V_E), not E*S_E(V_E), inside the gain.
    std::vector<double> state{0.1, 1.0, 0.0, 0.0, 0.7, 2.5};
    std::vector<double> stateDerivative(state.size(), 0.0);
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    const double ratio = std::sqrt((0.1 * 2.0) / (0.3 * 3.0));
    const double cutoff = (2.0 - (ratio * 3.0)) / (1.0 - ratio);
    const double coefficient = 0.1 * 2.0 / std::pow(2.0 - cutoff, 2.0);
    const double saturation = coefficient * std::pow(2.5 - cutoff, 2.0) / 2.5;
    EXPECT_NEAR(derivative[5], (0.7 - ((1.0 + saturation) * 2.5)) / 0.5, 1e-12);

    // The integral state is inside +/-0.2, but the complete PID output is
    // saturated. The outward integral drive must be held.
    inputs[exciterVsetInLocation] = 2.0;
    state = {0.1, 1.0, 0.0, 0.0, 0.0, 0.1};
    exciter.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    inputs[exciterVsetInLocation] = 0.0;
    EXPECT_EQ(
        exciter.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    exciter.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[2], 0.0);
}

TEST(ExciterModelTests, Ieeet3AndEsst2aMatchStaticSourceEquations)
{
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.02;
    inputs[exciterVssInLocation] = 0.01;
    IOdata fieldSet(4, 0.0);

    exciters::ExciterIEEET3 ieeet3;
    ieeet3.set("tr", 0.1);
    ieeet3.set("ka", 5.0);
    ieeet3.set("ta", 0.2);
    ieeet3.set("vrmax", 10.0);
    ieeet3.set("vrmin", -10.0);
    ieeet3.set("vbmax", 10.0);
    ieeet3.set("ke", 1.0);
    ieeet3.set("te", 0.5);
    ieeet3.set("kf", 0.1);
    ieeet3.set("tf", 0.5);
    ieeet3.set("kp", 0.0);
    ieeet3.set("ki", 0.0);
    ieeet3.dynInitializeA(0.0, 0);
    ieeet3.dynInitializeB(inputs, {0.15}, fieldSet);
    std::vector<double> state{0.8, 0.95, 0.1, 0.7, 0.8};
    std::vector<double> stateDerivative(state.size(), 0.0);
    ieeet3.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    std::vector<double> derivative(state.size(), 0.0);
    ieeet3.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.5, 1e-12);
    EXPECT_NEAR(derivative[2], 1.0, 1e-12);
    EXPECT_NEAR(derivative[3], 0.2, 1e-12);
    EXPECT_NEAR(derivative[4], -1.4, 1e-12);

    exciters::ExciterESST2A esst2a;
    esst2a.set("tr", 0.1);
    esst2a.set("ka", 5.0);
    esst2a.set("ta", 0.2);
    esst2a.set("vrmax", 10.0);
    esst2a.set("vrmin", -10.0);
    esst2a.set("kp", 0.0);
    esst2a.set("ki", 0.0);
    esst2a.set("kf", 0.1);
    esst2a.set("tf", 0.5);
    esst2a.set("ke", 1.0);
    esst2a.set("te", 0.5);
    esst2a.set("efdmax", 10.0);
    esst2a.dynInitializeA(0.0, 0);
    esst2a.dynInitializeB(inputs, {0.15}, fieldSet);
    state = {0.8, 0.95, 0.8, 0.7, 0.8};
    esst2a.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    derivative.assign(state.size(), 0.0);
    esst2a.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_NEAR(derivative[1], 0.5, 1e-12);
    EXPECT_NEAR(derivative[2], -2.5, 1e-12);
    EXPECT_NEAR(derivative[3], 0.2, 1e-12);
    EXPECT_NEAR(derivative[4], 0.0, 1e-12);
}

TEST(ExciterModelTests, Ieeet3AndEsst2aProjectAndReleaseDynamicLimits)
{
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);
    std::vector<double> stateDerivative(5, 0.0);
    std::vector<double> derivative(5, 0.0);

    exciters::ExciterIEEET3 ieeet3;
    ieeet3.set("vrmax", 1.0);
    ieeet3.set("vrmin", -1.0);
    ieeet3.set("kp", 0.0);
    ieeet3.set("ki", 0.0);
    ieeet3.dynInitializeA(0.0, 0);
    ieeet3.setRootOffset(0, cLocalSolverMode);
    ieeet3.dynInitializeB(inputs, {0.1}, fieldSet);
    inputs[exciterVsetInLocation] = 2.0;
    std::vector<double> state{0.1, 1.0, 1.1, 0.1, 0.1};
    ieeet3.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        ieeet3.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(ieeet3.getStates()[2], 1.0);
    std::array<double, 1> ieeet3Roots{};
    ieeet3.rootTest(inputs, emptyStateData, ieeet3Roots.data(), cLocalSolverMode);
    const double ieeet3HeldRoot = ieeet3Roots[0];
    EXPECT_GT(std::abs(ieeet3HeldRoot), 1e-6);
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        ieeet3.rootTest(inputs, emptyStateData, ieeet3Roots.data(), cLocalSolverMode);
        EXPECT_GT(ieeet3Roots[0] * ieeet3HeldRoot, 0.0) << voltage;
        EXPECT_GT(std::abs(ieeet3Roots[0]), 1e-6) << voltage;
    }
    inputs[exciterVoltageInLocation] = 1.0;
    ieeet3.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    inputs[exciterVsetInLocation] = 0.0;
    EXPECT_EQ(
        ieeet3.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    ieeet3.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[2], 0.0);

    inputs[exciterVsetInLocation] = 1.0;
    exciters::ExciterESST2A esst2a;
    esst2a.set("vrmax", 10.0);
    esst2a.set("vrmin", -10.0);
    esst2a.set("kp", 0.0);
    esst2a.set("ki", 0.0);
    esst2a.set("efdmax", 1.0);
    esst2a.dynInitializeA(0.0, 0);
    esst2a.setRootOffset(0, cLocalSolverMode);
    esst2a.dynInitializeB(inputs, {0.1}, fieldSet);
    state = {1.1, 1.0, 2.0, 1.1, 1.1};
    esst2a.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        esst2a.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    EXPECT_DOUBLE_EQ(esst2a.getStates()[4], 1.0);
    std::array<double, 2> esst2aRoots{};
    esst2a.rootTest(inputs, emptyStateData, esst2aRoots.data(), cLocalSolverMode);
    const double esst2aHeldRoot = esst2aRoots[1];
    EXPECT_GT(std::abs(esst2aHeldRoot), 1e-6);
    for (const double voltage : {1.0 - 1e-5, 1.0 + 1e-5}) {
        inputs[exciterVoltageInLocation] = voltage;
        esst2a.rootTest(inputs, emptyStateData, esst2aRoots.data(), cLocalSolverMode);
        EXPECT_GT(esst2aRoots[1] * esst2aHeldRoot, 0.0) << voltage;
        EXPECT_GT(std::abs(esst2aRoots[1]), 1e-6) << voltage;
    }
    inputs[exciterVoltageInLocation] = 1.0;
    esst2a.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[4], 0.0);
    state = {1.0, 1.0, 0.0, 1.0, 1.0};
    esst2a.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    EXPECT_EQ(
        esst2a.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::REVERSABLE_ONLY),
        ChangeCode::JACOBIAN_CHANGE);
    esst2a.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_LT(derivative[4], 0.0);
}

TEST(Esst2aTests, CompoundSourceAllRectifierSegmentsAndBypassesHaveJacobians)
{
    for (const double normalizedCurrent : {-0.2, 0.2, 0.6, 0.85, 1.2}) {
        exciters::ExciterESST2A model;
        model.set("ka", 5.0);
        model.set("ta", 0.2);
        model.set("kp", 0.7);
        model.set("ki", 1.0);
        model.set("kc", 0.2);
        model.set("vrmax", 20.0);
        model.set("vrmin", -20.0);
        model.dynInitializeA(0.0, 0);
        IOdata inputs(exciterInputCount, 0.0);
        inputs[exciterVoltageInLocation] = 1.0;
        inputs[exciterVsetInLocation] = 1.0;
        inputs[exciterVdInLocation] = -0.3;
        inputs[exciterVqInLocation] = 0.9;
        inputs[exciterIdInLocation] = -0.4;
        inputs[exciterIqInLocation] = 0.6;
        // |KP*VT+j*KI*IT| in the translated machine dq convention.
        const double voltageMagnitude = std::hypot(0.21 - 0.6, 0.63 + 0.4);
        IOdata fieldSet;
        model.dynInitializeB(inputs, {0.5}, fieldSet);
        inputs[exciterXadIfdInLocation] = normalizedCurrent * voltageMagnitude / 0.2;
        const std::vector<double> state{0.8, 0.98, 0.7, 0.9, 1.1};
        std::vector<double> zero(state.size(), 0.0);
        model.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
        std::vector<double> rates(state.size(), 0.0);
        model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
        // These approximate constants reproduce the model's rectifier curve.
        constexpr double lowCurrentSlope = 0.577;  // NOLINT(modernize-use-std-numbers)
        constexpr double squareRootOfThreeApproximation =
            1.732;  // NOLINT(modernize-use-std-numbers)
        double fex = 0.0;
        if (normalizedCurrent <= 0.0) {
            fex = 1.0;
        } else if (normalizedCurrent <= 0.433) {
            fex = 1.0 - (lowCurrentSlope * normalizedCurrent);
        } else if (normalizedCurrent < 0.75) {
            fex = std::sqrt(0.75 - (normalizedCurrent * normalizedCurrent));
        } else if (normalizedCurrent <= 1.0) {
            fex = squareRootOfThreeApproximation * (1.0 - normalizedCurrent);
        }
        EXPECT_NEAR(rates[4], ((voltageMagnitude * fex * 0.7) - 1.1) / 0.5, 1e-12);
        expectExciterJacobian(model, inputs, state);
        expectExciterDaeJacobian(model, inputs, state, 0.7, 1e-4);
    }
    for (const auto& [transducerTime, amplifierTime] :
         {std::pair{0.0, 0.2}, {0.1, 0.0}, {0.0, 0.0}}) {
        exciters::ExciterESST2A model;
        model.set("tr", transducerTime);
        model.set("ta", amplifierTime);
        model.set("ka", 5.0);
        model.set("kp", 0.0);
        model.set("ki", 0.0);
        model.dynInitializeA(0.0, 0);
        IOdata inputs(exciterInputCount, 0.0);
        inputs[exciterVoltageInLocation] = 1.0;
        inputs[exciterVsetInLocation] = 1.0;
        IOdata fieldSet;
        model.dynInitializeB(inputs, {1.0}, fieldSet);
        std::vector<double> residual(model.getStates().size(), 0.0);
        model.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        for (const auto value : residual) {
            EXPECT_NEAR(value, 0.0, 1e-10);
        }
        expectExciterJacobian(model, inputs, {0.9, 0.98, 0.7, 0.9, 1.1});
        inputs[exciterVsetInLocation] = 10.0;
        expectExciterJacobian(model, inputs, {0.9, 0.98, 0.7, 0.9, 1.1});
    }
}

TEST(Esst2aTests, OelSumAndOptionalUelHighGateMatchOpenipsl)
{
    exciters::ExciterESST2A model;
    model.set("kp", 0.0);
    model.set("ki", 0.0);
    model.set("ka", 5.0);
    model.set("ta", 0.2);
    model.set("uel_enabled", 1.0);
    model.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterVssInLocation] = 0.03;
    inputs[exciterVoelInLocation] = 0.02;
    inputs[exciterVuelInLocation] = -1.0;
    IOdata fieldSet;
    model.dynInitializeB(inputs, {1.0}, fieldSet);
    const auto state = model.getStates();
    expectExciterJacobian(model, inputs, state);
    inputs[exciterVoelInLocation] += 0.1;
    std::vector<double> rates(state.size(), 0.0);
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], -2.5, 1e-12);
    expectExciterJacobian(model, inputs, state);
    inputs[exciterVuelInLocation] = 0.5;
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[2], 7.5, 1e-12);
    expectExciterJacobian(model, inputs, state);
    EXPECT_TRUE(model.supportsLimiterSignal(ExciterLimiterSignal::UNDER));
    EXPECT_TRUE(model.supportsLimiterSignal(ExciterLimiterSignal::OVER));
    exciters::ExciterESST2A defaultModel;
    EXPECT_FALSE(defaultModel.supportsLimiterSignal(ExciterLimiterSignal::UNDER));
    EXPECT_TRUE(defaultModel.supportsLimiterSignal(ExciterLimiterSignal::OVER));
}

TEST(Esst2aTests, HeldAndOvershotLimitsHaveConsistentOutputAndJacobians)
{
    exciters::ExciterESST2A model;
    model.set("kp", 0.0);
    model.set("ki", 0.0);
    model.set("ka", 5.0);
    model.set("vrmax", 2.0);
    model.set("efdmax", 1.5);
    model.dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet;
    model.dynInitializeB(inputs, {1.0}, fieldSet);
    inputs[exciterVsetInLocation] = 2.0;
    const std::vector<double> state{1.4, 1.0, 2.2, 1.4, 1.7};
    std::vector<double> zero(state.size(), 0.0);
    model.setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    expectExciterJacobian(model, inputs, state);
    model.rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    EXPECT_TRUE(model.checkFlag(exciters::ExciterESST2A::REGULATOR_LIMITED));
    EXPECT_TRUE(model.checkFlag(exciters::ExciterESST2A::FIELD_LIMITED));
    expectExciterJacobian(model, inputs, state);
    model.setRootOffset(0, cLocalSolverMode);
    std::array<double, 2> roots{};
    model.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_LT(roots[0], 0.0);
    EXPECT_LT(roots[1], 0.0);
    inputs[exciterVsetInLocation] = 0.9;
    model.rootTrigger(0.0, inputs, {1, 0}, cLocalSolverMode);
    EXPECT_FALSE(model.checkFlag(exciters::ExciterESST2A::REGULATOR_LIMITED));
    auto release = model.getStates();
    release[2] = 1.0;
    model.setState(0.0, release.data(), zero.data(), cLocalSolverMode);
    model.rootTest(inputs, emptyStateData, roots.data(), cLocalSolverMode);
    EXPECT_GT(roots[1], 0.0);
    model.rootTrigger(0.0, inputs, {0, 1}, cLocalSolverMode);
    EXPECT_FALSE(model.checkFlag(exciters::ExciterESST2A::FIELD_LIMITED));
    model.derivative(inputs, emptyStateData, zero.data(), cLocalSolverMode);
    EXPECT_LT(zero[4], 0.0);
}

TEST(Esst2aTests, FactoryCloneValidationAndReinitializationClearLimiterState)
{
    std::unique_ptr<CoreObject> object(
        CoreObjectFactory::instance()->createObject("exciter", "esst2a"));
    auto* model = dynamic_cast<exciters::ExciterESST2A*>(object.get());
    ASSERT_NE(model, nullptr);
    model->set("ka", 7.0);
    model->set("kp", 0.0);
    model->set("ki", 0.0);
    model->set("uel_enabled", 1.0);
    std::unique_ptr<CoreObject> clone(model->clone());
    EXPECT_DOUBLE_EQ(clone->get("ka"), 7.0);
    EXPECT_DOUBLE_EQ(clone->get("uel_enabled"), 1.0);
    for (const auto name : {"tr", "ta", "kc", "efdmax"}) {
        EXPECT_THROW(model->set(name, -0.1), InvalidParameterValue);
    }
    for (const auto name : {"ka", "tf", "te"}) {
        EXPECT_THROW(model->set(name, 0.0), InvalidParameterValue);
    }
    EXPECT_THROW(model->set("kp", std::numeric_limits<double>::quiet_NaN()), InvalidParameterValue);
    model->set("uel_enabled", 0.0);
    model->dynInitializeA(0.0, 0);
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.0;
    inputs[exciterVsetInLocation] = 1.0;
    IOdata fieldSet;
    model->dynInitializeB(inputs, {1.0}, fieldSet);
    auto state = model->getStates();
    std::vector<double> zero(state.size(), 0.0);
    state[2] = 5.0;
    state[4] = 6.0;
    inputs[exciterVsetInLocation] = 2.0;
    model->setState(0.0, state.data(), zero.data(), cLocalSolverMode);
    model->rootCheck(inputs, emptyStateData, cLocalSolverMode, CheckLevel::FULL_CHECK);
    EXPECT_TRUE(model->checkFlag(exciters::ExciterESST2A::REGULATOR_LIMITED));
    EXPECT_TRUE(model->checkFlag(exciters::ExciterESST2A::FIELD_LIMITED));
    inputs[exciterVsetInLocation] = 1.0;
    model->dynInitializeA(0.0, 0);
    model->dynInitializeB(inputs, {1.0}, fieldSet);
    EXPECT_FALSE(model->checkFlag(exciters::ExciterESST2A::REGULATOR_LIMITED));
    EXPECT_FALSE(model->checkFlag(exciters::ExciterESST2A::FIELD_LIMITED));
    model->set("vrmax", 0.5);
    model->dynInitializeA(0.0, 1U << STRICT_EXCITER_LIMITS);
    EXPECT_THROW(model->dynInitializeB(inputs, {1.0}, fieldSet), InvalidParameterValue);
}

TEST(Esst2aTests, NearBoundaryInitializationKeepsOutputAndFeedbackConsistent)
{
    for (const double fieldTarget : {-5e-8, 2.0 + 5e-8}) {
        exciters::ExciterESST2A model;
        model.set("kp", 0.0);
        model.set("ki", 0.0);
        model.set("efdmax", 2.0);
        model.dynInitializeA(0.0, 0);
        IOdata inputs(exciterInputCount, 0.0);
        inputs[exciterVoltageInLocation] = 1.0;
        inputs[exciterVsetInLocation] = 1.0;
        IOdata fieldSet;
        ASSERT_NO_THROW(model.dynInitializeB(inputs, {fieldTarget}, fieldSet));
        const auto state = model.getStates();
        const double field = std::clamp(fieldTarget, 0.0, 2.0);
        EXPECT_DOUBLE_EQ(state[0], field);
        EXPECT_DOUBLE_EQ(state[2], field);  // KE=VB=1
        EXPECT_DOUBLE_EQ(state[3], field);
        EXPECT_DOUBLE_EQ(state[4], field);
        std::vector<double> residual(state.size(), 0.0);
        model.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
        for (const double value : residual) {
            EXPECT_NEAR(value, 0.0, 1e-12);
        }
        EXPECT_THROW(model.dynInitializeB(inputs, {-2e-7}, fieldSet), InvalidParameterValue);
        EXPECT_THROW(model.dynInitializeB(inputs, {2.0 + 2e-7}, fieldSet), InvalidParameterValue);
    }
}

TEST(ExciterModelTests, NewExciterJacobiansMatchOffEquilibriumFiniteDifferences)
{
    IOdata inputs(exciterInputCount, 0.0);
    inputs[exciterVoltageInLocation] = 1.01;
    inputs[exciterVsetInLocation] = 1.0;
    inputs[exciterVssInLocation] = 0.02;
    inputs[exciterXadIfdInLocation] = 0.4;
    inputs[exciterIdInLocation] = 0.2;
    inputs[exciterIqInLocation] = 0.1;
    inputs[exciterVdInLocation] = 0.1;
    inputs[exciterVqInLocation] = 1.0;
    IOdata fieldSet(4, 0.0);

    exciters::ExciterAC7B ac7b;
    ac7b.dynInitializeA(0.0, 0);
    ac7b.dynInitializeB(inputs, {1.0}, fieldSet);
    expectExciterJacobian(ac7b, inputs, {5.0, 0.98, 0.2, 0.03, 0.4, 5.0, 5.2});

    exciters::ExciterAC8B ac8b;
    ac8b.set("e1", 3.0);
    ac8b.set("se1", 0.2);
    ac8b.set("e2", 2.0);
    ac8b.set("se2", 0.05);
    ac8b.dynInitializeA(0.0, 0);
    ac8b.dynInitializeB(inputs, {1.0}, fieldSet);
    expectExciterJacobian(ac8b, inputs, {2.4, 0.98, 0.2, 0.03, 2.0, 2.5});

    exciters::ExciterIEEET3 ieeet3;
    ieeet3.dynInitializeA(0.0, 0);
    ieeet3.dynInitializeB(inputs, {1.0}, fieldSet);
    expectExciterJacobian(ieeet3, inputs, {0.9, 0.98, 0.4, 0.8, 1.1});

    exciters::ExciterESST2A esst2a;
    esst2a.dynInitializeA(0.0, 0);
    esst2a.dynInitializeB(inputs, {1.0}, fieldSet);
    expectExciterJacobian(esst2a, inputs, {0.9, 0.98, 0.4, 0.8, 1.1});
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
        {"ieeex1", {{"ta", 0.1}, {"ka", 6.0}}},
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
        {"ieeex1", {{"ta", 0.1}, {"ka", 6.0}}},
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
        {"ieeex1", {{"ta", 0.3}, {"ka", 6.0}}},
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
        {"ieeex1", {{"ta", 0.3}, {"ka", 6.0}}},
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
            (excname == "esac1a") || (excname == "esac2a") || (excname == "exac4") ||
            (excname == "scrx") || (excname == "esac6a") || (excname == "ac7b") ||
            (excname == "ac8b") || (excname == "ieeet3") || (excname == "esst2a")) {
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
            (excname == "esac1a") || (excname == "esac2a") || (excname == "exac4") ||
            (excname == "scrx") || (excname == "esac6a") || (excname == "ac7b") ||
            (excname == "ac8b") || (excname == "ieeet3") || (excname == "esst2a")) {
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

TEST_F(ExciterTests, PartitionedExciterEquationSweep)
{
    using Parameter = std::pair<std::string, double>;
    struct ExciterCase {
        std::string_view name;
        std::vector<Parameter> parameters;
    };
    const std::array cases{
        ExciterCase{.name = "basic", .parameters = {{"ta", 0.2}, {"ka", 11.0}}},
        ExciterCase{.name = "dc1a", .parameters = {{"ta", 0.1}, {"ka", 6.0}, {"vrmax", 2.0}}},
        ExciterCase{.name = "dc2a", .parameters = {{"ta", 0.1}, {"ka", 6.0}}},
        ExciterCase{.name = "ieeex1", .parameters = {{"ta", 0.1}, {"ka", 6.0}}},
        ExciterCase{.name = "exdc2", .parameters = {}},
        ExciterCase{.name = "sexs", .parameters = {}},
    };

    const std::string caseFile =
        std::string(GRIDDYN_TEST_DIRECTORY) + "/governor_tests/test_gov_stability.xml";
    for (const auto& exciterCase : cases) {
        SCOPED_TRACE(exciterCase.name);
        GridDynSimulation::resetObjectCounters();
        gds = readSimXMLFile(caseFile);
        ASSERT_NE(gds, nullptr);
        gds->set("dynamicsolvermethod", "partitioned");
        gds->set("defdyndiff", "basicode");
        gds->set("timestep", 0.005);

        auto* generator = gds->findByUserID("gen", 2);
        ASSERT_NE(generator, nullptr);
        auto factory = CoreObjectFactory::instance();
        std::unique_ptr<CoreObject> object(factory->createObject("exciter", exciterCase.name));
        ASSERT_NE(object, nullptr) << "Could not create exciter " << exciterCase.name;
        for (const auto& [parameter, value] : exciterCase.parameters) {
            object->set(parameter, value);
        }
        generator->add(object.release());

        ASSERT_EQ(gds->dynInitialize(), 0) << "dynInitialize for " << exciterCase.name;
        ASSERT_EQ(gds->run(0.005), 0) << "partitioned startup for " << exciterCase.name;
        const auto algMode = gds->getSolverMode("dynalg");
        const auto diffMode = gds->getSolverMode("dyndiff");
        ASSERT_GT(gds->stateSize(algMode), 0U);
        ASSERT_GT(gds->stateSize(diffMode), 0U);

        EXPECT_EQ(runResidualCheck(gds, algMode, false), 0) << "algebraic residual";
        EXPECT_EQ(runResidualCheck(gds, diffMode, false), 0) << "differential residual";
        EXPECT_EQ(runAlgebraicCheck(gds, algMode, false), 0) << "algebraic update";
        EXPECT_EQ(runDerivativeCheck(gds, diffMode, false), 0) << "derivative";
        EXPECT_EQ(runJacobianCheck(gds, algMode, false), 0) << "algebraic Jacobian";
        EXPECT_EQ(runJacobianCheck(gds, diffMode, false), 0) << "differential Jacobian";

        ASSERT_EQ(gds->run(0.05), 0) << "short integration for " << exciterCase.name;
        for (const auto mode : {algMode, diffMode}) {
            const auto state = gds->getState(mode);
            EXPECT_TRUE(std::all_of(state.begin(), state.end(), [](double value) {
                return std::isfinite(value) && std::abs(value) < 100.0;
            })) << "non-finite or unbounded partition state";
        }
    }
}
