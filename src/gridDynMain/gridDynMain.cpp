/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "core/coreExceptions.h"
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/griddyn-config.h"
#include "runner/gridDynRunner.h"
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#ifdef ENABLE_HELICS_EXECUTABLE
#    include "helics/helicsRunner.h"
#endif

#ifdef ENABLE_DIME
#    include "networking/dimeRunner.h"
#endif

#ifdef ENABLE_FMI_EXPORT
#    include "fmi_export/fmuBuilder.h"
#endif

namespace {
enum class ExecMode : std::uint8_t {
    NORMAL = 0,
    MPICOUNT = 1,
    HELICS = 2,
    BUILDFMU = 3,
    DIME = 4,
    BUILDGDZ = 5,
};
}

// main
int main(int argc, char* argv[])
{
    try {
        auto simulation = std::make_shared<griddyn::gridDynSimulation>();

        // Store the simulation pointer somewhere so that it can be accessed in other modules.
        griddyn::gridDynSimulation::setInstance(
            simulation.get());  // peer to gridDynSimulation::GetInstance ();

        // TODO(phlpt): Restore a configurable mechanism for loading extra models in gridDynMain.
        // executable. If always loading them when available isn't desired, alternate mechanism is
        // required (command line arg, config file?)
        griddyn::loadLibraries();

        auto executionMode = ExecMode::NORMAL;
        // check for different options
        for (int ii = 1; ii < argc; ++ii) {
            if (strcmp("--mpicount", argv[ii]) == 0) {
                executionMode = ExecMode::MPICOUNT;
                break;
            }
            if (strncmp("--buildgdz", argv[ii], 10) == 0) {
                executionMode = ExecMode::BUILDGDZ;
                break;
            }
#ifdef ENABLE_FMI_EXPORT
            if (strncmp("--buildfmu", argv[ii], 10) == 0) {
                executionMode = ExecMode::BUILDFMU;
                break;
            }
#endif
#ifdef ENABLE_HELICS_EXECUTABLE
            if (strcmp("--helics", argv[ii]) == 0) {
                executionMode = ExecMode::HELICS;

                break;
            }
#endif
#ifdef ENABLE_DIME
            if (strcmp("--dime", argv[ii]) == 0) {
                executionMode = ExecMode::DIME;
                break;
            }
#endif
        }

        switch (executionMode) {
            case ExecMode::NORMAL: {
                auto runner = std::make_unique<griddyn::GriddynRunner>(simulation);
                auto returnCode = runner->Initialize(argc, argv);
                if (returnCode > 0) {
                    return 0;
                }
                if (returnCode < 0) {
                    return returnCode;
                }
                runner->simInitialize();
                runner->Run();
            } break;
            case ExecMode::MPICOUNT: {
                auto runner = std::make_unique<griddyn::GriddynRunner>(simulation);
                auto returnCode = runner->Initialize(argc, argv);
                if (returnCode > 0) {
                    return 0;
                }
                if (returnCode < 0) {
                    return returnCode;
                }
                simulation->countMpiObjects(true);
            }
                return 0;
            case ExecMode::BUILDFMU:
#ifdef ENABLE_FMI_EXPORT
            {
                simulation->log(nullptr,
                                griddyn::print_level::summary,
                                std::string("Building FMI through FMI builder"));
                auto builder = std::make_unique<fmi::fmuBuilder>(simulation);
                auto returnCode = builder->Initialize(argc, argv);
                if (returnCode < 0) {
                    return returnCode;
                }
                builder->makeFmu();
            }
#endif
                return 0;
            case ExecMode::HELICS: {
#ifdef ENABLE_HELICS_EXECUTABLE
                auto runner = std::make_unique<helicsLib::HelicsRunner>(simulation);
                simulation->log(nullptr,
                                griddyn::print_level::summary,
                                std::string("Executing through HELICS runner"));
                auto returnCode = runner->Initialize(argc, argv);
                if (returnCode > 0) {
                    return 0;
                }
                if (returnCode < 0) {
                    return returnCode;
                }
                try {
                    runner->simInitialize();
                    runner->Run();
                }
                catch (const griddyn::executionFailure& e) {
                    simulation->log(nullptr, griddyn::print_level::error, std::string(e.what()));
                }
#endif
            } break;
            case ExecMode::DIME: {
#ifdef ENABLE_DIME
                auto runner = std::make_unique<dimeLib::DimeRunner>(simulation);
                simulation->log(nullptr,
                                griddyn::print_level::summary,
                                std::string("Executing through DIME runner"));
                auto returnCode = runner->Initialize(argc, argv);
                if (returnCode > 0) {
                    return 0;
                }
                if (returnCode < 0) {
                    return returnCode;
                }
                runner->simInitialize();
                runner->Run();
#endif
            }
            case ExecMode::BUILDGDZ:
                simulation->log(nullptr,
                                griddyn::print_level::error,
                                std::string("GDZ builder not implemented yet"));
                return (-4);
            default:
                simulation->log(nullptr,
                                griddyn::print_level::error,
                                std::string("unknown execution mode"));
                return (-4);
                break;
        }

        auto processState = simulation->currentProcessState();
        if (processState >= griddyn::gridDynSimulation::gridState_t::DYNAMIC_COMPLETE) {
            auto stateSize = simulation->getInt("dynstatesize");
            auto jacobianSize = simulation->getInt("dynnonzeros");
            auto summaryMessage = std::format(
                "Simulation Final Dynamic Statesize ={} ({} V, {} angle, {} alg, {} differential), {} non zero elements in the the Jacobian\n",
                stateSize,
                simulation->getInt("vcount"),
                simulation->getInt("account"),
                simulation->getInt("algcount"),
                simulation->getInt("diffcount"),
                jacobianSize);
            simulation->log(nullptr, griddyn::print_level::summary, summaryMessage);
        } else {
            auto stateSize = simulation->getInt("pflowstatesize");
            auto jacobianSize = simulation->getInt("pflownonzeros");
            auto summaryMessage = std::format(
                "Simulation Final Powerflow Statesize ={} ({} V, {} angle), {} non zero elements in the the Jacobian\n",
                stateSize,
                simulation->getInt("vcount"),
                simulation->getInt("account"),
                jacobianSize);
            simulation->log(nullptr, griddyn::print_level::summary, summaryMessage);
        }

        return 0;
    }
    catch (const std::exception&) {
        return -5;
    }
}
