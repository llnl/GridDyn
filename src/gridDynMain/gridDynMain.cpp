/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "core/coreExceptions.h"
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/griddyn-config.h"
#include "runner/gridDynRunner.h"

#include <boost/format.hpp>
#ifdef ENABLE_HELICS_EXECUTABLE
#    include "helics/helicsRunner.h"
#endif

#ifdef ENABLE_DIME
#    include "networking/dimeRunner.h"
#endif

#ifdef ENABLE_FMI_EXPORT
#    include "fmi_export/fmuBuilder.h"
#endif

enum class execMode_t {
    normal = 0,
    mpicount = 1,
    helics = 2,
    buildfmu = 3,
    dime = 4,
    buildgdz = 5,
};

using namespace griddyn;
// main
int main(int argc, char* argv[])
{
    auto gds = std::make_shared<gridDynSimulation>();

    // Store the simulation pointer somewhere so that it can be accessed in other modules.
    gridDynSimulation::setInstance(gds.get());  // peer to gridDynSimulation::GetInstance ();

    // TODO: This was removed earlier. Need a way to get access to extraModels with gridDynMain
    // executable. If always loading them when available isn't desired, alternate mechanism is
    // required (command line arg, config file?)
    loadLibraries();

    auto execMode = execMode_t::normal;
    // check for different options
    for (int ii = 1; ii < argc; ++ii) {
        if (strcmp("--mpicount", argv[ii]) == 0) {
            execMode = execMode_t::mpicount;
            break;
        }
        if (strncmp("--buildgdz", argv[ii], 10) == 0) {
            execMode = execMode_t::buildgdz;
            break;
        }
#ifdef ENABLE_FMI_EXPORT
        if (strncmp("--buildfmu", argv[ii], 10) == 0) {
            execMode = execMode_t::buildfmu;
            break;
        }
#endif
#ifdef ENABLE_HELICS_EXECUTABLE
        if (strcmp("--helics", argv[ii]) == 0) {
            execMode = execMode_t::helics;

            break;
        }
#endif
#ifdef ENABLE_DIME
        if (strcmp("--dime", argv[ii]) == 0) {
            execMode = execMode_t::dime;
            break;
        }
#endif
    }

    switch (execMode) {
        case execMode_t::normal: {
            auto runner = std::make_unique<GriddynRunner>(gds);
            auto ret = runner->Initialize(argc, argv);
            if (ret > 0) {
                return 0;
            }
            if (ret < 0) {
                return ret;
            }
            runner->simInitialize();
            runner->Run();
        } break;
        case execMode_t::mpicount: {
            auto runner = std::make_unique<GriddynRunner>(gds);
            auto ret = runner->Initialize(argc, argv);
            if (ret > 0) {
                return 0;
            }
            if (ret < 0) {
                return ret;
            }
            gds->countMpiObjects(true);
        }
            return 0;
        case execMode_t::buildfmu:
#ifdef ENABLE_FMI_EXPORT
        {
            gds->log(nullptr,
                     print_level::summary,
                     std::string("Building FMI through FMI builder"));
            auto builder = std::make_unique<fmi::fmuBuilder>(gds);
            auto ret = builder->Initialize(argc, argv);
            if (ret < 0) {
                return ret;
            }
            builder->MakeFmu();
        }
#endif
            return 0;
        case execMode_t::helics: {
#ifdef ENABLE_HELICS_EXECUTABLE
            auto runner = std::make_unique<helicsLib::helicsRunner>(gds);
            gds->log(nullptr, print_level::summary, std::string("Executing through HELICS runner"));
            auto ret = runner->Initialize(argc, argv);
            if (ret > 0) {
                return 0;
            }
            if (ret < 0) {
                return ret;
            }
            try {
                runner->simInitialize();
                runner->Run();
            }
            catch (const executionFailure& e) {
                gds->log(nullptr, print_level::error, std::string(e.what()));
            }
#endif
        } break;
        case execMode_t::dime: {
#ifdef ENABLE_DIME
            auto runner = std::make_unique<dimeLib::dimeRunner>(gds);
            gds->log(nullptr, print_level::summary, std::string("Executing through DIME runner"));
            auto ret = runner->Initialize(argc, argv);
            if (ret > 0) {
                return 0;
            }
            if (ret < 0) {
                return ret;
            }
            runner->simInitialize();
            runner->Run();
#endif
        }
        case execMode_t::buildgdz:
            gds->log(nullptr, print_level::error, std::string("GDZ builder not implemented yet"));
            return (-4);
        default:
            gds->log(nullptr, print_level::error, std::string("unknown execution mode"));
            return (-4);
            break;
    }

    auto pState = gds->currentProcessState();
    if (pState >= gridDynSimulation::gridState_t::DYNAMIC_COMPLETE) {
        auto ssize = gds->getInt("dynstatesize");
        auto jsize = gds->getInt("dynnonzeros");
        auto res =
            boost::format(
                "Simulation Final Dynamic Statesize =%d (%d V, %d angle, %d alg, %d differential), %d non zero elements in the the Jacobian\n") %
            ssize % gds->getInt("vcount") % gds->getInt("account") % gds->getInt("algcount") %
            gds->getInt("diffcount") % jsize;
        gds->log(nullptr, print_level::summary, res.str());
    } else {
        // if (pState <= gridDynSimulation::gridState_t::DYNAMIC_INITIALIZED)
        auto ssize = gds->getInt("pflowstatesize");
        auto jsize = gds->getInt("pflownonzeros");
        auto res =
            boost::format(
                "Simulation Final Powerflow Statesize =%d (%d V, %d angle), %d non zero elements in the the Jacobian\n") %
            ssize % gds->getInt("vcount") % gds->getInt("account") % jsize;
        gds->log(nullptr, print_level::summary, res.str());
    }

    return 0;
}
