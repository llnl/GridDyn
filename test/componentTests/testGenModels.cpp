/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/Generator.h"
#include "griddyn/genmodels/GenModel5.h"
#include "griddyn/genmodels/GenModel5type2.h"
#include "griddyn/genmodels/GenModel8.h"
#include "griddyn/genmodels/GenModelCSVGN1.h"
#include "griddyn/genmodels/GenModelClassical.h"
#include "griddyn/genmodels/GenModelGENROE.h"
#include "griddyn/genmodels/GenModelGENROU.h"
#include "griddyn/genmodels/GenModelGENSAE.h"
#include "griddyn/genmodels/GenModelGENSAL.h"
#include "griddyn/genmodels/GenModelGENTPJ.h"
#include "utilities/MatrixDataSparse.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
// test case for CoreObject object

#define GENMODEL_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/genmodel_tests/"

using namespace griddyn;

namespace {
void checkModelJacobians(GenModel& model, const IOdata& inputs, const std::vector<double>& state)
{
    model.setOffset(0, cDaeSolverMode);
    std::vector<double> rates(state.size(), 0.0);
    index_t sequence = 100;
    auto residual = [&](const IOdata& testInputs, const std::vector<double>& testState) {
        std::vector<double> result(state.size(), 0.0);
        StateData data(0.0, testState.data(), rates.data(), ++sequence);
        data.stateSize = static_cast<count_t>(state.size());
        model.residual(testInputs, data, result.data(), cDaeSolverMode);
        return result;
    };
    IOlocs inputLocations(inputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        inputLocations[index] = static_cast<index_t>(state.size() + index);
    }
    StateData data(0.0, state.data(), rates.data(), ++sequence);
    data.stateSize = static_cast<count_t>(state.size());
    data.cj = 0.0;
    MatrixDataSparse<double> jacobian;
    model.jacobianElements(inputs, data, jacobian, inputLocations, cDaeSolverMode);
    MatrixDataSparse<double> outputJacobian;
    model.outputPartialDerivatives(inputs, data, outputJacobian, cDaeSolverMode);
    model.ioPartialDerivatives(inputs, data, outputJacobian, inputLocations, cDaeSolverMode);
    const auto baseline = residual(inputs, state);
    StateData outputData(0.0, state.data(), rates.data(), ++sequence);
    const auto baselineOutput = model.getOutputs(inputs, outputData, cDaeSolverMode);
    constexpr double step = 1e-7;
    for (std::size_t col = 0; col < state.size() + inputs.size(); ++col) {
        auto perturbedState = state;
        auto perturbedInputs = inputs;
        if (col < state.size()) {
            perturbedState[col] += step;
        } else {
            perturbedInputs[col - state.size()] += step;
        }
        const auto perturbed = residual(perturbedInputs, perturbedState);
        for (std::size_t row = 0; row < state.size(); ++row) {
            EXPECT_NEAR(jacobian.at(static_cast<index_t>(row), static_cast<index_t>(col)),
                        (perturbed[row] - baseline[row]) / step,
                        2e-5)
                << "residual row " << row << " column " << col;
        }
        StateData perturbedData(0.0, perturbedState.data(), rates.data(), ++sequence);
        const auto perturbedOutput =
            model.getOutputs(perturbedInputs, perturbedData, cDaeSolverMode);
        for (std::size_t row = 0; row < baselineOutput.size(); ++row) {
            EXPECT_NEAR(outputJacobian.at(static_cast<index_t>(row), static_cast<index_t>(col)),
                        (perturbedOutput[row] - baselineOutput[row]) / step,
                        2e-5)
                << "output row " << row << " column " << col;
        }
    }
}

void configureKundurGenrou(genmodels::GenModelGENROU& model, double s10, double s12)
{
    // Machine-base parameters for the first generator in the ANDES
    // kundur/kundur_full.json reference case.
    model.set("h", 6.5);
    model.set("d", 0.0);
    model.set("r", 0.0);
    model.set("xl", 0.06);
    model.set("xd", 1.8);
    model.set("xq", 1.7);
    model.set("xdp", 0.3);
    model.set("xqp", 0.55);
    model.set("xdpp", 0.25);
    model.set("xqpp", 0.25);
    model.set("tdop", 8.0);
    model.set("tdopp", 0.03);
    model.set("tqop", 0.4);
    model.set("tqopp", 0.05);
    model.set("s10", s10);
    model.set("s12", s12);
}

void checkGenrouInitialization(const std::vector<double>& expectedState,
                               double expectedFieldVoltage,
                               double s10,
                               double s12)
{
    genmodels::GenModelGENROU model;
    configureKundurGenrou(model, s10, s12);
    model.dynInitializeA(0.0, 0);

    IOdata inputs(4, 0.0);
    inputs[VOLTAGE_IN_LOCATION] = 1.0000000000182052;
    inputs[ANGLE_IN_LOCATION] = 0.5702549171702084;

    // ANDES solves this case on the 100 MVA system base while the machine
    // is rated 900 MVA. GridDyn generator models operate on the machine
    // base, so the terminal P/Q references are divided by nine.
    IOdata desiredOutput(2, 0.0);
    desiredOutput[POUT_LOCATION] = 7.268029078708957 / 9.0;
    desiredOutput[QOUT_LOCATION] = 1.0946333735232774 / 9.0;
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, desiredOutput, fieldSet);

    const auto& state = model.getStates();
    ASSERT_EQ(state.size(), expectedState.size());
    for (std::size_t index = 0; index < state.size(); ++index) {
        EXPECT_NEAR(state[index], expectedState[index], 2e-8) << "state index " << index;
    }
    EXPECT_NEAR(fieldSet[genModelEftInLocation], expectedFieldVoltage, 2e-8);
    EXPECT_NEAR(fieldSet[genModelPmechInLocation], 7.268029078708958 / 9.0, 2e-8);
}

bool matchesTestModel1ActivePowerSetpoint(const std::string& modelName)
{
    // test_model1.xml schedules 1.2 pu of generator active power. CSVGN1 is
    // an SVC model and correctly injects no active power.
    return modelName != "csvgn1";
}
}  // namespace

class GenModelTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(GenModelTests, PsatFifthOrderMachinesStartAtPowerFlowWithUnequalReactances)
{
    for (bool type2 : {false, true}) {
        for (double resistance : {0.0, 0.01}) {
            SCOPED_TRACE(::testing::Message() << "type2=" << type2 << " r=" << resistance);
            std::unique_ptr<genmodels::GenModel5> model = type2 ?
                std::make_unique<genmodels::GenModel5type2>() :
                std::make_unique<genmodels::GenModel5>();
            model->set("h", 4.0);
            model->set("r", resistance);
            model->set("xl", 0.06);
            model->set("xd", 1.9);
            model->set("xq", 1.7);
            model->set("xdp", 0.3);
            model->set("xqp", 0.55);
            model->set("xdpp", 0.2);
            model->set("xqpp", 0.25);
            model->set("tdop", 8.0);
            model->set("tqop", 0.8);
            model->set("tdopp", 0.03);
            model->set("tqopp", 0.02);
            model->set("taa", 0.04);
            model->dynInitializeA(0.0, 0);
            IOdata inputs{1.02, 0.1, 0.0, 0.0};
            IOdata fieldSet(4, 0.0);
            model->dynInitializeB(inputs, {0.8, 0.2}, fieldSet);
            inputs[genModelEftInLocation] = fieldSet[genModelEftInLocation];
            inputs[genModelPmechInLocation] = fieldSet[genModelPmechInLocation];
            auto state = model->getStates();
            ASSERT_EQ(state.size(), 7U);
            EXPECT_NEAR(fieldSet[genModelPmechInLocation],
                        0.8 + (resistance * ((0.8 * 0.8) + (0.2 * 0.2)) / (1.02 * 1.02)),
                        1e-12);
            std::vector<double> derivatives(state.size(), 0.0);
            model->derivative(inputs, emptyStateData, derivatives.data(), cLocalSolverMode);
            for (std::size_t index = 2; index < derivatives.size(); ++index) {
                EXPECT_NEAR(derivatives[index], 0.0, 1e-11) << "initial derivative " << index;
            }
            // Perturb both currents, voltages, and speed to exercise the power
            // and stator Jacobians away from the power-flow operating point.
            state[0] -= 0.03;
            state[1] += 0.04;
            state[2] += 0.02;
            state[3] += 0.01;
            state[4] += 0.02;
            state[5] -= 0.02;
            checkModelJacobians(*model, inputs, state);
        }
    }
}

TEST_F(GenModelTests, GentpjDirectInitializationSatisfiesSaturatedEquilibrium)
{
    for (bool saturated : {false, true}) {
        for (const auto& power : std::array<std::array<double, 2>, 4>{
                 {{0.0, 0.0}, {0.8, 0.2}, {0.8, -0.8}, {0.1, -1.0}}}) {
            SCOPED_TRACE(::testing::Message()
                         << "saturated=" << saturated << " P=" << power[0] << " Q=" << power[1]);
            genmodels::GenModelGENTPJ model;
            model.set("r", 0.01);
            model.set("xl", 0.06);
            model.set("xd", 1.9);
            model.set("xq", 1.7);
            model.set("xdp", 0.3);
            model.set("xqp", 0.55);
            model.set("xdpp", 0.2);
            model.set("xqpp", 0.25);
            model.set("s10", saturated ? 0.1 : 0.0);
            model.set("s12", 0.3);
            model.set("kis", saturated ? 0.2 : 0.0);
            model.dynInitializeA(0.0, 0);
            IOdata inputs{1.02, 0.1, 0.0, 0.0};
            IOdata fieldSet(4, 0.0);
            model.dynInitializeB(inputs, {power[0], power[1]}, fieldSet);
            inputs[genModelEftInLocation] = fieldSet[genModelEftInLocation];
            inputs[genModelPmechInLocation] = fieldSet[genModelPmechInLocation];
            const auto state = model.getStates();
            ASSERT_EQ(state.size(), 8U);
            if (!saturated) {
                const auto voltage = std::polar(inputs[0], inputs[1]);
                const auto current = std::complex<double>(power[0], -power[1]) / std::conj(voltage);
                EXPECT_NEAR(state[2],
                            std::arg(voltage + std::complex<double>(0.01, 1.7) * current),
                            1e-12);
            }
            std::vector<double> rates(state.size(), 0.0);
            model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
            for (std::size_t index = 2; index < state.size(); ++index) {
                EXPECT_NEAR(rates[index], 0.0, 1e-10) << "initial derivative " << index;
            }
            EXPECT_NEAR(fieldSet[genModelPmechInLocation],
                        power[0] +
                            (0.01 * ((power[0] * power[0]) + (power[1] * power[1])) /
                             (inputs[0] * inputs[0])),
                        1e-12);
        }
    }
}

