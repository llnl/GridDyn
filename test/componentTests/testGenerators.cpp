/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include "griddyn/ExcitationLimiter.h"
#include "griddyn/Exciter.h"
#include "griddyn/Generator.h"
#include "griddyn/GridBus.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/VoltageCompensator.h"
#include "griddyn/exciters/ExciterESAC6A.h"
#include "griddyn/exciters/ExciterSCRX.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/genmodels/GenModel6.h"
#include "griddyn/governors/GovernorIeeeSimple.h"
#include "griddyn/limiters/ExcitationLimiterMNLEX2.h"
#include "griddyn/limiters/ExcitationLimiterOEL3C.h"
#include "griddyn/limiters/ExcitationLimiterOEL4C.h"
#include "griddyn/limiters/ExcitationLimiterUEL1.h"
#include "griddyn/limiters/ExcitationLimiterUEL2C.h"
#include "utilities/MatrixDataSparse.hpp"
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <string>

#define GEN_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/gen_tests/"

using namespace griddyn;

class GeneratorTests: public GridDynSimulationTestFixture, public ::testing::Test {};

class InspectableDynamicGenerator: public DynamicGenerator {
  public:
    using DynamicGenerator::DynamicGenerator;
    const ControlSignalRouting& exciterRoutes() const { return signalRoutes[EXCITER_LOC]; }
    const ControlSignalRouting& routes(SubModelLocations model) const
    {
        return signalRoutes[model];
    }
    const IOdata& currentSignals() const { return signalFrame.values; }
};

TEST(DynamicGeneratorModelTests, SignalRoutesFreezeUntilFullDynamicInitialization)
{
    InspectableDynamicGenerator host(DynamicGenerator::DynModel::SIMPLE);
    host.dynInitializeA(0.0, 0);
    EXPECT_EQ(host.routes(DynamicGenerator::GEN_MODEL_LOC).bindings().size(), 4);
    EXPECT_EQ(host.routes(DynamicGenerator::EXCITER_LOC).bindings().size(), exciterInputCount);
    EXPECT_EQ(host.routes(DynamicGenerator::GOVERNOR_LOC).bindings().size(), 3);
    EXPECT_EQ(host.routes(DynamicGenerator::PSS_LOC).bindings().size(), pssInputCount);
    EXPECT_EQ(host.routes(DynamicGenerator::VOLTAGE_COMPENSATOR_LOC).bindings().size(),
              voltageCompensatorInputCount);
    EXPECT_EQ(host.routes(DynamicGenerator::ISOC_CONTROL_LOC).bindings().size(), 1);
    EXPECT_EQ(host.routes(DynamicGenerator::OEL_LOC).bindings().size(),
              excitationLimiterInputCount);
    EXPECT_EQ(host.routes(DynamicGenerator::UEL_LOC).bindings().size(),
              excitationLimiterInputCount);
    const ControlSignalContext context{host.currentSignals(),
                                       nullptr,
                                       emptyStateData,
                                       cLocalSolverMode};
    EXPECT_DOUBLE_EQ(host.exciterRoutes().values(context)[exciterVssInLocation], 0.0);
    EXPECT_EQ(host.exciterRoutes().inputLocations(context).locations[exciterVssInLocation],
              kNullLocation);
    for (auto input : {exciterVuelInLocation, exciterVoelInLocation}) {
        EXPECT_DOUBLE_EQ(host.exciterRoutes().values(context)[input], 0.0);
        EXPECT_EQ(host.exciterRoutes().inputLocations(context).locations[input], kNullLocation);
    }

    auto stabilizer = std::make_unique<Stabilizer>();
    EXPECT_THROW(host.add(stabilizer.get()), InvalidParameterValue);
    EXPECT_THROW(host.setMechanicalPowerSource("external"), InvalidParameterValue);
    EXPECT_THROW(host.set("mechanical_power_output", 0.0), InvalidParameterValue);

    host.resetSignalRoutesForDynamicInitialization();
    auto* attached = stabilizer.get();
    host.add(attached);
    stabilizer.release();
    EXPECT_NO_THROW(host.dynInitializeA(0.0, 0));
    const auto& bindings = host.exciterRoutes().bindings();
    const auto stabilizerRoute =
        std::find_if(bindings.begin(), bindings.end(), [](const auto& route) {
            return route.inputIndex == exciterVssInLocation;
        });
    ASSERT_NE(stabilizerRoute, bindings.end());
    EXPECT_EQ(stabilizerRoute->sourceName, attached->getName());
    EXPECT_THROW(host.remove(attached), InvalidParameterValue);

    host.resetSignalRoutesForDynamicInitialization();
    host.remove(attached);
    EXPECT_NO_THROW(host.dynInitializeA(0.0, 0));
    EXPECT_DOUBLE_EQ(host.exciterRoutes().values(context)[exciterVssInLocation], 0.0);
}

