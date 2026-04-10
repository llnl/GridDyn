/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * LLNS Copyright Start
 * Copyright (c) 2016, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
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

fskitRunner::fskitRunner() {}

int fskitRunner::Initialize(int argc,
                            char* argv[],
                            std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler)
{
    if (scheduler) {
        GriddynFederatedScheduler::Initialize(scheduler);
    }
    return Initialize(argc, argv);
}

int fskitRunner::Initialize(int argc, char* argv[])
{
    GRIDDYN_TRACER("GridDyn::GriddynRunner::Initialize");
    auto retval = GriddynRunner::Initialize(argc, argv);
    auto gds = GriddynRunner::getSim();
    gridDynSimulation::setInstance(gds.get());
    return retval;
}

coreTime fskitRunner::Run()
{
    GRIDDYN_TRACER("GridDyn::GriddynRunner::Run");
    return GriddynRunner::Run();
}

void fskitRunner::Finalize()
{
    GRIDDYN_TRACER("GridDyn::GriddynRunner::Finalize");
    GriddynRunner::Finalize();
}
}  // namespace griddyn
