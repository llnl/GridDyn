/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/loads/CompositeLoad.h"
#include "griddyn/loads/MotorLoadDQ.h"
#include "griddyn/simulation/Diagnostics.h"
#include "utilities/MatrixDataSparse.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>

using namespace griddyn;
using griddyn::loads::MotorLoadDQ;

namespace {
class MotorDQProbe: public MotorLoadDQ {
  public:
    MotorDQProbe()
    {
        loadStateSizes(cDaeSolverMode);
        setOffset(0, cDaeSolverMode);
    }
    double frequencyBase() const { return systemBaseFrequency; }
};

std::unique_ptr<GridDynSimulation> readMotorCase()
{
    return readSimXMLFile(std::string{GRIDDYN_TEST_DIRECTORY} + "/load_tests/motor_dq.xml");
}

std::unique_ptr<GridDynSimulation> makeFixedVoltageCase(MotorLoadDQ*& motor)
{
    auto simulation = readMotorCase();
    motor = dynamic_cast<MotorLoadDQ*>(simulation->getBus(1)->getLoad());
    return simulation;
}
}  // namespace

TEST(MotorDQTests, MagneticAndMechanicalEnergyBalanceIncludesStatorStorage)
{
    MotorDQProbe motor;
    motor.set("load_torque", 0.6);
    motor.set("damping", 0.04);
    const std::array<double, 5> state{0.20, -0.91, 0.15, -0.82, 0.83};
    const IOdata inputs{0.92, 0.17, 1.0};
    std::array<double, 5> derivative{};
    motor.derivative(inputs,
                     StateData{0.0, state.data(), derivative.data()},
                     derivative.data(),
                     cDaeSolverMode);

    // Recover currents by independently solving the inductance relation.
    const double statorReactance = motor.get("xls") + motor.get("xm");
    const double rotorReactance = motor.get("xlr") + motor.get("xm");
    const double mutual = motor.get("xm");
    const double determinant = statorReactance * rotorReactance - mutual * mutual;
    std::array<double, 4> current{};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        current[axis] = (rotorReactance * state[axis] - mutual * state[axis + 2]) / determinant;
        current[axis + 2] =
            (statorReactance * state[axis + 2] - mutual * state[axis]) / determinant;
    }
    double magneticPower = 0.0;
    for (std::size_t index = 0; index < current.size(); ++index) {
        magneticPower += current[index] * derivative[index] / motor.frequencyBase();
    }
    const double copperLoss =
        motor.get("rs") * (current[0] * current[0] + current[1] * current[1]) +
        motor.get("rr") * (current[2] * current[2] + current[3] * current[3]);
    const double mechanicalStorage = 2.0 * motor.get("h") * state[4] * derivative[4];
    const double shaftPower = 0.6 * state[4] * state[4] * state[4];
    const double frictionPower = 0.04 * state[4] * state[4];
    const double terminalPower =
        motor.getRealPower(inputs, StateData{0.0, state.data(), derivative.data()}, cDaeSolverMode);
    EXPECT_NEAR(terminalPower,
                copperLoss + magneticPower + mechanicalStorage + shaftPower + frictionPower,
                1e-11);
    EXPECT_GT(std::abs(derivative[0]) + std::abs(derivative[1]), 1.0);
}