TEST(DynamicGeneratorModelTests, LimiterRoutesRequireSupportedExciterAndFreeze)
{
    InspectableDynamicGenerator unsupported(DynamicGenerator::DynModel::SIMPLE);
    auto* unsupportedOel = new ExcitationLimiter();
    unsupportedOel->set("threshold", 1.0);
    unsupported.add(unsupportedOel);
    EXPECT_THROW(unsupported.dynInitializeA(0.0, 0), InvalidParameterValue);

    InspectableDynamicGenerator host(DynamicGenerator::DynModel::SIMPLE);
    host.add(new exciters::ExciterSCRX());
    auto* oel = new ExcitationLimiter("field_oel");
    oel->set("threshold", 1.0);
    host.add(oel);
    auto* uel = new ExcitationLimiter("reactive_uel");
    uel->setRole(ExcitationLimiter::Role::UNDER);
    uel->set("threshold", -0.2);
    host.add(uel);
    std::unique_ptr<CoreObject> clonedObject(host.clone());
    auto* clone = dynamic_cast<DynamicGenerator*>(clonedObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ExcitationLimiter*>(clone->find("oel")), nullptr);
    EXPECT_NE(dynamic_cast<ExcitationLimiter*>(clone->find("uel")), nullptr);
    EXPECT_NO_THROW(clone->dynInitializeA(0.0, 0));
    ASSERT_NO_THROW(host.dynInitializeA(0.0, 0));
    EXPECT_EQ(host.find("oel"), oel);
    EXPECT_EQ(host.find("uel"), uel);
    const auto& bindings = host.exciterRoutes().bindings();
    const auto routeName = [&bindings](index_t input) {
        const auto route =
            std::find_if(bindings.begin(), bindings.end(), [input](const auto& item) {
                return item.inputIndex == input;
            });
        return route == bindings.end() ? std::string{} : route->sourceName;
    };
    EXPECT_EQ(routeName(exciterVoelInLocation), oel->getName());
    EXPECT_EQ(routeName(exciterVuelInLocation), uel->getName());
    EXPECT_THROW(host.remove(oel), InvalidParameterValue);

    InspectableDynamicGenerator uelOnly(DynamicGenerator::DynModel::SIMPLE);
    uelOnly.add(new exciters::ExciterESAC6A());
    auto* allowed = new ExcitationLimiter();
    allowed->setRole(ExcitationLimiter::Role::UNDER);
    allowed->set("threshold", -0.2);
    uelOnly.add(allowed);
    EXPECT_NO_THROW(uelOnly.dynInitializeA(0.0, 0));
}

TEST(ExcitationLimiterTests, OneModelImplementsBothActionsAndJacobian)
{
    std::unique_ptr<CoreObject> constructed(
        CoreObjectFactory::instance()->createObject("excitationlimiter", "limiter"));
    ASSERT_NE(dynamic_cast<ExcitationLimiter*>(constructed.get()), nullptr);
    ExcitationLimiter oel;
    oel.set("threshold", 1.0);
    oel.set("gain", 2.0);
    oel.dynInitializeA(0.0, 0);
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterFieldCurrentInLocation] = 1.2;
    IOdata fieldSet;
    oel.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_NEAR(oel.getOutput(), 0.4, 1e-12);
    oel.setOffset(0, cDaeSolverMode);
    auto state = oel.getStates();
    StateData stateData(0.0, state.data());
    stateData.stateSize = static_cast<count_t>(state.size());
    IOlocs inputLocs(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        inputLocs[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    oel.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 10), 2.0);
    inputs[limiterFieldCurrentInLocation] = 0.9;
    oel.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(oel.getOutput(), 0.0);
    EXPECT_THROW(oel.setRole(ExcitationLimiter::Role::UNDER), InvalidParameterValue);

    ExcitationLimiter uel;
    uel.setRole(ExcitationLimiter::Role::UNDER);
    uel.set("threshold", -0.2);
    uel.set("gain", 2.0);
    uel.dynInitializeA(0.0, 0);
    inputs = IOdata(excitationLimiterInputCount, 0.0);
    inputs[limiterIqInLocation] = 0.3;
    inputs[limiterVdInLocation] = 1.0;
    uel.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_NEAR(uel.getOutput(), 0.2, 1e-12);
    uel.setOffset(0, cDaeSolverMode);
    state = uel.getStates();
    StateData uelState(0.0, state.data());
    uelState.stateSize = static_cast<count_t>(state.size());
    MatrixDataSparse<double> uelJacobian;
    uel.jacobianElements(inputs, uelState, uelJacobian, inputLocs, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(uelJacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(uelJacobian.at(0, 12), 2.0);
    EXPECT_DOUBLE_EQ(uelJacobian.at(0, 13), 0.6);
}

TEST(ExcitationLimiterTests, Mnlex2CircleRateFeedbackAndJacobian)
{
    std::unique_ptr<CoreObject> constructed(
        CoreObjectFactory::instance()->createObject("excitationlimiter", "mnlex2"));
    ASSERT_NE(dynamic_cast<limiters::ExcitationLimiterMNLEX2*>(constructed.get()), nullptr);
    limiters::ExcitationLimiterMNLEX2 limiter;
    EXPECT_THROW(limiter.setRole(ExcitationLimiter::Role::OVER), InvalidParameterValue);
    limiter.set("q0", 0.0);
    limiter.set("radius", 0.5);
    limiter.set("km", 1.0);
    limiter.set("tm", 1.0);
    limiter.set("kf2", 0.1);
    limiter.set("melmax", 2.0);
    std::unique_ptr<CoreObject> cloned(limiter.clone());
    auto* mnlexClone = dynamic_cast<limiters::ExcitationLimiterMNLEX2*>(cloned.get());
    ASSERT_NE(mnlexClone, nullptr);
    EXPECT_EQ(mnlexClone->role(), ExcitationLimiter::Role::UNDER);
    EXPECT_DOUBLE_EQ(mnlexClone->get("radius"), 0.5);
    limiter.dynInitializeA(0.0, 0);
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterIdInLocation] = 1.0;
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    limiter.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_NEAR(limiter.getOutput(), 0.75, 1e-12);
    limiter.setOffset(0, cDaeSolverMode);
    auto states = limiter.getStates();
    StateData stateData(0.0, states.data());
    stateData.stateSize = static_cast<count_t>(states.size());
    stateData.cj = 2.0;
    IOlocs locations(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        locations[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    limiter.jacobianElements(inputs, stateData, jacobian, locations, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 1), 1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 1), -3.1);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 2), 0.1);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 11), 2.0);
    inputs[limiterIdInLocation] = 1.1;
    limiter.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_NEAR(limiter.getOutput(), 0.771, 1e-12);
    limiter.timestep(0.2, inputs, cLocalSolverMode);
    EXPECT_NEAR(limiter.getOutput(), 0.78969, 1e-12);
}

