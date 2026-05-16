/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fmi/fmi_models/fmiMELoad3phase.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "griddyn/simulation/diagnostics.h"
#include "griddyn/simulation/gridDynSimulationFileOps.h"
#include "griddyn/solvers/solverInterface.h"
#include <chrono>
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <set>
#include <utility>

using namespace griddyn;

class ExtraFmuTests: public gridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(ExtraFmuTests, LoadFmu)
{
    fmi::FmiMELoad3phase ld3;
    ld3.set(
        "fmu",
        "C:\\Users\\top1\\Documents\\codeProjects\\griddyn_test_cases\\fmus\\DUMMY_0CYMDIST.fmu");
    ld3.set(
        "_configurationFileName",
        "C:\\Users\\top1\\Documents\\codeProjects\\griddyn_test_cases\\fmus\\configuration.json");
    ld3.dynInitializeA(0, 0);
    IOdata res;
    ld3.dynInitializeB(noInputs, noInputs, res);
}
