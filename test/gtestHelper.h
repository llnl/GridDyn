/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "fileInput/fileInput.h"
#include "griddyn/GridDynSimulation.h"
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

// #define WINDOWS_MEMORY_DEBUG

#ifdef WINDOWS_MEMORY_DEBUG
#    ifdef WIN32
#        define _CRTDBG_MAP_ALLOC
#        include <crtdbg.h>
#        include <stdlib.h>

#    endif
#endif

#ifndef GRIDDYN_TEST_DIRECTORY
#    define GRIDDYN_TEST_DIRECTORY "./test_files/"
#endif

#define IEEE_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/IEEE_test_cases/"
#define MATLAB_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/matlab_test_files/"
#define OTHER_TEST_DIRECTORY GRIDDYN_TEST_DIRECTORY "/other_test_cases/"

static const char ieee_test_directory[] = GRIDDYN_TEST_DIRECTORY "/IEEE_test_cases/";
static const char matlab_test_directory[] = GRIDDYN_TEST_DIRECTORY "/matlab_test_files/";
static const char other_test_directory[] = GRIDDYN_TEST_DIRECTORY "/other_test_cases/";

#define ENABLE_IN_DEVELOPMENT_CASES

namespace griddyn {
class GridLoad;
class solverMode;
}  // namespace griddyn

struct GridDynSimulationTestFixture {
    GridDynSimulationTestFixture();
    GridDynSimulationTestFixture(const GridDynSimulationTestFixture&) = delete;
    ~GridDynSimulationTestFixture();
    GridDynSimulationTestFixture& operator=(const GridDynSimulationTestFixture&) = delete;

    std::unique_ptr<griddyn::GridDynSimulation> gds;
    std::unique_ptr<griddyn::GridDynSimulation> gds2;

    void simpleRunTestXML(const std::string& fileName);
    void runTestXML(const std::string& fileName,
                    griddyn::GridDynSimulation::GridState finalState);
    void detailedStageCheck(const std::string& fileName,
                            griddyn::GridDynSimulation::GridState finalState);
    void simpleStageCheck(const std::string& fileName,
                          griddyn::GridDynSimulation::GridState finalState);
    void dynamicInitializationCheck(const std::string& fileName);

    void checkState(griddyn::GridDynSimulation::GridState state);
    void requireState(griddyn::GridDynSimulation::GridState state);
    void checkState2(griddyn::GridDynSimulation::GridState state);
    void requireState2(griddyn::GridDynSimulation::GridState state);
};

struct gridLoadTestFixture {
    gridLoadTestFixture();
    gridLoadTestFixture(const gridLoadTestFixture&) = delete;
    ~gridLoadTestFixture();
    gridLoadTestFixture& operator=(const gridLoadTestFixture&) = delete;

    griddyn::GridLoad* ld1 = nullptr;
    griddyn::GridLoad* ld2 = nullptr;
};

struct glbconfig {
    glbconfig();
    ~glbconfig();
};

std::ostream& operator<<(std::ostream& os, griddyn::GridDynSimulation::GridState state);

const std::string& to_string(griddyn::GridDynSimulation::GridState state);

void checkStates(griddyn::GridDynSimulation::GridState state1,
                 griddyn::GridDynSimulation::GridState state2);

void requireStates(griddyn::GridDynSimulation::GridState state1,
                   griddyn::GridDynSimulation::GridState state2);

int runJacobianCheck(std::unique_ptr<griddyn::GridDynSimulation>& gds,
                     const griddyn::solverMode& sMode,
                     bool checkRequired = true);

int runJacobianCheck(std::unique_ptr<griddyn::GridDynSimulation>& gds,
                     const griddyn::solverMode& sMode,
                     double tol,
                     bool checkRequired = true);

int runResidualCheck(std::unique_ptr<griddyn::GridDynSimulation>& gds,
                     const griddyn::solverMode& sMode,
                     bool checkRequired = true);

int runDerivativeCheck(std::unique_ptr<griddyn::GridDynSimulation>& gds,
                       const griddyn::solverMode& sMode,
                       bool checkRequired = true);

int runAlgebraicCheck(std::unique_ptr<griddyn::GridDynSimulation>& gds,
                      const griddyn::solverMode& sMode,
                      bool checkRequired = true);

void printBusResultDeviations(const std::vector<double>& V1,
                              const std::vector<double>& V2,
                              const std::vector<double>& A1,
                              const std::vector<double>& A2);