TEST(ExcitationLimiterTests, Oel4cDelaySignedOutputAndTimerReset)
{
    std::unique_ptr<CoreObject> constructed(
        CoreObjectFactory::instance()->createObject("excitationlimiter", "oel4c"));
    ASSERT_NE(dynamic_cast<limiters::ExcitationLimiterOEL4C*>(constructed.get()), nullptr);
    limiters::ExcitationLimiterOEL4C limiter;
    EXPECT_THROW(limiter.setRole(ExcitationLimiter::Role::UNDER), InvalidParameterValue);
    limiter.set("qref", 0.1);
    limiter.set("kp", 1.0);
    limiter.set("ki", 1.0);
    limiter.set("tdelay", 0.2);
    limiter.set("vmin", -0.5);
    std::unique_ptr<CoreObject> cloned(limiter.clone());
    auto* oelClone = dynamic_cast<limiters::ExcitationLimiterOEL4C*>(cloned.get());
    ASSERT_NE(oelClone, nullptr);
    EXPECT_EQ(oelClone->role(), ExcitationLimiter::Role::OVER);
    EXPECT_DOUBLE_EQ(oelClone->get("qref"), 0.1);
    limiter.dynInitializeA(0.0, 0);
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterIqInLocation] = -0.4;
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    limiter.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.0);
    limiter.setOffset(0, cDaeSolverMode);
    auto inactiveStates = limiter.getStates();
    std::array<double, 2> inactiveRates{};
    StateData inactiveData(0.1, inactiveStates.data(), inactiveRates.data());
    inactiveData.stateSize = static_cast<count_t>(inactiveStates.size());
    inactiveData.cj = 2.0;
    std::array<double, 2> inactiveResidual{};
    limiter.residual(inputs, inactiveData, inactiveResidual.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(inactiveResidual[0], 0.0);
    EXPECT_DOUBLE_EQ(inactiveResidual[1], 0.0);
    IOlocs inactiveLocations(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        inactiveLocations[index] = 10 + index;
    }
    MatrixDataSparse<double> inactiveJacobian;
    limiter.jacobianElements(
        inputs, inactiveData, inactiveJacobian, inactiveLocations, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(inactiveJacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(inactiveJacobian.at(0, 1), 0.0);
    EXPECT_DOUBLE_EQ(inactiveJacobian.at(1, 1), -2.0);
    limiter.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.0);
    limiter.timestep(0.2, inputs, cLocalSolverMode);
    EXPECT_NEAR(limiter.getOutput(), 0.33, 1e-12);
    limiter.setOffset(0, cDaeSolverMode);
    auto states = limiter.getStates();
    StateData stateData(0.2, states.data());
    stateData.stateSize = static_cast<count_t>(states.size());
    IOlocs locations(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        locations[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    limiter.jacobianElements(inputs, stateData, jacobian, locations, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 1), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 12), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 12), 1.0);
    inputs[limiterIqInLocation] = 0.0;
    limiter.timestep(0.3, inputs, cLocalSolverMode);
    EXPECT_NEAR(limiter.getOutput(), 0.03, 1e-12);
    inputs[limiterIqInLocation] = -0.4;
    limiter.timestep(0.4, inputs, cLocalSolverMode);
    EXPECT_NEAR(limiter.getOutput(), 0.03, 1e-12);
    limiter.timestep(0.6, inputs, cLocalSolverMode);
    EXPECT_NEAR(limiter.getOutput(), 0.39, 1e-12);
}

