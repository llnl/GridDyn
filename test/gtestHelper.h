/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "fileInput/fileInput.h"
#include "griddyn/gridDynSimulation.h"
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
class Load;
class solverMode;
}  // namespace griddyn

struct gridDynSimulationTestFixture {
    gridDynSimulationTestFixture();
    gridDynSimulationTestFixture(const gridDynSimulationTestFixture&) = delete;
    ~gridDynSimulationTestFixture();
    gridDynSimulationTestFixture& operator=(const gridDynSimulationTestFixture&) = delete;

    std::unique_ptr<griddyn::gridDynSimulation> gds;
    std::unique_ptr<griddyn::gridDynSimulation> gds2;

    void simpleRunTestXML(const std::string& fileName);
    void runTestXML(const std::string& fileName,
                    griddyn::gridDynSimulation::gridState_t finalState);
    void detailedStageCheck(const std::string& fileName,
                            griddyn::gridDynSimulation::gridState_t finalState);
    void simpleStageCheck(const std::string& fileName,
                          griddyn::gridDynSimulation::gridState_t finalState);
    void dynamicInitializationCheck(const std::string& fileName);

    void checkState(griddyn::gridDynSimulation::gridState_t state);
    void requireState(griddyn::gridDynSimulation::gridState_t state);
    void checkState2(griddyn::gridDynSimulation::gridState_t state);
    void requireState2(griddyn::gridDynSimulation::gridState_t state);
};

struct gridLoadTestFixture {
    gridLoadTestFixture();
    gridLoadTestFixture(const gridLoadTestFixture&) = delete;
    ~gridLoadTestFixture();
    gridLoadTestFixture& operator=(const gridLoadTestFixture&) = delete;

    griddyn::Load* ld1 = nullptr;
    griddyn::Load* ld2 = nullptr;
};

struct glbconfig {
    glbconfig();
    ~glbconfig();
};

std::ostream& operator<<(std::ostream& os, griddyn::gridDynSimulation::gridState_t state);

const std::string& to_string(griddyn::gridDynSimulation::gridState_t state);

void checkStates(griddyn::gridDynSimulation::gridState_t state1,
                 griddyn::gridDynSimulation::gridState_t state2);

void requireStates(griddyn::gridDynSimulation::gridState_t state1,
                   griddyn::gridDynSimulation::gridState_t state2);

int runJacobianCheck(std::unique_ptr<griddyn::gridDynSimulation>& gds,
                     const griddyn::solverMode& sMode,
                     bool checkRequired = true);

int runJacobianCheck(std::unique_ptr<griddyn::gridDynSimulation>& gds,
                     const griddyn::solverMode& sMode,
                     double tol,
                     bool checkRequired = true);

int runResidualCheck(std::unique_ptr<griddyn::gridDynSimulation>& gds,
                     const griddyn::solverMode& sMode,
                     bool checkRequired = true);

int runDerivativeCheck(std::unique_ptr<griddyn::gridDynSimulation>& gds,
                       const griddyn::solverMode& sMode,
                       bool checkRequired = true);

int runAlgebraicCheck(std::unique_ptr<griddyn::gridDynSimulation>& gds,
                      const griddyn::solverMode& sMode,
                      bool checkRequired = true);

void printBusResultDeviations(const std::vector<double>& V1,
                              const std::vector<double>& V2,
                              const std::vector<double>& A1,
                              const std::vector<double>& A2);