TEST(Csvgn1Tests, ZeroTimeConstantsInitializeAndHaveConsistentEquations)
{
    for (double time4 : {0.0, 0.1}) {
        for (double time5 : {0.0, 0.05}) {
            for (double time2 : {0.0, 0.04}) {
                if (time4 == 0.0 && time2 > 0.0) {
                    continue;
                }
                SCOPED_TRACE(::testing::Message()
                             << "T2=" << time2 << " T4=" << time4 << " T5=" << time5);
                genmodels::GenModelCSVGN1 model;
                model.set("k", 2.0);
                model.set("t1", 0.05);
                model.set("t2", time2);
                model.set("t3", 0.1);
                model.set("t4", time4);
                model.set("t5", time5);
                model.set("cbase", 80.0);
                model.set("rmin", 10.0);
                model.set("vmin", 0.1);
                model.set("vmax", 0.9);
                model.dynInitializeA(0.0, 0);
                IOdata inputs{1.0, 0.0, 0.0, 0.0};
                IOdata fieldSet(4, 0.0);
                model.dynInitializeB(inputs, {0.0, 0.2}, fieldSet);
                EXPECT_DOUBLE_EQ(fieldSet[genModelPmechInLocation], 0.0);
                auto state = model.getStates();
                ASSERT_EQ(state.size(), 3U);
                const index_t algebraicCount = (time4 == 0.0 ? 1 : 0) + (time5 == 0.0 ? 1 : 0);
                const index_t bypassedReactorIndex = time4 == 0.0 ? 1 : 0;
                const index_t reactorIndex = time5 == 0.0 ? bypassedReactorIndex : 2;
                EXPECT_NEAR(state[reactorIndex], 0.6, 1e-12);
                EXPECT_NEAR(
                    model.getOutput(inputs, emptyStateData, cLocalSolverMode, QOUT_LOCATION),
                    -0.2,
                    1e-12);
                std::vector<double> residual(3, 0.0);
                model.residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
                for (double value : residual) {
                    EXPECT_NEAR(value, 0.0, 1e-12);
                }
                EXPECT_EQ(model.getOffsets(cLocalSolverMode).local.algSize, algebraicCount);
                for (double voltage : {0.6, 1.0, 1.5}) {
                    inputs[0] = voltage;
                    checkModelJacobians(model, inputs, state);
                    std::vector<double> update(state);
                    std::vector<double> zeroRates(3, 0.0);
                    StateData data(0.0, state.data(), zeroRates.data());
                    model.algebraicUpdate(inputs, data, update.data(), cDaeSolverMode, 1.0);
                    StateData updated(0.0, update.data(), zeroRates.data());
                    model.residual(inputs, updated, residual.data(), cDaeSolverMode);
                    for (index_t index = 0; index < algebraicCount; ++index) {
                        EXPECT_NEAR(residual[index], 0.0, 1e-12);
                    }
                }
            }
        }
    }
}

TEST(Csvgn1Tests, LimitedLagHoldsItsStateAndReleasesImmediately)
{
    genmodels::GenModelCSVGN1 model;
    model.set("k", 2.0);
    model.set("t1", 0.1);
    model.set("t3", 0.1);
    model.set("t2", 0.0);
    model.set("t4", 0.2);
    model.set("vmax", 0.9);
    model.set("cbase", 80.0);
    model.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0, 0.0, 0.0};
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, {0.0, 0.2}, fieldSet);
    std::vector<double> state{0.3, 0.9, 0.6};
    std::vector<double> rates(3, 0.0);
    model.setState(0.0, state.data(), rates.data(), cLocalSolverMode);
    inputs[0] = 1.3;
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(rates[1], 0.0);
    inputs[0] = 0.9;
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    EXPECT_NEAR(rates[1], -2.5, 1e-12);
}

TEST(Csvgn1Tests, LimitedLeadLagFeedsBackTheLimitedOutput)
{
    genmodels::GenModelCSVGN1 model;
    model.set("k", 2.0);
    model.set("t1", 0.1);
    model.set("t3", 0.1);
    model.set("t2", 0.1);
    model.set("t4", 0.2);
    model.set("vmax", 0.9);
    model.set("cbase", 80.0);
    model.dynInitializeA(0.0, 0);
    IOdata inputs{1.0, 0.0, 0.0, 0.0};
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, {0.0, 0.2}, fieldSet);
    std::vector<double> state{0.3, 0.6, 0.6};
    std::vector<double> rates(3, 0.0);
    model.setState(0.0, state.data(), rates.data(), cLocalSolverMode);
    inputs[0] = 1.5;
    model.derivative(inputs, emptyStateData, rates.data(), cLocalSolverMode);
    // OpenIPSL LeadLagLim: u = clip(0.5 * 2 * 0.8 + 0.5 * 0.6) = 0.9;
    // its internal integrator follows (u - state)/T2, not the unlimited input.
    EXPECT_NEAR(rates[1], 3.0, 1e-12);
    checkModelJacobians(model, inputs, state);
}

TEST(Csvgn1Tests, RegulatorClampUsesRightSidedJacobianAtExactLimits)
{
    genmodels::GenModelCSVGN1 model;
    // Binary fractions make the initialized reference and raw output exact:
    // error = 0.25, firstOutput = 0.25, regulatorOutput = 0.5.
    model.set("k", 2.0);
    model.set("t1", 0.125);
    model.set("t2", 0.125);
    model.set("t3", 0.25);
    model.set("t4", 0.25);
    model.set("t5", 0.125);
    model.set("cbase", 75.0);
    model.set("vmin", 0.125);
    model.set("vmax", 0.875);
    model.dynInitializeA(0.0, 0);
    const IOdata inputs{1.0, 0.0, 0.0, 0.0};
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, {0.0, 0.25}, fieldSet);
    const auto state = model.getStates();
    ASSERT_EQ(state, (std::vector<double>{0.25, 0.5, 0.5}));
    model.setOffset(0, cDaeSolverMode);
    const std::vector<double> rates(state.size(), 0.0);
    StateData data(0.0, state.data(), rates.data());
    data.cj = 0.0;
    const IOlocs inputLocations{3, kNullLocation, kNullLocation, kNullLocation};
    for (bool upperLimit : {false, true}) {
        SCOPED_TRACE(upperLimit ? "upper limit" : "lower limit");
        model.set("vmin", upperLimit ? 0.125 : 0.5);
        model.set("vmax", upperLimit ? 0.5 : 0.875);
        MatrixDataSparse<double> jacobian;
        model.jacobianElements(inputs, data, jacobian, inputLocations, cDaeSolverMode);
        const double inputPartial = upperLimit ? 0.0 : 4.0;
        EXPECT_DOUBLE_EQ(jacobian.at(1, 0), inputPartial);
        EXPECT_DOUBLE_EQ(jacobian.at(1, 1), upperLimit ? -8.0 : -4.0);
        EXPECT_DOUBLE_EQ(jacobian.at(1, 3), inputPartial);
        EXPECT_DOUBLE_EQ(jacobian.at(2, 0), inputPartial);
        EXPECT_DOUBLE_EQ(jacobian.at(2, 1), inputPartial);
        EXPECT_DOUBLE_EQ(jacobian.at(2, 3), inputPartial);
        checkModelJacobians(model, inputs, state);
    }
}