TEST(ExcitationLimiterTests, AdditionalWeccLimiterProfilesRegisterAndProvideConsistentJacobians)
{
    std::unique_ptr<CoreObject> uel1Object(
        CoreObjectFactory::instance()->createObject("excitationlimiter", "uel1"));
    auto* uel1Factory = dynamic_cast<limiters::ExcitationLimiterUEL1*>(uel1Object.get());
    ASSERT_NE(uel1Factory, nullptr);
    EXPECT_THROW(uel1Factory->setRole(ExcitationLimiter::Role::OVER), InvalidParameterValue);
    uel1Factory->set("kuf", 0.2);
    EXPECT_THROW(uel1Factory->dynInitializeA(0.0, 0), InvalidParameterValue);

    limiters::ExcitationLimiterUEL1 uel1;
    uel1.set("kur", 0.1);
    uel1.set("kuc", 0.0);
    uel1.set("kul", 1.0);
    uel1.set("tu1", 0.02);
    uel1.set("tu2", 0.1);
    uel1.set("tu3", 0.05);
    uel1.set("tu4", 0.25);
    uel1.dynInitializeA(0.0, 0);
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterIqInLocation] = 1.0;
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    uel1.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_NEAR(uel1.getOutput(), 0.9, 1e-12);
    uel1.setOffset(0, cDaeSolverMode);
    auto states = uel1.getStates();
    StateData stateData(0.0, states.data());
    stateData.stateSize = static_cast<count_t>(states.size());
    IOlocs inputLocs(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        inputLocs[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    uel1.jacobianElements(inputs, stateData, jacobian, inputLocs, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 12), 0.04);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 13), -0.004);
    std::array<double, 4> uel1Rates{};
    std::array<double, 4> uel1Residual{};
    StateData uel1ResidualData(0.0, states.data(), uel1Rates.data());
    uel1ResidualData.stateSize = static_cast<count_t>(states.size());
    uel1.residual(inputs, uel1ResidualData, uel1Residual.data(), cDaeSolverMode);
    auto perturbedInputs = inputs;
    constexpr double finiteDifferenceStep = 1e-6;
    perturbedInputs[limiterIqInLocation] += finiteDifferenceStep;
    std::array<double, 4> perturbedUel1Residual{};
    uel1.residual(perturbedInputs, uel1ResidualData, perturbedUel1Residual.data(), cDaeSolverMode);
    EXPECT_NEAR((perturbedUel1Residual[0] - uel1Residual[0]) / finiteDifferenceStep,
                jacobian.at(0, 12),
                1e-6);

    limiters::ExcitationLimiterUEL1 cappedUel1;
    cappedUel1.set("kur", 0.1);
    cappedUel1.set("kuc", 0.0);
    cappedUel1.set("kul", 1.0);
    cappedUel1.set("tu2", 0.0);
    cappedUel1.set("tu4", 0.0);
    cappedUel1.set("vulmax", 0.5);
    cappedUel1.dynInitializeA(0.0, 0);
    cappedUel1.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(cappedUel1.getOutput(), 0.5);
    for (int step = 1; step <= 10; ++step) {
        cappedUel1.timestep(step * 0.1, inputs, cLocalSolverMode);
        EXPECT_DOUBLE_EQ(cappedUel1.getOutput(), 0.5);
    }

    std::unique_ptr<CoreObject> uel2cObject(
        CoreObjectFactory::instance()->createObject("excitationlimiter", "uel2c"));
    auto* uel2cFactory = dynamic_cast<limiters::ExcitationLimiterUEL2C*>(uel2cObject.get());
    ASSERT_NE(uel2cFactory, nullptr);
    EXPECT_THROW(uel2cFactory->setRole(ExcitationLimiter::Role::OVER), InvalidParameterValue);
    EXPECT_THROW(uel2cFactory->set("k1", 1.5), InvalidParameterValue);

    limiters::ExcitationLimiterUEL2C uel2c;
    const std::array<double, 5> curveP{0.0, 0.3, 0.6, 0.9, 1.02};
    const std::array<double, 5> curveQ{-0.31, -0.31, -0.28, -0.21, 0.0};
    for (std::size_t point = 0; point < curveP.size(); ++point) {
        uel2c.set("p" + std::to_string(point), curveP[point]);
        uel2c.set("q" + std::to_string(point), curveQ[point]);
    }
    uel2c.set("kul", 1.0);
    uel2c.set("kui", 0.0);
    uel2c.set("k2", 1.0);
    uel2c.dynInitializeA(0.0, 0);
    inputs.assign(excitationLimiterInputCount, 0.0);
    inputs[limiterIdInLocation] = 0.45;
    inputs[limiterIqInLocation] = 0.4;
    inputs[limiterVdInLocation] = 1.0;
    uel2c.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_NEAR(uel2c.getOutput(), 0.105, 1e-12);
    uel2c.setOffset(0, cDaeSolverMode);
    states = uel2c.getStates();
    StateData uel2cState(0.0, states.data());
    uel2cState.stateSize = static_cast<count_t>(states.size());
    MatrixDataSparse<double> uel2cJacobian;
    uel2c.jacobianElements(inputs, uel2cState, uel2cJacobian, inputLocs, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(uel2cJacobian.at(0, 1), 1.0);
    EXPECT_DOUBLE_EQ(uel2cJacobian.at(0, 12), 1.0);
    EXPECT_NEAR(uel2cJacobian.at(0, 13), 0.15, 1e-12);
    std::array<double, 2> uel2cRates{};
    std::array<double, 2> uel2cResidual{};
    StateData uel2cResidualData(0.0, states.data(), uel2cRates.data());
    uel2cResidualData.stateSize = static_cast<count_t>(states.size());
    uel2c.residual(inputs, uel2cResidualData, uel2cResidual.data(), cDaeSolverMode);
    perturbedInputs = inputs;
    perturbedInputs[limiterIqInLocation] += finiteDifferenceStep;
    std::array<double, 2> perturbedUel2cResidual{};
    uel2c.residual(perturbedInputs,
                   uel2cResidualData,
                   perturbedUel2cResidual.data(),
                   cDaeSolverMode);
    EXPECT_NEAR((perturbedUel2cResidual[0] - uel2cResidual[0]) / finiteDifferenceStep,
                uel2cJacobian.at(0, 12),
                1e-6);

    limiters::ExcitationLimiterUEL2C cappedUel2c;
    for (std::size_t point = 0; point < curveP.size(); ++point) {
        cappedUel2c.set("p" + std::to_string(point), curveP[point]);
        cappedUel2c.set("q" + std::to_string(point), curveQ[point]);
    }
    cappedUel2c.set("kul", 1.0);
    cappedUel2c.set("kui", 0.5);
    cappedUel2c.dynInitializeA(0.0, 0);
    auto cappedInputs = inputs;
    cappedInputs[limiterIqInLocation] = 1.0;
    cappedUel2c.dynInitializeB(cappedInputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(cappedUel2c.getOutput(), 0.25);
    for (int step = 1; step <= 10; ++step) {
        cappedUel2c.timestep(step * 0.1, cappedInputs, cLocalSolverMode);
        EXPECT_DOUBLE_EQ(cappedUel2c.getOutput(), 0.25);
    }

    std::unique_ptr<CoreObject> oel3cObject(
        CoreObjectFactory::instance()->createObject("excitationlimiter", "oel3c"));
    auto* oel3cFactory = dynamic_cast<limiters::ExcitationLimiterOEL3C*>(oel3cObject.get());
    ASSERT_NE(oel3cFactory, nullptr);
    EXPECT_THROW(oel3cFactory->setRole(ExcitationLimiter::Role::UNDER), InvalidParameterValue);
    EXPECT_THROW(oel3cFactory->set("oelinput", 1.0), InvalidParameterValue);

    limiters::ExcitationLimiterOEL3C oel3c;
    oel3c.set("itfpu", 1.0);
    oel3c.set("tf", 0.0);
    oel3c.set("k1", 2.0);
    oel3c.set("kpoel", 0.5);
    oel3c.set("koel", 1.0);
    oel3c.set("toel", 1.0);
    oel3c.set("voelmin2", -0.5);
    oel3c.dynInitializeA(0.0, 0);
    inputs.assign(excitationLimiterInputCount, 0.0);
    inputs[limiterFieldCurrentInLocation] = 1.2;
    oel3c.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_NEAR(oel3c.getOutput(), 0.22, 1e-12);
    oel3c.setOffset(0, cDaeSolverMode);
    states = oel3c.getStates();
    StateData oel3cState(0.0, states.data());
    oel3cState.stateSize = static_cast<count_t>(states.size());
    std::array<double, 3> rates{};
    std::array<double, 3> derivatives{};
    StateData oel3cDynamicState(0.0, states.data(), rates.data());
    oel3cDynamicState.stateSize = static_cast<count_t>(states.size());
    oel3c.derivative(inputs, oel3cDynamicState, derivatives.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(derivatives[1], 0.0);
    EXPECT_NEAR(derivatives[2], -0.44, 1e-12);
    MatrixDataSparse<double> oel3cJacobian;
    oel3c.jacobianElements(inputs, oel3cState, oel3cJacobian, inputLocs, cDaeSolverMode);
    EXPECT_NEAR(oel3cJacobian.at(0, 10), 1.2, 1e-12);
    EXPECT_NEAR(oel3cJacobian.at(2, 10), -2.4, 1e-12);
    std::array<double, 3> oel3cResidual{};
    StateData oel3cResidualData(0.0, states.data(), rates.data());
    oel3cResidualData.stateSize = static_cast<count_t>(states.size());
    oel3c.residual(inputs, oel3cResidualData, oel3cResidual.data(), cDaeSolverMode);
    perturbedInputs = inputs;
    perturbedInputs[limiterFieldCurrentInLocation] += finiteDifferenceStep;
    std::array<double, 3> perturbedOel3cResidual{};
    oel3c.residual(perturbedInputs,
                   oel3cResidualData,
                   perturbedOel3cResidual.data(),
                   cDaeSolverMode);
    EXPECT_NEAR((perturbedOel3cResidual[0] - oel3cResidual[0]) / finiteDifferenceStep,
                oel3cJacobian.at(0, 10),
                1e-6);
    for (int step = 1; step <= 100; ++step) {
        oel3c.timestep(step * 0.1, inputs, cLocalSolverMode);
    }
    EXPECT_DOUBLE_EQ(oel3c.getOutput(), 0.5);

    limiters::ExcitationLimiterOEL3C filteredOel3c;
    filteredOel3c.dynInitializeA(0.0, 0);
    inputs[limiterFieldCurrentInLocation] = 1.2;
    filteredOel3c.dynInitializeB(inputs, {}, fieldSet);
    filteredOel3c.setOffset(0, cDaeSolverMode);
    auto filteredStates = filteredOel3c.getStates();
    StateData filteredState(0.0, filteredStates.data());
    filteredState.stateSize = static_cast<count_t>(filteredStates.size());
    filteredState.cj = 0.0;
    MatrixDataSparse<double> filteredJacobian;
    filteredOel3c.jacobianElements(
        inputs, filteredState, filteredJacobian, inputLocs, cDaeSolverMode);
    EXPECT_NEAR(filteredJacobian.at(0, 1), 1.0 / 1.05, 1e-12);
    EXPECT_DOUBLE_EQ(filteredJacobian.at(1, 1), -50.0);
    EXPECT_DOUBLE_EQ(filteredJacobian.at(1, 10), 50.0);
    EXPECT_NEAR(filteredJacobian.at(2, 1), -1.0 / 25.2, 1e-12);
}

TEST(ExcitationLimiterTests, Oel4cSaturationStopsIntegration)
{
    limiters::ExcitationLimiterOEL4C limiter;
    limiter.set("qref", 0.0);
    limiter.set("tdelay", 0.0);
    limiter.set("kp", 2.0);
    limiter.set("ki", 1.0);
    limiter.set("vmin", -0.2);
    limiter.dynInitializeA(0.0, 0);
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterIqInLocation] = -0.4;
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    limiter.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.2);
    limiter.setOffset(0, cDaeSolverMode);
    auto states = limiter.getStates();
    std::array<double, 2> stateRate{};
    StateData stateData(0.0, states.data(), stateRate.data());
    stateData.stateSize = static_cast<count_t>(states.size());
    stateData.cj = 2.0;
    std::array<double, 2> residual{};
    limiter.residual(inputs, stateData, residual.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(residual[0], 0.0);
    EXPECT_DOUBLE_EQ(residual[1], 0.0);
    double deriv[2]{};
    limiter.derivative(inputs, stateData, deriv, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(deriv[1], 0.0);
    IOlocs locations(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        locations[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    limiter.jacobianElements(inputs, stateData, jacobian, locations, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 1), 0.0);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 1), -2.0);
    limiter.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.2);
}

