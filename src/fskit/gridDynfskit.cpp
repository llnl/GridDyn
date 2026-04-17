/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynfskit.h"

#include "core/factoryTemplates.hpp"
#include "fskitCommunicator.h"
#include "fskitRunner.h"
#include "griddyn-tracer.h"
#include <memory>
#include <string>
// static std::vector<std::shared_ptr<objectFactory>> fskitFactories;

static griddyn::childClassFactory<FskitCommunicator, griddyn::Communicator>
    commFac(std::vector<std::string>{"fskit"});

void loadFskit(const std::string& /*subset*/) {}

extern "C" {

/*
 * This is a C interface for running GridDyn through FSKIT.
 */
int griddyn_runner_main(int argc, char* argv[])
{
    GRIDDYN_TRACER("GridDyn::griddyn_runner_main");

#ifdef GRIDDYN_HAVE_ETRACE
    std::stringstream program_trace_filename;
    program_trace_filename << "etrace/"
                           << "program_trace." << std::setw(6) << std::setfill('0') << 0
                           << ".etrace";
    init_tracefile(program_trace_filename.str().c_str());

#endif

    auto GridDyn = std::make_shared<griddyn::fskitRunner>();

    // Not running with FSKIT.
    std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler(nullptr);
    GridDyn->Initialize(argc, argv, scheduler);
    GridDyn->simInitialize();

    GridDyn->Run();

    GridDyn->Finalize();

    return 0;
}
}