TEST(Csvgn1Tests, RejectsUnrealizableLeadAndNegativeTimeConstants)
{
    for (const auto& parameters : std::array<std::array<double, 3>, 4>{
             {{0.1, 0.0, 0.05}, {0.0, -0.1, 0.05}, {0.0, 0.1, -0.05}, {-0.1, 0.1, 0.05}}}) {
        genmodels::GenModelCSVGN1 model;
        model.set("t2", parameters[0]);
        model.set("t4", parameters[1]);
        model.set("t5", parameters[2]);
        EXPECT_THROW(model.dynInitializeA(0.0, 0), InvalidParameterValue);
    }
}

TEST_F(GenModelTests, GenclsFactoryRegistration)
{
    auto cof = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(cof->createObject("genmodel", "gencls"));
    ASSERT_NE(object, nullptr);
    EXPECT_NE(dynamic_cast<genmodels::GenModelClassical*>(object.get()), nullptr);
}

TEST_F(GenModelTests, GenroeFactoryCloneAndExponentialSaturation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("genmodel", "genroe"));
    auto* genroe = dynamic_cast<genmodels::GenModelGENROE*>(object.get());
    ASSERT_NE(genroe, nullptr);
    configureKundurGenrou(*genroe, 0.10, 0.50);
    genroe->dynInitializeA(0.0, 0);

    // GENROE fits S(psi)=B psi^A, unlike GENROU's cutoff-scaled quadratic
    // curve.  Their steady-state field-voltage requirements therefore differ
    // at this nonsaturation-point operating condition while the electrical
    // state realization remains the same.
    genmodels::GenModelGENROU genrou;
    configureKundurGenrou(genrou, 0.10, 0.50);
    genrou.dynInitializeA(0.0, 0);
    IOdata initializationInputs{1.0, 0.0, 0.0, 0.0};
    IOdata initializationOutput{0.8, 0.2};
    IOdata genroeFieldSet(4, 0.0);
    IOdata genrouFieldSet(4, 0.0);
    genroe->dynInitializeB(initializationInputs, initializationOutput, genroeFieldSet);
    genrou.dynInitializeB(initializationInputs, initializationOutput, genrouFieldSet);
    EXPECT_NE(genroeFieldSet[genModelEftInLocation], genrouFieldSet[genModelEftInLocation]);

    std::unique_ptr<CoreObject> cloneObject(genroe->clone());
    auto* clone = dynamic_cast<genmodels::GenModelGENROE*>(cloneObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("s10"), 0.10);
    EXPECT_DOUBLE_EQ(clone->get("s12"), 0.50);
}

TEST_F(GenModelTests, GenroeExponentialSaturationMatchesPublishedCurve)
{
    const auto initializeAtVoltage = [](double voltage) {
        genmodels::GenModelGENROE model;
        configureKundurGenrou(model, 0.10, 0.50);
        model.dynInitializeA(0.0, 0);
        IOdata inputs{voltage, 0.0, 0.0, 0.0};
        IOdata desiredOutput{0.0, 0.0};
        IOdata fieldSet(4, 0.0);
        model.dynInitializeB(inputs, desiredOutput, fieldSet);
        return std::make_pair(model.getStates(), fieldSet[genModelEftInLocation]);
    };

    // At no load, |psi''|=VT and Efd=VT*(1+S_e(VT)). These values therefore
    // check both calibration points without reaching into model internals.
    const auto [statesAtOne, fieldAtOne] = initializeAtVoltage(1.0);
    const auto [statesAtOnePointTwo, fieldAtOnePointTwo] = initializeAtVoltage(1.2);
    EXPECT_NEAR(fieldAtOne, 1.1, 1e-13);
    EXPECT_NEAR(fieldAtOnePointTwo, 1.8, 1e-13);
    ASSERT_EQ(statesAtOne.size(), 8U);
    EXPECT_NEAR(statesAtOne[0], 0.0, 1e-13);
    EXPECT_NEAR(statesAtOne[1], 0.0, 1e-13);
    EXPECT_NEAR(statesAtOne[2], 0.0, 1e-13);
    EXPECT_NEAR(statesAtOne[3], 1.0, 1e-13);

    const double exponent = std::log(0.50 / 0.10) / std::log(1.2);
    const double expectedSaturation = 0.10 * std::pow(1.1, exponent);
    const auto [statesAtIntermediate, fieldAtIntermediate] = initializeAtVoltage(1.1);
    (void)statesAtOnePointTwo;
    (void)statesAtIntermediate;
    EXPECT_NEAR(fieldAtIntermediate, 1.1 * (1.0 + expectedSaturation), 1e-13);
}

TEST_F(GenModelTests, GensaeFactoryInitializationAndExponentialFluxSaturation)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("genmodel", "gensae"));
    auto* model = dynamic_cast<genmodels::GenModelGENSAE*>(object.get());
    ASSERT_NE(model, nullptr);
    model->set("h", 4.28);
    model->set("d", 0.0);
    model->set("r", 0.0);
    model->set("xl", 0.12);
    model->set("xd", 1.84);
    model->set("xq", 1.75);
    model->set("xdp", 0.41);
    model->set("xpp", 0.2);
    model->set("tdop", 5.0);
    model->set("tdopp", 0.07);
    model->set("tqopp", 0.09);
    model->set("s10", 0.11);
    model->set("s12", 0.39);
    model->dynInitializeA(0.0, 0);

    IOdata inputs{1.0, 0.0, 0.0, 0.0};
    IOdata fieldSet(4, 0.0);
    model->dynInitializeB(inputs, {0.0, 0.0}, fieldSet);
    ASSERT_EQ(model->getStates().size(), 7U);
    // At no load the air-gap flux is one pu. GENSAE therefore uses S10,
    // rather than GENSAL's saturation of E'q, and needs Efd=1+S10.
    EXPECT_NEAR(fieldSet[genModelEftInLocation], 1.11, 1e-13);
    EXPECT_NEAR(model->getStates()[0], 0.0, 1e-13);
    EXPECT_NEAR(model->getStates()[1], 0.0, 1e-13);
    EXPECT_NEAR(model->getStates()[2], 0.0, 1e-13);
    EXPECT_NEAR(model->getStates()[3], 1.0, 1e-13);

    std::unique_ptr<CoreObject> cloneObject(model->clone());
    auto* clone = dynamic_cast<genmodels::GenModelGENSAE*>(cloneObject.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_DOUBLE_EQ(clone->get("s10"), 0.11);
    EXPECT_DOUBLE_EQ(clone->get("s12"), 0.39);
}