TEST(MotorDQTests, AnalyticStateAndTerminalJacobiansAtPerturbedOperatingPoint)
{
    MotorDQProbe motor;
    motor.set("load_torque", 0.6);
    motor.set("torque_linear", 0.15);
    motor.set("damping", 0.04);
    const std::array<double, 5> state{0.20, -0.91, 0.15, -0.82, 0.83};
    const IOdata inputs{0.92, 0.17, 1.0};
    const IOlocs inputLocations{5, 6, kNullLocation};
    const std::array<double, 5> stateDerivative{0.01, 0.02, -0.01, 0.03, -0.02};
    StateData stateData{0.0, state.data(), stateDerivative.data()};
    stateData.cj = 2.3;
    MatrixDataSparse<double> jacobian;
    MatrixDataSparse<double> outputJacobian;
    motor.jacobianElements(inputs, stateData, jacobian, inputLocations, cDaeSolverMode);
    motor.outputPartialDerivatives(inputs, stateData, outputJacobian, cDaeSolverMode);
    motor.ioPartialDerivatives(inputs, stateData, outputJacobian, inputLocations, cDaeSolverMode);
    jacobian.compact();
    outputJacobian.compact();
    constexpr double perturbation = 1e-6;
    for (std::size_t column = 0; column < 7; ++column) {
        auto plusState = state;
        auto minusState = state;
        auto plusDerivative = stateDerivative;
        auto minusDerivative = stateDerivative;
        auto plusInputs = inputs;
        auto minusInputs = inputs;
        if (column < state.size()) {
            plusState[column] += perturbation;
            minusState[column] -= perturbation;
            plusDerivative[column] += stateData.cj * perturbation;
            minusDerivative[column] -= stateData.cj * perturbation;
        } else {
            plusInputs[column - state.size()] += perturbation;
            minusInputs[column - state.size()] -= perturbation;
        }
        const StateData plusData{0.0, plusState.data(), plusDerivative.data()};
        const StateData minusData{0.0, minusState.data(), minusDerivative.data()};
        std::array<double, 5> plusResidual{};
        std::array<double, 5> minusResidual{};
        motor.residual(plusInputs, plusData, plusResidual.data(), cDaeSolverMode);
        motor.residual(minusInputs, minusData, minusResidual.data(), cDaeSolverMode);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(column)),
                        (plusResidual[row] - minusResidual[row]) / (2.0 * perturbation),
                        2e-6);
        }
        EXPECT_NEAR(outputJacobian.at(POUT_LOCATION, static_cast<index_t>(column)),
                    (motor.getRealPower(plusInputs, plusData, cDaeSolverMode) -
                     motor.getRealPower(minusInputs, minusData, cDaeSolverMode)) /
                        (2.0 * perturbation),
                    1e-8);
        EXPECT_NEAR(outputJacobian.at(QOUT_LOCATION, static_cast<index_t>(column)),
                    (motor.getReactivePower(plusInputs, plusData, cDaeSolverMode) -
                     motor.getReactivePower(minusInputs, minusData, cDaeSolverMode)) /
                        (2.0 * perturbation),
                    1e-8);
    }
}

TEST(MotorDQTests, NativeXmlInitializesAgainstTheSingleCageEquivalentCircuit)
{
    auto simulation = readMotorCase();
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    auto* bus = simulation->getBus(1);
    ASSERT_NE(bus, nullptr);
    auto* motor = dynamic_cast<MotorLoadDQ*>(bus->getLoad());
    ASSERT_NE(motor, nullptr);
    const double slip = motor->get("slip");
    ASSERT_GT(slip, 0.0);
    ASSERT_LT(slip, 0.1);
    const std::complex<double> magnetizing{0.0, motor->get("xm")};
    const std::complex<double> rotor{motor->get("rr") / slip, motor->get("xlr")};
    const auto impedance = std::complex<double>{motor->get("rs"), motor->get("xls")} +
        magnetizing * rotor / (magnetizing + rotor);
    const auto power = bus->getVoltage() * bus->getVoltage() / std::conj(impedance);
    EXPECT_NEAR(motor->getRealPower(), motor->get("scale") * power.real(), 1e-9);
    EXPECT_NEAR(motor->getReactivePower(), motor->get("scale") * power.imag(), 1e-9);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.03), 0);
    EXPECT_GT(motor->getRealPower(), 0.0);
}

TEST(MotorDQTests, StartingBuildsFluxAndAcceleratesFromRest)
{
    MotorLoadDQ* motor = nullptr;
    auto simulation = makeFixedVoltageCase(motor);
    motor->set("initialization", "starting");
    motor->set("load_torque", 0.1);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_NEAR(motor->get("speed"), 0.0, 1e-12);
    EXPECT_NEAR(motor->getRealPower(), 0.0, 1e-12);
    ASSERT_EQ(simulation->run(0.2), 0);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
    EXPECT_GT(motor->get("speed"), 0.01);
    EXPECT_GT(motor->getRealPower(), 0.0);
    for (double value : simulation->getState(cDaeSolverMode))
        EXPECT_TRUE(std::isfinite(value));
}

