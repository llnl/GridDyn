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

// libraries

// headers
#include "gridDyn.h"
#include "gridDynRunner.h"
#include "gridDynServer.h"
#include "libraryLoader.h"
#include <chrono>
#include <cstdio>
#include <memory>

#include <filesystem>

// main
int main(int argc, char* argv[])
{
    loadLibraries();
    std::shared_ptr<gridDynSimulation> gds = std::make_shared<gridDynSimulation>();

    // Store the simulation pointer somewhere so that it can be accessed in other modules.
    gridDynSimulation::setInstance(gds.get());  // peer to gridDynSimulation::GetInstance ();

    if (!gds) {
        return (-5);
    }

    bool isMpiCountMode = false;
    // check for an MPI run setup
    for (int ii = 0; ii < argc; ++ii) {
        if (!strcmp("--mpicount", argv[ii])) {
            isMpiCountMode = true;
        }
    }

    // create the simulation

    return 0;
}