TEST_F(GenModelTests, GenclsAndesInitialization)
{
    genmodels::GenModelClassical model;
    model.set("h", 3.0);
    model.set("d", 1.0);
    model.set("ra", 0.01);
    model.set("xd1", 0.23);
    model.dynInitializeA(0.0, 0);

    // This operating point is evaluated with the initialization equations in
    // ANDES GENCLS. GridDyn's Id sign is opposite to ANDES; Iq and delta have
    // the same signs.
    IOdata inputs(4, 0.0);
    inputs[VOLTAGE_IN_LOCATION] = 1.02;
    inputs[ANGLE_IN_LOCATION] = 0.1;
    IOdata desiredOutput(2, 0.0);
    desiredOutput[POUT_LOCATION] = 0.8;
    desiredOutput[QOUT_LOCATION] = 0.2;
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, desiredOutput, fieldSet);

    const std::vector<double> expectedState{-0.32208726118340464,
                                            0.7415217916050773,
                                            0.26479303757424744,
                                            1.0};
    ASSERT_EQ(model.getStates().size(), expectedState.size());
    for (std::size_t index = 0; index < expectedState.size(); ++index) {
        EXPECT_NEAR(model.getStates()[index], expectedState[index], 1e-12)
            << "state index " << index;
    }
    EXPECT_NEAR(fieldSet[genModelEftInLocation], 1.08767666283497, 1e-12);
    EXPECT_NEAR(fieldSet[genModelPmechInLocation], 0.8065359477124185, 1e-12);
}

TEST_F(GenModelTests, GenclsEquationChecks)
{
    const std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");
    gds = readSimXMLFile(fileName);
    Generator* gen = gds->getGen(0);
    ASSERT_NE(gen, nullptr);
    auto* model = new genmodels::GenModelClassical();
    gen->add(model);
    model->set("h", 3.0);
    model->set("d", 1.0);
    model->set("r", 0.01);
    model->set("x", 0.23);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runAlgebraicCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(GenModelTests, GenclsAndesPerturbedSwingEquation)
{
    genmodels::GenModelClassical model;
    model.set("h", 3.0);
    model.set("d", 1.0);
    model.set("r", 0.01);
    model.set("x", 0.23);
    model.dynInitializeA(0.0, 0);

    IOdata inputs{1.02, 0.1, 0.0, 0.0};
    IOdata desiredOutput{0.8, 0.2};
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, desiredOutput, fieldSet);

    std::vector<double> state{-0.3, 0.75, 0.3, 1.01};
    std::vector<double> stateDerivative(state.size(), 0.0);
    model.setState(0.0, state.data(), stateDerivative.data(), cLocalSolverMode);
    inputs[genModelEftInLocation] = fieldSet[genModelEftInLocation];
    inputs[genModelPmechInLocation] = 0.9;
    model.derivative(inputs, emptyStateData, stateDerivative.data(), cLocalSolverMode);

    EXPECT_NEAR(stateDerivative[2], 3.7699111843077517, 1e-12);
    EXPECT_NEAR(stateDerivative[3], 0.012373750478962074, 1e-12);
}

TEST_F(GenModelTests, GenclsZeroInertiaIsInfiniteBus)
{
    genmodels::GenModelClassical model;
    model.set("h", 0.0);
    model.set("d", 1.0);
    model.set("r", 0.0);
    model.set("x", 0.2);
    model.dynInitializeA(0.0, 0);

    IOdata inputs(4, 0.0);
    inputs[VOLTAGE_IN_LOCATION] = 1.0;
    IOdata desiredOutput(2, 0.0);
    desiredOutput[POUT_LOCATION] = 0.8;
    desiredOutput[QOUT_LOCATION] = 0.1;
    IOdata fieldSet(4, 0.0);
    model.dynInitializeB(inputs, desiredOutput, fieldSet);
    inputs[genModelEftInLocation] = fieldSet[genModelEftInLocation];
    inputs[genModelPmechInLocation] = fieldSet[genModelPmechInLocation] + 0.5;

    std::vector<double> derivative(model.getStates().size(), 1.0);
    model.derivative(inputs, emptyStateData, derivative.data(), cLocalSolverMode);
    EXPECT_DOUBLE_EQ(derivative[2], 0.0);
    EXPECT_DOUBLE_EQ(derivative[3], 0.0);
}

