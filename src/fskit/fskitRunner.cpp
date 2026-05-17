/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fskitRunner.h"

#include "core/factoryTemplates.hpp"
#include "fskitCommunicator.h"
#include "gridDynFederatedScheduler.h"
#include "griddyn-tracer.h"
#include "griddyn/gridDynSimulation.h"
#include <memory>
#include <string>

namespace griddyn {
static childClassFactory<FskitCommunicator, griddyn::Communicator>
    commFac(std::vector<std::string>{"fskit"});

FskitRunner::FskitRunner() {}

int FskitRunner::Initialize(
    int argc,
    char* argv[],
    std::shared_ptr<fskit::GrantedTimeWindowScheduler> grantedTimeWindowScheduler)
{
    if (grantedTimeWindowScheduler) {
        GridDynFederatedScheduler::Initialize(grantedTimeWindowScheduler);
    }
    return Initialize(argc, argv);
}

int FskitRunner::Initialize(int argc, char* argv[])
{
    GRIDDYN_TRACER("GridDyn::GriddynRunner::Initialize");
    auto returnValue = GriddynRunner::Initialize(argc, argv);
    auto simulation = GriddynRunner::getSim();
    gridDynSimulation::setInstance(simulation.get());
    return returnValue;
}

coreTime FskitRunner::Run()
{
    GRIDDYN_TRACER("GridDyn::GriddynRunner::Run");
    return GriddynRunner::Run();
}

void FskitRunner::Finalize()
{
    GRIDDYN_TRACER("GridDyn::GriddynRunner::Finalize");
    GriddynRunner::Finalize();
}
}  // namespace griddyn