TEST(MotorDQTests, VoltageSagDeceleratesAndRecoveryReaccelerates)
{
    MotorLoadDQ* motor = nullptr;
    auto simulation = makeFixedVoltageCase(motor);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    const double initialSpeed = motor->get("speed");
    ASSERT_GT(initialSpeed, 0.9);
    simulation->getBus(0)->set("voltage", 0.65);
    ASSERT_EQ(simulation->run(0.12), 0);
    const double sagSpeed = motor->get("speed");
    EXPECT_LT(sagSpeed, initialSpeed - 0.005);
    simulation->getBus(0)->set("voltage", 1.0);
    ASSERT_EQ(simulation->run(0.5), 0);
    EXPECT_GT(motor->get("speed"), sagSpeed);
    EXPECT_GT(motor->getRealPower(), 0.0);
}

TEST(MotorDQTests, PartitionedModesUseFluxStatesForNetworkPowerAndDerivatives)
{
    auto simulation = readMotorCase();
    simulation->set("dynamicsolvermethod", "partitioned");
    simulation->set("defdyndiff", "basicode");
    simulation->set("timestep", 0.0001);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    ASSERT_EQ(simulation->run(0.001), 0);
    const auto algMode = simulation->getSolverMode("dynalg");
    const auto diffMode = simulation->getSolverMode("dyndiff");
    EXPECT_EQ(runResidualCheck(simulation, algMode, false), 0);
    EXPECT_EQ(runResidualCheck(simulation, diffMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, algMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, diffMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(simulation, diffMode, false), 0);
    ASSERT_EQ(simulation->run(0.01), 0);
}

TEST(MotorDQTests, CompositeAllocationAndCloningPreserveMotorScale)
{
    auto simulation =
        readSimXMLFile(std::string{GRIDDYN_TEST_DIRECTORY} + "/load_tests/motor_dq_composite.xml");
    auto* composite = dynamic_cast<loads::CompositeLoad*>(simulation->getBus(1)->getLoad());
    ASSERT_NE(composite, nullptr);
    auto* motor = dynamic_cast<MotorLoadDQ*>(composite->component(0));
    ASSERT_NE(motor, nullptr);
    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_NEAR(motor->get("p"), 0.32, 1e-12);
    EXPECT_NEAR(motor->get("scale"), 0.32 / motor->get("load_factor"), 1e-12);
    EXPECT_NEAR(motor->getRealPower(), 0.32, 0.005);
    EXPECT_GT(motor->getReactivePower(), 0.0);
    EXPECT_EQ(runResidualCheck(simulation, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(simulation, cDaeSolverMode, false), 0);
    ASSERT_EQ(simulation->run(0.01), 0);
    std::unique_ptr<CoreObject> copiedObject{motor->clone()};
    auto* copied = dynamic_cast<MotorLoadDQ*>(copiedObject.get());
    ASSERT_NE(copied, nullptr);
    EXPECT_DOUBLE_EQ(copied->get("speed"), motor->get("speed"));
    copied->set("load_torque", 0.2);
    EXPECT_NE(copied->get("load_torque"), motor->get("load_torque"));
}

TEST(MotorDQTests, InvalidParametersAndInfeasibleOperatingPointsAreRejected)
{
    MotorLoadDQ motor;
    EXPECT_THROW(motor.set("rs", 0.0), InvalidParameterValue);
    EXPECT_THROW(motor.set("h", -1.0), InvalidParameterValue);
    EXPECT_THROW(motor.set("xm", std::numeric_limits<double>::infinity()), InvalidParameterValue);
    EXPECT_THROW(motor.set("rating", 0.0), InvalidParameterValue);
    EXPECT_THROW(motor.set("load_factor", 1.1), InvalidParameterValue);
    EXPECT_THROW(motor.set("initial_speed", 1.0), InvalidParameterValue);
    EXPECT_THROW(motor.set("initialization", "unknown"), InvalidParameterValue);
    MotorLoadDQ* loaded = nullptr;
    auto simulation = makeFixedVoltageCase(loaded);
    loaded->set("torque_constant", 1.0);
    loaded->set("torque_quadratic", 0.0);
    loaded->set("load_torque", 1000.0);
    EXPECT_THROW(simulation->dynInitialize(), InvalidParameterValue);
}