TEST(ExcitationLimiterTests, Mnlex2SaturationClampsOutputAndKeepsResidualJacobianConsistent)
{
    limiters::ExcitationLimiterMNLEX2 limiter;
    limiter.set("q0", 0.0);
    limiter.set("radius", 0.5);
    limiter.set("km", 1.0);
    limiter.set("tm", 1.0);
    limiter.set("melmax", 0.5);
    ASSERT_NO_THROW(limiter.dynInitializeA(0.0, 0));
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterIdInLocation] = 1.0;
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    limiter.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.5);

    limiter.setOffset(0, cDaeSolverMode);
    auto states = limiter.getStates();
    std::array<double, 3> stateRate{};
    StateData stateData(0.0, states.data(), stateRate.data());
    stateData.stateSize = static_cast<count_t>(states.size());
    stateData.cj = 2.0;
    std::array<double, 3> residual{};
    limiter.residual(inputs, stateData, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_DOUBLE_EQ(value, 0.0);
    }

    std::array<double, 3> derivative{};
    limiter.derivative(inputs, stateData, derivative.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);

    IOlocs locations(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        locations[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    limiter.jacobianElements(inputs, stateData, jacobian, locations, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 1), 0.0);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 1), -2.0);
    EXPECT_DOUBLE_EQ(jacobian.at(2, 1), 0.0);
    EXPECT_DOUBLE_EQ(jacobian.at(2, 2), -3.0);

    limiter.timestep(0.1, inputs, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.5);
}

