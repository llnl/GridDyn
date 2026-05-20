/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "griddyn/Load.h"
#include "griddyn/simulation/Diagnostics.h"
#include "gtestHelper.h"
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>

using namespace griddyn;

namespace {
class GriddynGlobalEnvironment final: public ::testing::Environment {
  public:
    void TearDown() override
    {
#ifdef _CRTDBG_MAP_ALLOC
        _CrtDumpMemoryLeaks();
#endif
    }
};

::testing::Environment* const registeredEnvironment =
    ::testing::AddGlobalTestEnvironment(new GriddynGlobalEnvironment{});
}  // namespace

GridDynSimulationTestFixture::GridDynSimulationTestFixture()
{
    readerConfig::setPrintMode(0);
}

GridDynSimulationTestFixture::~GridDynSimulationTestFixture() = default;

void GridDynSimulationTestFixture::checkState(griddyn::GridDynSimulation::gridState_t state)
{
    EXPECT_EQ(to_string(gds->currentProcessState()), to_string(state));
}

void GridDynSimulationTestFixture::requireState(griddyn::GridDynSimulation::gridState_t state)
{
    ASSERT_EQ(to_string(gds->currentProcessState()), to_string(state));
}

void GridDynSimulationTestFixture::checkState2(griddyn::GridDynSimulation::gridState_t state)
{
    EXPECT_EQ(to_string(gds2->currentProcessState()), to_string(state));
}

void GridDynSimulationTestFixture::requireState2(griddyn::GridDynSimulation::gridState_t state)
{
    ASSERT_EQ(to_string(gds2->currentProcessState()), to_string(state));
}

void checkStates(griddyn::GridDynSimulation::gridState_t state1,
                 griddyn::GridDynSimulation::gridState_t state2)
{
    EXPECT_EQ(to_string(state1), to_string(state2));
}

void requireStates(griddyn::GridDynSimulation::gridState_t state1,
                   griddyn::GridDynSimulation::gridState_t state2)
{
    ASSERT_EQ(to_string(state1), to_string(state2));
}

static const char startupString[] = "startup";
static const char initializedString[] = "initialized";
static const char pflowString[] = "powerflow_complete";
static const char dinitString[] = "dynamic init";
static const char dcompString[] = "dynamic complete";
static const char dpartString[] = "dynamic partial";
static const char errorString[] = "error";
static const char haltedString[] = "halted";
static const char ukString[] = "unknown";
static const std::string startupStringRef(startupString);
static const std::string initializedStringRef(initializedString);
static const std::string pflowStringRef(pflowString);
static const std::string dinitStringRef(dinitString);
static const std::string dcompStringRef(dcompString);
static const std::string dpartStringRef(dpartString);
static const std::string errorStringRef(errorString);
static const std::string haltedStringRef(haltedString);
static const std::string ukStringRef(ukString);
const std::string& to_string(griddyn::GridDynSimulation::gridState_t state)
{
    switch (state) {
        case GridDynSimulation::gridState_t::STARTUP:
            return startupStringRef;
        case GridDynSimulation::gridState_t::INITIALIZED:
            return initializedStringRef;
        case GridDynSimulation::gridState_t::POWERFLOW_COMPLETE:
            return pflowStringRef;
        case GridDynSimulation::gridState_t::DYNAMIC_INITIALIZED:
            return dinitStringRef;
        case GridDynSimulation::gridState_t::DYNAMIC_PARTIAL:
            return dpartStringRef;
        case GridDynSimulation::gridState_t::DYNAMIC_COMPLETE:
            return dcompStringRef;
        case GridDynSimulation::gridState_t::GD_ERROR:
            return errorStringRef;
        case GridDynSimulation::gridState_t::HALTED:
            return haltedStringRef;
        default:
            return ukStringRef;
    }
}
std::ostream& operator<<(std::ostream& os, griddyn::GridDynSimulation::gridState_t state)
{
    os << to_string(state);
    return os;
}

