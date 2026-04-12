/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiRunner.h"

#include "FMI2/fmi2Functions.h"
#include "core/coreOwningPtr.hpp"
#include "fileInput/fileInput.h"
#include "fmiCoordinator.h"
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/gridDynSimulation.h"
#include "loadFMIExportObjects.h"
#include <filesystem>
#include <memory>
#include <string>

namespace griddyn {
namespace fmi {
    fmiRunner::fmiRunner(const std::string& name,
                         const std::string& resourceLocations,
                         const fmi2CallbackFunctions* functions,
                         bool ModelExchange): identifier(name), resource_loc(resourceLocations)
    {
        if (functions != nullptr) {
            loggerFunc = functions->logger;
            stepFinished = functions->stepFinished;
        }
        loadLibraries();
        fmiRunner::Reset();
        modelExchangeRunner = ModelExchange;
    }

    fmiRunner::~fmiRunner() = default;

    int fmiRunner::Reset()
    {
        m_gds = std::make_shared<gridDynSimulation>(identifier);

        coord = make_owningPtr<fmiCoordinator>();
        // store the coordinator as a support object so everything can find it
        m_gds->add(coord.get());

        readerInfo ri;
        loadFmiExportReaderInfoDefinitions(ri);

        ri.addDirectory(resource_loc);

        std::filesystem::path mainFilePath = resource_loc;
        mainFilePath /= "simulation.xml";

        if (std::filesystem::exists(mainFilePath)) {
            loadFile(m_gds.get(), mainFilePath.string(), &ri, "xml");
        } else {
            throw(std::invalid_argument("unable to locate main file"));
        }
        m_gds->setFlag("force_extra_powerflow");
        return FUNCTION_EXECUTION_SUCCESS;
    }

    void fmiRunner::UpdateOutputs()
    {
        coord->updateOutputs(m_gds->getSimulationTime());
    }

    coreTime fmiRunner::Run()
    {
        return GriddynRunner::Run();
    }

    void fmiRunner::StepAsync(coreTime time)
    {
        if (stepFinished != nullptr) {
            async_retFMI = std::async(std::launch::async, [this, time] {
                fmiRunner::Step(time);
                stepFinished(fmiComp, fmi2OK);
            });
        } else {
            auto stime = Step(time);
            if (stime < time) {
                //????
            }
        }
    }

    bool fmiRunner::isFinished() const
    {
        return (async_retFMI.valid()) ?
            (async_retFMI.wait_for(std::chrono::seconds(0)) == std::future_status::ready) :
            true;
    }
    coreTime fmiRunner::Step(coreTime time)
    {
        auto retTime = GriddynRunner::Step(time);
        coord->updateOutputs(retTime);
        return retTime;
    }

    void fmiRunner::Finalize()
    {
        GriddynRunner::Finalize();
    }

    index_t fmiRunner::findVR(const std::string& varName) const
    {
        return coord->findVR(varName);
    }

    void fmiRunner::logger(int level, const std::string& logMessage)
    {
        if (loggingCategories[level]) {
            if (loggerFunc != nullptr) {
                switch (level) {
                    case 0:
                    case 1:
                        loggerFunc(fmiComp,
                                   m_gds->getName().c_str(),
                                   fmi2Error,
                                   "logError",
                                   logMessage.c_str());
                        break;
                    case 2:
                        loggerFunc(fmiComp,
                                   m_gds->getName().c_str(),
                                   fmi2OK,
                                   "logWarning",
                                   logMessage.c_str());
                        break;
                    case 3:
                        loggerFunc(fmiComp,
                                   m_gds->getName().c_str(),
                                   fmi2OK,
                                   "logSummary",
                                   logMessage.c_str());
                        break;
                    case 4:
                        loggerFunc(fmiComp,
                                   m_gds->getName().c_str(),
                                   fmi2OK,
                                   "logNormal",
                                   logMessage.c_str());
                        break;
                    case 5:
                        loggerFunc(fmiComp,
                                   m_gds->getName().c_str(),
                                   fmi2OK,
                                   "logDebug",
                                   logMessage.c_str());
                        break;
                    default:
                        break;
                }
            }
        }
    }

    id_type_t fmiRunner::GetID() const
    {
        return m_gds->getID();
    }

    bool fmiRunner::Set(index_t vr, double val)
    {
        return coord->sendInput(vr, val);
    }

    bool fmiRunner::SetString(index_t vr, const char* s)
    {
        return coord->sendInput(vr, s);
    }

    double fmiRunner::Get(index_t vr)
    {
        return coord->getOutput(vr);
    }

}  // namespace fmi
}  // namespace griddyn