TEST(ExcitationLimiterTests, Mnlex2InactiveLowerLimitHasConsistentResidualAndJacobian)
{
    limiters::ExcitationLimiterMNLEX2 limiter;
    limiter.set("q0", 0.0);
    limiter.set("radius", 2.0);
    limiter.set("km", 1.0);
    limiter.set("tm", 1.0);
    limiter.set("melmax", 0.5);
    ASSERT_NO_THROW(limiter.dynInitializeA(0.0, 0));
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterIdInLocation] = 1.0;
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    limiter.dynInitializeB(inputs, {}, fieldSet);
    EXPECT_DOUBLE_EQ(limiter.getOutput(), 0.0);

    limiter.setOffset(0, cDaeSolverMode);
    auto states = limiter.getStates();
    std::array<double, 3> stateRate{};
    StateData stateData(0.0, states.data(), stateRate.data());
    stateData.stateSize = static_cast<count_t>(states.size());
    stateData.cj = 2.0;
    std::array<double, 3> residual{};
    limiter.residual(inputs, stateData, residual.data(), cDaeSolverMode);
    for (double value : residual) {
        EXPECT_DOUBLE_EQ(value, 0.0);
    }
    std::array<double, 3> derivative{};
    limiter.derivative(inputs, stateData, derivative.data(), cDaeSolverMode);
    EXPECT_DOUBLE_EQ(derivative[1], 0.0);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);

    IOlocs locations(excitationLimiterInputCount, kNullLocation);
    for (index_t index = 0; index < excitationLimiterInputCount; ++index) {
        locations[index] = 10 + index;
    }
    MatrixDataSparse<double> jacobian;
    limiter.jacobianElements(inputs, stateData, jacobian, locations, cDaeSolverMode);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 0), -1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(0, 1), 1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(1, 1), -2.0);
    EXPECT_DOUBLE_EQ(jacobian.at(2, 1), 1.0);
    EXPECT_DOUBLE_EQ(jacobian.at(2, 2), -3.0);
}

TEST(DynamicGeneratorModelTests, ConcreteLimitersAttachToExistingRoutes)
{
    InspectableDynamicGenerator host(DynamicGenerator::DynModel::SIMPLE);
    host.add(new exciters::ExciterSCRX());
    host.add(new limiters::ExcitationLimiterMNLEX2("circle_uel"));
    auto* oel = new limiters::ExcitationLimiterOEL4C("reactive_oel");
    oel->set("qref", 0.1);
    host.add(oel);
    ASSERT_NO_THROW(host.dynInitializeA(0.0, 0));
    const auto& bindings = host.exciterRoutes().bindings();
    const auto sourceFor = [&bindings](index_t input) {
        const auto found =
            std::find_if(bindings.begin(), bindings.end(), [input](const auto& route) {
                return route.inputIndex == input;
            });
        return found == bindings.end() ? std::string{} : found->sourceName;
    };
    EXPECT_EQ(sourceFor(exciterVuelInLocation), "circle_uel");
    EXPECT_EQ(sourceFor(exciterVoelInLocation), "reactive_oel");

    const auto verifyRoute =
        [](ExcitationLimiter* limiter, index_t exciterInput, const std::string& limiterName) {
            InspectableDynamicGenerator candidate(DynamicGenerator::DynModel::SIMPLE);
            candidate.add(new exciters::ExciterSCRX());
            candidate.add(limiter);
            EXPECT_NO_THROW(candidate.dynInitializeA(0.0, 0));
            const auto& limiterRoutes = candidate.exciterRoutes().bindings();
            const auto route = std::find_if(limiterRoutes.begin(),
                                            limiterRoutes.end(),
                                            [exciterInput](const auto& binding) {
                                                return binding.inputIndex == exciterInput;
                                            });
            ASSERT_NE(route, limiterRoutes.end());
            EXPECT_EQ(route->sourceName, limiterName);
        };
    verifyRoute(new limiters::ExcitationLimiterUEL1("circular_uel1"),
                exciterVuelInLocation,
                "circular_uel1");
    verifyRoute(new limiters::ExcitationLimiterOEL3C("field_oel3c"),
                exciterVoelInLocation,
                "field_oel3c");

    auto* curveUel = new limiters::ExcitationLimiterUEL2C("curve_uel2c");
    for (std::size_t point = 0; point < 2; ++point) {
        curveUel->set("p" + std::to_string(point), static_cast<double>(point));
        curveUel->set("q" + std::to_string(point), -0.2 + 0.2 * point);
    }
    verifyRoute(curveUel, exciterVuelInLocation, "curve_uel2c");
}