void GridDynSimulationTestFixture::simpleRunTestXML(const std::string& fileName)
{
    runTestXML(fileName, GridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
}

void GridDynSimulationTestFixture::runTestXML(const std::string& fileName,
                                              GridDynSimulation::gridState_t finalState)
{
    gds = readSimXMLFile(fileName);
    gds->consolePrintLevel = print_level::no_print;
    gds->run();
    requireState(finalState);
}

void GridDynSimulationTestFixture::simpleStageCheck(const std::string& fileName,
                                                    GridDynSimulation::gridState_t finalState)
{
    readerConfig::setPrintMode(0);
    int retval = 0;
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(readerConfig::warnCount, 0);
    requireState(GridDynSimulation::gridState_t::STARTUP);
    switch (finalState) {
        case GridDynSimulation::gridState_t::STARTUP:
            return;
        case GridDynSimulation::gridState_t::INITIALIZED:
            retval = gds->pFlowInitialize();
            EXPECT_EQ(retval, 0);
            return;
        case GridDynSimulation::gridState_t::POWERFLOW_COMPLETE:
            retval = gds->powerflow();
            EXPECT_EQ(retval, 0);
            return;
        case GridDynSimulation::gridState_t::DYNAMIC_INITIALIZED:
            retval = gds->dynInitialize();
            EXPECT_EQ(retval, 0);
            return;
        case GridDynSimulation::gridState_t::DYNAMIC_COMPLETE:
            EXPECT_EQ(gds->run(), 0);
            return;
        default:
            gds->run();
            checkState(finalState);
            return;
    }
}

void GridDynSimulationTestFixture::detailedStageCheck(const std::string& fileName,
                                                      GridDynSimulation::gridState_t finalState)
{
    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);
    ASSERT_EQ(readerConfig::warnCount, 0);
    requireState(GridDynSimulation::gridState_t::STARTUP);
    int retval = gds->pFlowInitialize();
    EXPECT_EQ(retval, 0);
    requireState(GridDynSimulation::gridState_t::INITIALIZED);
    runJacobianCheck(gds, cPflowSolverMode);

    if (finalState == GridDynSimulation::gridState_t::INITIALIZED) {
        return;
    }
    gds->powerflow();

    requireState(GridDynSimulation::gridState_t::POWERFLOW_COMPLETE);

    if (finalState == GridDynSimulation::gridState_t::POWERFLOW_COMPLETE) {
        return;
    }
    gds->dynInitialize();
    runResidualCheck(gds, cDaeSolverMode);

    runJacobianCheck(gds, cDaeSolverMode);

    requireState(GridDynSimulation::gridState_t::DYNAMIC_INITIALIZED);
    if (finalState == GridDynSimulation::gridState_t::DYNAMIC_INITIALIZED) {
        return;
    }
    retval = gds->run();
    ASSERT_EQ(retval, 0);
    if (gds->hasDynamics()) {
        requireState(GridDynSimulation::gridState_t::DYNAMIC_COMPLETE);
    } else {
        requireState(GridDynSimulation::gridState_t::POWERFLOW_COMPLETE);
    }
}

void GridDynSimulationTestFixture::dynamicInitializationCheck(const std::string& fileName)
{
    readerConfig::setPrintMode(0);
    gds = readSimXMLFile(fileName);

    int retval = gds->dynInitialize();

    EXPECT_EQ(retval, 0);

    int mmatch = runJacobianCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
    mmatch = runResidualCheck(gds, cDaeSolverMode);

    ASSERT_EQ(mmatch, 0);
}

gridLoadTestFixture::gridLoadTestFixture()
{
    readerConfig::setPrintMode(0);
}

gridLoadTestFixture::~gridLoadTestFixture()
{
    if (ld1) {
        delete ld1;
    }

    if (ld2) {
        delete ld2;
    }
}

glbconfig::glbconfig() = default;
glbconfig::~glbconfig() = default;

int runJacobianCheck(std::unique_ptr<gridDynSimulation>& gds,
                     const solverMode& sMode,
                     bool checkRequired)
{
    int mmatch = JacobianCheck(gds.get(), sMode);
    if (mmatch > 0) {
        printStateNames(gds.get(), sMode);
        if (checkRequired) {
            ADD_FAILURE() << "Jacobian mismatch count: " << mmatch;
        }
    }
    return mmatch;
}

int runJacobianCheck(std::unique_ptr<gridDynSimulation>& gds,
                     const solverMode& sMode,
                     double tol,
                     bool checkRequired)
{
    int mmatch = JacobianCheck(gds.get(), sMode, tol);
    if (mmatch > 0) {
        printStateNames(gds.get(), sMode);
        if (checkRequired) {
            ADD_FAILURE() << "Jacobian mismatch count: " << mmatch;
        }
    }
    return mmatch;
}

int runResidualCheck(std::unique_ptr<gridDynSimulation>& gds,
                     const solverMode& sMode,
                     bool checkRequired)
{
    int mmatch = residualCheck(gds.get(), sMode);
    if (mmatch > 0) {
        printStateNames(gds.get(), sMode);
        if (checkRequired) {
            ADD_FAILURE() << "Residual mismatch count: " << mmatch;
        }
    }

    return mmatch;
}

int runDerivativeCheck(std::unique_ptr<gridDynSimulation>& gds,
                       const solverMode& sMode,
                       bool checkRequired)
{
    int mmatch = derivativeCheck(gds.get(), gds->getSimulationTime(), sMode);
    if (mmatch > 0) {
        printStateNames(gds.get(), sMode);
        if (checkRequired) {
            ADD_FAILURE() << "Derivative mismatch count: " << mmatch;
        }
    }
    return mmatch;
}

int runAlgebraicCheck(std::unique_ptr<gridDynSimulation>& gds,
                      const solverMode& sMode,
                      bool checkRequired)
{
    int mmatch = algebraicCheck(gds.get(), gds->getSimulationTime(), sMode);
    if (mmatch > 0) {
        printStateNames(gds.get(), sMode);
        if (checkRequired) {
            ADD_FAILURE() << "Algebraic mismatch count: " << mmatch;
        }
    }
    return mmatch;
}

void printBusResultDeviations(const std::vector<double>& V1,
                              const std::vector<double>& V2,
                              const std::vector<double>& A1,
                              const std::vector<double>& A2)
{
    for (size_t kk = 0; kk < V1.size(); ++kk) {
        if ((std::abs(V1[kk] - V2[kk]) > 0.0001) || (std::abs(A1[kk] - A2[kk]) > 0.0001)) {
            std::cout << "Bus " << kk + 1 << "::" << V1[kk] << "vs." << V2[kk]
                      << "::" << A1[kk] * 180.0 / kPI << "vs." << A2[kk] * 180.0 / kPI
                      << "::" << V1[kk] - V2[kk] << ',' << (A1[kk] - A2[kk]) * 180.0 / kPI << "\n";
        }
    }
}