TEST_F(GenModelTests, GenclsRejectsInvalidParameters)
{
    genmodels::GenModelClassical model;
    model.set("h", -1.0);
    EXPECT_THROW(model.dynInitializeA(0.0, 0), InvalidParameterValue);

    model.set("h", 1.0);
    model.set("x", 0.0);
    EXPECT_THROW(model.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST_F(GenModelTests, GenrouFactoryRegistration)
{
    auto cof = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(cof->createObject("genmodel", "genrou"));
    ASSERT_NE(object, nullptr);
    EXPECT_NE(dynamic_cast<genmodels::GenModelGENROU*>(object.get()), nullptr);
}

TEST_F(GenModelTests, GensalFactoryInitializationAndEquations)
{
    auto factory = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(factory->createObject("genmodel", "gensal"));
    auto* model = dynamic_cast<genmodels::GenModelGENSAL*>(object.get());
    ASSERT_NE(model, nullptr);
    model->set("h", 4.0);
    model->set("d", 0.0);
    model->set("r", 0.0);
    model->set("xl", 0.12);
    model->set("xd", 1.41);
    model->set("xq", 1.35);
    model->set("xdp", 0.30);
    model->set("xpp", 0.20);
    model->set("tdop", 5.0);
    model->set("tdopp", 0.05);
    model->set("tqopp", 0.10);
    model->set("s10", 0.1);
    model->set("s12", 0.5);
    model->dynInitializeA(0.0, 0);

    IOdata inputs{1.0, 0.070620673811798, 0.0, 0.0};
    IOdata desiredOutput{0.8, 0.2};
    IOdata fieldSet(4, 0.0);
    model->dynInitializeB(inputs, desiredOutput, fieldSet);
    inputs[genModelEftInLocation] = fieldSet[genModelEftInLocation];
    inputs[genModelPmechInLocation] = fieldSet[genModelPmechInLocation];
    std::vector<double> residual(model->getStates().size(), 0.0);
    model->residual(inputs, emptyStateData, residual.data(), cLocalSolverMode);
    for (double value : residual) {
        EXPECT_NEAR(value, 0.0, 2e-10);
    }

    std::unique_ptr<CoreObject> cloned(model->clone());
    ASSERT_NE(dynamic_cast<genmodels::GenModelGENSAL*>(cloned.get()), nullptr);
}

TEST_F(GenModelTests, GensalAnalyticJacobianMatchesFiniteDifferences)
{
    gds = readSimXMLFile(std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml"));
    auto* generator = gds->getGen(0);
    ASSERT_NE(generator, nullptr);
    auto* model = new genmodels::GenModelGENSAL();
    model->set("h", 4.0);
    model->set("xl", 0.12);
    model->set("xd", 1.41);
    model->set("xq", 1.35);
    model->set("xdp", 0.30);
    model->set("xpp", 0.20);
    model->set("tdop", 5.0);
    model->set("tdopp", 0.05);
    model->set("tqopp", 0.10);
    model->set("s10", 0.1);
    model->set("s12", 0.5);
    generator->add(model);
    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(GenModelTests, GenModel8AlgebraicUpdateMatchesLeakageAwareResidual)
{
    gds = readSimXMLFile(std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml"));
    auto* generator = gds->getGen(0);
    ASSERT_NE(generator, nullptr);

    auto* model = new genmodels::GenModel8();
    model->set("h", 4.0);
    model->set("d", 0.04);
    model->set("r", 0.001);
    model->set("xl", 0.12);
    model->set("xd", 1.60);
    model->set("xq", 1.55);
    model->set("xdp", 0.32);
    model->set("xqp", 0.55);
    model->set("xdpp", 0.22);
    model->set("xqpp", 0.25);
    model->set("tdop", 5.0);
    model->set("tqop", 0.70);
    model->set("tdopp", 0.05);
    model->set("tqopp", 0.10);
    generator->add(model);

    ASSERT_EQ(gds->dynInitialize(), 0);

    std::vector<double> state = gds->getState(cDaeSolverMode);
    std::vector<double> stateDerivative(state.size(), 0.0);
    const auto& modelOffsets = model->getOffsets(cDaeSolverMode);
    IOdata inputs{1.0, 0.0, 2.0, 0.8};
    StateData stateData(0.0, state.data(), stateDerivative.data(), 12345);

    std::vector<double> algebraicUpdate(state);
    model->algebraicUpdate(inputs, stateData, algebraicUpdate.data(), cDaeSolverMode, 1.0);

    std::vector<double> residual(state.size(), 0.0);
    StateData updatedStateData(0.0, algebraicUpdate.data(), stateDerivative.data(), 12346);
    model->residual(inputs, updatedStateData, residual.data(), cDaeSolverMode);

    EXPECT_NEAR(residual[modelOffsets.algOffset], 0.0, 1e-12);
    EXPECT_NEAR(residual[modelOffsets.algOffset + 1], 0.0, 1e-12);
}

TEST_F(GenModelTests, GenrouRejectsInvalidParameters)
{
    genmodels::GenModelGENROU model;
    configureKundurGenrou(model, 0.0, 1.0);
    model.set("xdp", 0.2);
    model.set("xdpp", 0.25);

    EXPECT_THROW(model.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST_F(GenModelTests, GenrouAcceptsZeroQuadratureTransientTimeConstant)
{
    genmodels::GenModelGENROU model;
    configureKundurGenrou(model, 0.0, 1.0);

    // PSS/E represents the absent q-axis transient reactance with Xq == Xqp
    // and Tqop == 0.  Initialization replaces the zero time constant with a
    // short compatibility value so the retained state remains well-defined.
    model.set("xq", 0.55);
    model.set("tqop", 0.0);

    EXPECT_NO_THROW(model.dynInitializeA(0.0, 0));

    IOdata inputs(4, 0.0);
    inputs[VOLTAGE_IN_LOCATION] = 1.0;
    IOdata desiredOutput(2, 0.0);
    IOdata fieldSet(4, 0.0);
    EXPECT_NO_THROW(model.dynInitializeB(inputs, desiredOutput, fieldSet));
}

TEST_F(GenModelTests, GenrouAcceptsAndesLeakageReactanceWarningRange)
{
    genmodels::GenModelGENROU model;
    configureKundurGenrou(model, 0.0, 1.0);

    // The IEEE 39-bus DYR case uses xl > xpp. ANDES warns about this input
    // relationship but initializes the machine because its equations remain
    // well-defined.
    model.set("xl", 0.35);
    model.set("xdpp", 0.2);
    model.set("xqpp", 0.2);

    EXPECT_NO_THROW(model.dynInitializeA(0.0, 0));
}

TEST_F(GenModelTests, GenrouRejectsSingularReactanceCoefficient)
{
    genmodels::GenModelGENROU model;
    configureKundurGenrou(model, 0.0, 1.0);
    model.set("xl", 0.3);

    EXPECT_THROW(model.dynInitializeA(0.0, 0), InvalidParameterValue);
}

TEST_F(GenModelTests, GenrouAndesInitialization)
{
    // Captured from ANDES kundur_full initialization. ANDES Id, e1d, and e2q
    // are sign-adjusted to GridDyn's established dq convention; currents are
    // converted from the 100 MVA system base to the 900 MVA machine base.
    checkGenrouInitialization({-6.181548626591364 / 9.0,
                               3.9762954475880385 / 9.0,
                               1.419948332280109,
                               1.0,
                               -0.5080821960806938,
                               0.8662650669828909,
                               -0.7245693926715981,
                               0.7014237702737878},
                              1.8965231714147848,
                              0.0,
                              1.0);
}

TEST_F(GenModelTests, GenrouAndesSaturatedInitialization)
{
    // Same ANDES operating point with S10=0.1 and S12=0.3. This exercises the
    // ExcQuadSat-compatible initialization and saturation-dependent states.
    checkGenrouInitialization({-5.998928215369296 / 9.0,
                               4.246802229474443 / 9.0,
                               1.375539180362926,
                               1.0,
                               -0.46150083698625766,
                               0.8928705752592525,
                               // Captured ANDES state, not an approximation of ln(2).
                               // NOLINTNEXTLINE(modernize-use-std-numbers)
                               -0.6927156250354218,
                               0.7328991561827378},
                              2.015401860073227,
                              0.1,
                              0.3);
}

TEST_F(GenModelTests, GenrouAndesPerturbedDerivatives)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");
    gds = readSimXMLFile(fileName);
    Generator* gen = gds->getGen(0);
    ASSERT_NE(gen, nullptr);
    auto* model = new genmodels::GenModelGENROU();
    configureKundurGenrou(*model, 0.1, 0.3);
    gen->add(model);
    ASSERT_EQ(gds->dynInitialize(), 0);

    // Values below are the GridDyn-sign-convention equivalent of a direct
    // evaluation of the ANDES GENROU equations away from equilibrium.
    std::vector<double> state = gds->getState(cDaeSolverMode);
    const auto& modelOffsets = model->getOffsets(cDaeSolverMode);
    state[modelOffsets.algOffset] = -0.65;
    state[modelOffsets.algOffset + 1] = 0.5;
    state[modelOffsets.diffOffset] = 1.4;
    state[modelOffsets.diffOffset + 1] = 1.002;
    state[modelOffsets.diffOffset + 2] = -0.45;
    state[modelOffsets.diffOffset + 3] = 0.9;
    state[modelOffsets.diffOffset + 4] = -0.70;
    state[modelOffsets.diffOffset + 5] = 0.75;
    IOdata inputs{1.01, 0.57, 2.0, 0.82};
    std::vector<double> derivative(state.size(), 0.0);
    StateData stateData(0.0, state.data(), nullptr, 12345);
    std::vector<double> algebraicUpdate(state);
    model->algebraicUpdate(inputs, stateData, algebraicUpdate.data(), cDaeSolverMode, 1.0);
    model->derivative(inputs, stateData, derivative.data(), cDaeSolverMode);

    const std::vector<double> expected{0.75398223686155097,
                                       -0.00040493876689850193,
                                       -0.11774384280401951,
                                       0.00034563024648560925,
                                       0.099999999999998423,
                                       -0.19999999999999926};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_NEAR(derivative[modelOffsets.diffOffset + index], expected[index], 2e-12)
            << "differential state index " << index;
    }
}

TEST_F(GenModelTests, GenrouExposesAndesControllerSignals)
{
    const std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");
    gds = readSimXMLFile(fileName);
    Generator* gen = gds->getGen(0);
    ASSERT_NE(gen, nullptr);
    auto* model = new genmodels::GenModelGENROU();
    configureKundurGenrou(*model, 0.0, 1.0);
    model->set("r", 0.02);
    gen->add(model);
    ASSERT_EQ(gds->dynInitialize(), 0);

    // GridDyn state order is [Id, Iq, delta, omega, e1d, e1q, e2q, e2d].
    std::vector<double> state = gds->getState(cDaeSolverMode);
    const auto& modelOffsets = model->getOffsets(cDaeSolverMode);
    state[modelOffsets.algOffset] = -0.65;
    state[modelOffsets.algOffset + 1] = 0.5;
    state[modelOffsets.diffOffset] = 1.4;
    state[modelOffsets.diffOffset + 1] = 1.002;
    state[modelOffsets.diffOffset + 2] = -0.45;
    state[modelOffsets.diffOffset + 3] = 0.9;
    state[modelOffsets.diffOffset + 4] = -0.70;
    state[modelOffsets.diffOffset + 5] = 0.75;
    const StateData stateData(0.0, state.data(), nullptr, 9001);
    const IOdata inputs{1.01, 0.57, 2.0, 0.82};
    const auto signals = model->getMachineControllerSignals(inputs, stateData, cDaeSolverMode);

    EXPECT_DOUBLE_EQ(signals[static_cast<index_t>(MachineControllerSignal::ID)], -0.65);
    EXPECT_DOUBLE_EQ(signals[static_cast<index_t>(MachineControllerSignal::IQ)], 0.5);
    EXPECT_NEAR(signals[static_cast<index_t>(MachineControllerSignal::VD)],
                -0.7453106848210623,
                1e-13);
    EXPECT_NEAR(signals[static_cast<index_t>(MachineControllerSignal::VQ)],
                0.6816245176719798,
                1e-13);
    EXPECT_NEAR(signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)],
                0.8252642039696805,
                1e-13);
    EXPECT_NEAR(signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE)],
                0.8387142039696805,
                1e-13);
    EXPECT_NEAR(signals[static_cast<index_t>(MachineControllerSignal::XADIFD)], 1.8671875, 1e-13);
}

TEST_F(GenModelTests, GenrouSaturatedEquationChecks)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");
    gds = readSimXMLFile(fileName);

    Generator* gen = gds->getGen(0);
    ASSERT_NE(gen, nullptr);
    auto* model = new genmodels::GenModelGENROU();
    configureKundurGenrou(*model, 0.1, 0.3);
    gen->add(model);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runAlgebraicCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(GenModelTests, GenroeSaturatedEquationAndJacobianChecks)
{
    const std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");
    gds = readSimXMLFile(fileName);

    Generator* gen = gds->getGen(0);
    ASSERT_NE(gen, nullptr);
    auto* model = new genmodels::GenModelGENROE();
    configureKundurGenrou(*model, 0.1, 0.3);
    gen->add(model);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runAlgebraicCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(GenModelTests, GensaeSaturatedEquationAndJacobianChecks)
{
    const std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");
    gds = readSimXMLFile(fileName);
    Generator* gen = gds->getGen(0);
    ASSERT_NE(gen, nullptr);
    auto* model = new genmodels::GenModelGENSAE();
    model->set("h", 4.28);
    model->set("d", 0.0);
    model->set("r", 0.0);
    model->set("xl", 0.12);
    model->set("xd", 1.84);
    model->set("xq", 1.75);
    model->set("xdp", 0.41);
    model->set("xpp", 0.2);
    model->set("tdop", 5.0);
    model->set("tdopp", 0.07);
    model->set("tqopp", 0.09);
    model->set("s10", 0.11);
    model->set("s12", 0.39);
    gen->add(model);

    ASSERT_EQ(gds->dynInitialize(), 0);
    EXPECT_EQ(runResidualCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runAlgebraicCheck(gds, cDaeSolverMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, cDaeSolverMode, false), 0);
}

TEST_F(GenModelTests, ModelTest1)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    gds = readSimXMLFile(fileName);

    int retval = gds->dynInitialize();
    EXPECT_EQ(retval, 0);
    requireState(GridDynSimulation::GridState::DYNAMIC_INITIALIZED);

    std::vector<double> initialState = gds->getState();
    runResidualCheck(gds, cDaeSolverMode);
    // gds->saveJacobian(std::string(GENMODEL_TEST_DIRECTORY "mjac5.bin"));
    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
    std::vector<double> finalState = gds->getState();

    auto cdiff = gmlc::utilities::countDiffs(initialState, finalState, 0.001, 0.01);

    EXPECT_EQ(cdiff, 0U);
}

TEST_F(GenModelTests, ModelTest2)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    auto cof = CoreObjectFactory::instance();
    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        if (!matchesTestModel1ActivePowerSetpoint(gname)) {
            continue;
        }
        // skip any fmi model
        if (gname.starts_with("fmi")) {
            continue;
        }
        gds = readSimXMLFile(fileName);

        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);

        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr) << "Failed to create model " << gname;
        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Model " << gname << " dynInitialize issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " residual issue";
        mmatch = runJacobianCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian issue";
        mmatch = runDerivativeCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " derivative issue";
        mmatch = runAlgebraicCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " algebraic issue";
    }
}