TEST_F(GeneratorTests, XmlLimiterRoleSelectsSlotBeforeAttachment)
{
    gds = readSimXMLFile(std::string(GEN_TEST_DIRECTORY "test_excitation_limiter_roles.xml"));
    auto* bus = dynamic_cast<GridBus*>(gds->find("bus1"));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* oel = dynamic_cast<ExcitationLimiter*>(generator->find("oel"));
    auto* uel = dynamic_cast<ExcitationLimiter*>(generator->find("uel"));
    ASSERT_NE(oel, nullptr);
    ASSERT_NE(uel, nullptr);
    EXPECT_EQ(oel->role(), ExcitationLimiter::Role::OVER);
    EXPECT_EQ(uel->role(), ExcitationLimiter::Role::UNDER);
    EXPECT_EQ(oel->getName(), "field_oel");
    EXPECT_EQ(uel->getName(), "reactive_uel");
}

TEST_F(GeneratorTests, XmlLoadsConcreteLimiterTypesAndParameters)
{
    gds = readSimXMLFile(std::string(GEN_TEST_DIRECTORY "test_excitation_limiter_models.xml"));
    auto* bus = dynamic_cast<GridBus*>(gds->find("bus1"));
    ASSERT_NE(bus, nullptr);
    auto* generator = dynamic_cast<DynamicGenerator*>(bus->getGen(0));
    ASSERT_NE(generator, nullptr);
    auto* oel = dynamic_cast<limiters::ExcitationLimiterOEL4C*>(generator->find("oel"));
    auto* uel = dynamic_cast<limiters::ExcitationLimiterMNLEX2*>(generator->find("uel"));
    ASSERT_NE(oel, nullptr);
    ASSERT_NE(uel, nullptr);
    EXPECT_EQ(oel->role(), ExcitationLimiter::Role::OVER);
    EXPECT_EQ(uel->role(), ExcitationLimiter::Role::UNDER);
    EXPECT_DOUBLE_EQ(oel->get("qref"), 0.1);
    EXPECT_DOUBLE_EQ(oel->get("tdelay"), 0.2);
    EXPECT_DOUBLE_EQ(uel->get("radius"), 0.5);
}

TEST_F(GeneratorTests, XmlLoadsAdditionalConcreteLimiterProfiles)
{
    gds = readSimXMLFile(std::string(GEN_TEST_DIRECTORY "test_excitation_limiter_new_models.xml"));
    auto* bus = dynamic_cast<GridBus*>(gds->find("bus1"));
    ASSERT_NE(bus, nullptr);

    auto* oelGenerator = dynamic_cast<DynamicGenerator*>(bus->getGen(0));
    auto* uel1Generator = dynamic_cast<DynamicGenerator*>(bus->getGen(1));
    auto* uel2cGenerator = dynamic_cast<DynamicGenerator*>(bus->getGen(2));
    ASSERT_NE(oelGenerator, nullptr);
    ASSERT_NE(uel1Generator, nullptr);
    ASSERT_NE(uel2cGenerator, nullptr);

    auto* oel3c = dynamic_cast<limiters::ExcitationLimiterOEL3C*>(oelGenerator->find("oel"));
    auto* uel1 = dynamic_cast<limiters::ExcitationLimiterUEL1*>(uel1Generator->find("uel"));
    auto* uel2c = dynamic_cast<limiters::ExcitationLimiterUEL2C*>(uel2cGenerator->find("uel"));
    ASSERT_NE(oel3c, nullptr);
    ASSERT_NE(uel1, nullptr);
    ASSERT_NE(uel2c, nullptr);
    EXPECT_DOUBLE_EQ(oel3c->get("itfpu"), 1.05);
    EXPECT_DOUBLE_EQ(uel1->get("kuc"), 1.38);
    EXPECT_DOUBLE_EQ(uel2c->get("p2"), 1.0);
    EXPECT_DOUBLE_EQ(uel2c->get("q2"), 0.0);
}

