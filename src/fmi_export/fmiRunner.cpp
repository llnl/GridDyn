/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
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

namespace griddyn::fmi {
FmiRunner::FmiRunner(const std::string& name,
                     const std::string& resourceLocations,
                     const fmi2CallbackFunctions* functions,
                     bool ModelExchange): identifier(name), mResourceLocation(resourceLocations)
{
    if (functions != nullptr) {
        loggerFunc = functions->logger;
        stepFinished = functions->stepFinished;
    }
    loadLibraries();
    FmiRunner::Reset();
    mModelExchangeRunner = ModelExchange;
}

FmiRunner::~FmiRunner() = default;

int FmiRunner::Reset()
{
    m_gds = std::make_shared<gridDynSimulation>(identifier);

    mCoordinator = make_owningPtr<FmiCoordinator>();
    // store the coordinator as a support object so everything can find it
    m_gds->add(mCoordinator.get());

    readerInfo readerInformation;
    loadFmiExportReaderInfoDefinitions(readerInformation);

    readerInformation.addDirectory(mResourceLocation);

    std::filesystem::path mainFilePath = mResourceLocation;
    mainFilePath /= "simulation.xml";

    if (std::filesystem::exists(mainFilePath)) {
        loadFile(m_gds.get(), mainFilePath.string(), &readerInformation, "xml");
    } else {
        throw(std::invalid_argument("unable to locate main file"));
    }
    m_gds->setFlag("force_extra_powerflow");
    return FUNCTION_EXECUTION_SUCCESS;
}

void FmiRunner::updateOutputs()
{
    mCoordinator->updateOutputs(m_gds->getSimulationTime());
}

coreTime FmiRunner::Run()
{
    return GriddynRunner::Run();
}

void FmiRunner::StepAsync(coreTime time)
{
    if (stepFinished != nullptr) {
        mAsyncReturn = std::async(std::launch::async, [this, time] {
            FmiRunner::Step(time);
            stepFinished(fmiComp, fmi2OK);
        });
    } else {
        auto stime = Step(time);
        if (stime < time) {
            //????
        }
    }
}

bool FmiRunner::isFinished() const
{
    return (mAsyncReturn.valid()) ?
        (mAsyncReturn.wait_for(std::chrono::seconds(0)) == std::future_status::ready) :
        true;
}
coreTime FmiRunner::Step(coreTime time)
{
    auto retTime = GriddynRunner::Step(time);
    mCoordinator->updateOutputs(retTime);
    return retTime;
}

void FmiRunner::Finalize()
{
    GriddynRunner::Finalize();
}

index_t FmiRunner::findVR(const std::string& varName) const
{
    return mCoordinator->findVR(varName);
}

void FmiRunner::logger(int level, const std::string& logMessage)
{
    if (mLoggingCategories[level]) {
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
                    loggerFunc(
                        fmiComp, m_gds->getName().c_str(), fmi2OK, "logNormal", logMessage.c_str());
                    break;
                case 5:
                    loggerFunc(
                        fmiComp, m_gds->getName().c_str(), fmi2OK, "logDebug", logMessage.c_str());
                    break;
                default:
                    break;
            }
        }
    }
}

id_type_t FmiRunner::getId() const
{
    return m_gds->getID();
}

bool FmiRunner::setValue(index_t valueReference, double inputValue)
{
    return mCoordinator->sendInput(valueReference, inputValue);
}

bool FmiRunner::setStringValue(index_t valueReference, const char* stringValue)
{
    return mCoordinator->sendInput(valueReference, stringValue);
}

double FmiRunner::getValue(index_t valueReference)
{
    return mCoordinator->getOutput(valueReference);
}

}  // namespace griddyn::fmi