TEST_F(GenModelTests, ModelTest2WithR)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    auto cof = CoreObjectFactory::instance();
    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        if (!matchesTestModel1ActivePowerSetpoint(gname)) {
            continue;
        }
        if (gname.starts_with("fmi")) {
            continue;
        }
        gds = readSimXMLFile(fileName);

        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr) << "Failed to create model " << gname;
        // just set the resistance to make sure the models can handle that parameter
        obj->set("r", 0.001);
        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Model " << gname << " dynInitialize r issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " residual r issue";
        mmatch = runJacobianCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian r issue";
    }
}

#ifdef GRIDDYN_ENABLE_CVODE
TEST_F(GenModelTests, ModelTest2AlgDiffTests)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml");

    auto cof = CoreObjectFactory::instance();

    auto genlist = cof->getTypeNames("genmodel");

    for (auto& gname : genlist) {
        if (!matchesTestModel1ActivePowerSetpoint(gname)) {
            continue;
        }
        if (gname.starts_with("fmi")) {
            continue;
        }
        gds = readSimXMLFile(fileName);

        Generator* gen = gds->getGen(0);
        ASSERT_NE(gen, nullptr);
        auto obj = cof->createObject("genmodel", gname);
        ASSERT_NE(obj, nullptr) << "Failed to create model " << gname;
        // just set the resistance to make sure the models can handle that parameter
        obj->set("r", 0.001);
        gen->add(obj);

        int retval = gds->dynInitialize();

        EXPECT_EQ(retval, 0) << "Model " << gname << " dynInitialize issue";
        auto mmatch = runResidualCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " residual issue";
        mmatch = runDerivativeCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " derivative issue";
        mmatch = runAlgebraicCheck(gds, cDaeSolverMode, false);
        ASSERT_EQ(mmatch, 0) << "Model " << gname << " algebraic issue";
        if (gds->diffSize(cDaeSolverMode) > 0) {
            mmatch = runJacobianCheck(gds, cDynDiffSolverMode, false);
            ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian dynDiff issue";
            mmatch = runJacobianCheck(gds, cDynAlgSolverMode, false);
            ASSERT_EQ(mmatch, 0) << "Model " << gname << " Jacobian dynAlg issue";
        }
    }
}
#endif

TEST_F(GenModelTests, PartitionedGeneratorExciterFieldJacobianMatchesFiniteDifferences)
{
    gds = readSimXMLFile(std::string(GENMODEL_TEST_DIRECTORY "test_model1.xml"));
    gds->set("dynamicsolvermethod", "partitioned");
    gds->set("defdyndiff", "basicode");
    gds->set("timestep", 0.005);

    ASSERT_EQ(gds->dynInitialize(), 0);
    ASSERT_EQ(gds->run(0.005), 0);
    const auto diffMode = gds->getSolverMode("dyndiff");
    EXPECT_EQ(runResidualCheck(gds, diffMode, false), 0);
    EXPECT_EQ(runDerivativeCheck(gds, diffMode, false), 0);
    EXPECT_EQ(runJacobianCheck(gds, diffMode, false), 0);
}

TEST_F(GenModelTests, ModelTest3)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model2.xml");

    gds = readSimXMLFile(fileName);

    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}
