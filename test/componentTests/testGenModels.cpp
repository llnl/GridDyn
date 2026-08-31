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
#include "griddyn/genmodels/GenModelClassical.h"
#include "griddyn/genmodels/GenModelGENROU.h"
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
// test case for CoreObject object

#define GENMODEL_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/genmodel_tests/"

using namespace griddyn;

namespace {
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
}  // namespace

class GenModelTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(GenModelTests, GenclsFactoryRegistration)
{
    auto cof = CoreObjectFactory::instance();
    std::unique_ptr<CoreObject> object(cof->createObject("genmodel", "gencls"));
    ASSERT_NE(object, nullptr);
    EXPECT_NE(dynamic_cast<genmodels::GenModelClassical*>(object.get()), nullptr);
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
    EXPECT_NEAR(signals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE)],
                0.8252642039696805,
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

TEST_F(GenModelTests, ModelTest3)
{
    std::string fileName = std::string(GENMODEL_TEST_DIRECTORY "test_model2.xml");

    gds = readSimXMLFile(fileName);

    gds->run();
    requireState(GridDynSimulation::GridState::DYNAMIC_COMPLETE);
}