TEST(ExcitationLimiterTests, Oel4cRootsScheduleAndResetDelay)
{
    limiters::ExcitationLimiterOEL4C limiter;
    limiter.set("qref", 0.1);
    limiter.set("tdelay", 0.2);
    limiter.dynInitializeA(0.0, 0);
    IOdata inputs(excitationLimiterInputCount, 0.0);
    inputs[limiterVdInLocation] = 1.0;
    IOdata fieldSet;
    limiter.dynInitializeB(inputs, {}, fieldSet);
    limiter.setRootOffset(0, cLocalSolverMode);
    inputs[limiterIqInLocation] = -0.4;
    auto states = limiter.getStates();
    StateData entered(0.1, states.data());
    EXPECT_EQ(limiter.rootCheck(inputs, entered, cLocalSolverMode, CheckLevel::FULL_CHECK),
              ChangeCode::JACOBIAN_CHANGE);
    double roots[2]{};
    StateData beforeDelay(0.2, states.data());
    limiter.rootTest(inputs, beforeDelay, roots, cLocalSolverMode);
    EXPECT_NEAR(roots[0], 0.3, 1e-8);
    EXPECT_NEAR(roots[1], -0.1, 1e-12);
    StateData atDelay(0.3, states.data());
    limiter.rootTest(inputs, atDelay, roots, cLocalSolverMode);
    EXPECT_NEAR(roots[1], 0.0, 1e-12);
    limiter.rootTrigger(0.3, inputs, {0, 1}, cLocalSolverMode);
    limiter.rootTest(inputs, atDelay, roots, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(roots[1], 1.0);
    inputs[limiterIqInLocation] = 0.0;
    limiter.rootTrigger(0.4, inputs, {-1, 0}, cLocalSolverMode);
    StateData cleared(0.4, states.data());
    limiter.rootTest(inputs, cleared, roots, cLocalSolverMode);
    EXPECT_DOUBLE_EQ(roots[1], 1.0);
}

TEST(DynamicGeneratorModelTests, MechanicalPowerSourceCanBeExternalAndIndexed)
{
    DynamicGenerator primary;
    auto* governor = new governors::GovernorIeeeSimple();
    primary.add(governor);

    EXPECT_EQ(primary.getMechanicalPowerSource(), governor);
    EXPECT_EQ(primary.getMechanicalPowerOutput(), 0);
    EXPECT_FALSE(primary.hasExplicitMechanicalPowerSource());

    auto* multiOutputSource = new genmodels::GenModel6();
    primary.add(multiOutputSource);

    DynamicGenerator secondary;
    secondary.setMechanicalPowerSource(multiOutputSource, 1);
    EXPECT_EQ(secondary.getMechanicalPowerSource(), multiOutputSource);
    EXPECT_EQ(secondary.getMechanicalPowerOutput(), 1);
    EXPECT_TRUE(secondary.hasExplicitMechanicalPowerSource());

    EXPECT_THROW(secondary.setMechanicalPowerSource(multiOutputSource, 2), InvalidParameterValue);

    secondary.clearMechanicalPowerSource();
    EXPECT_EQ(secondary.getMechanicalPowerSource(), nullptr);
    EXPECT_FALSE(secondary.hasExplicitMechanicalPowerSource());
}

TEST_F(GeneratorTests, MechanicalPowerSourcePathResolvesAfterTreeAssembly)
{
    const std::string fileName =
        std::string(GRIDDYN_TEST_DIRECTORY "/governor_tests/test_gov_stability.xml");
    gds = readSimXMLFile(fileName);

    auto* bus1 = dynamic_cast<GridBus*>(gds->find("bus1"));
    auto* bus2 = dynamic_cast<GridBus*>(gds->find("bus2"));
    ASSERT_NE(bus1, nullptr);
    ASSERT_NE(bus2, nullptr);

    auto* primary = dynamic_cast<DynamicGenerator*>(bus1->getGen(0));
    auto* secondary = dynamic_cast<DynamicGenerator*>(bus2->getGen(0));
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);

    auto* governor = new governors::GovernorIeeeSimple("shared_governor");
    primary->add(governor);
    secondary->add(new genmodels::GenModel6());
    ASSERT_NE(dynamic_cast<genmodels::GenModel6*>(secondary->find("genmodel")), nullptr);
    secondary->set("mechanical_power_output", 0.0);
    secondary->set("mechanical_power_source", fullObjectName(governor));
    EXPECT_EQ(secondary->getMechanicalPowerSource(), nullptr);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(secondary->getMechanicalPowerSource(), governor);
    EXPECT_EQ(secondary->getMechanicalPowerOutput(), 0);
}

TEST_F(GeneratorTests, GenTestRemote)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_remote.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

TEST_F(GeneratorTests, GenTestRemoteB)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_remote_b.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
}

TEST_F(GeneratorTests, GenTestRemote2)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_dualremote.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::POWERFLOW_COMPLETE);
}

TEST_F(GeneratorTests, GenTestRemote2B)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_gen_dualremote_b.xml");
    detailedStageCheck(fileName, GridDynSimulation::GridState::DYNAMIC_INITIALIZED);
}

#ifdef ENABLE_EXPERIMENTAL_TEST_CASES
TEST_F(GeneratorTests, GenTestIsoc)
{
    std::string fileName = std::string(GEN_TEST_DIRECTORY "test_isoc2.xml");

    gds = readSimXMLFile(fileName);

    gds->set("recorddirectory", GEN_TEST_DIRECTORY);

    gds->run();

    std::string recname = std::string(GEN_TEST_DIRECTORY "datafile.dat");
    TimeSeriesMulti<> ts3(recname);
    ASSERT_GT(ts3.size(), 30u);
    EXPECT_LT(ts3.data(0, 30), 0.995);
    EXPECT_GT(ts3[0].back(), 1.0);

    EXPECT_GT(ts3.data(1, 0) - ts3[1].back(), 0.199);
    remove(recname.c_str());
}
#endif
