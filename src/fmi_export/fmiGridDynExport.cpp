/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FMI2/fmi2Functions.h"
#include "core/coreExceptions.h"
#include "fmiRunner.h"
#include "griddyn/gridDynSimulation.h"
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

const char* fmi2GetTypesPlatform(void)
{
    return "default";
}
const char* fmi2GetVersion(void)
{
    return "2.0";
}

using griddyn::coreObjectException;
using griddyn::id_type_t;
using griddyn::fmi::fmiRunner;

using runner_t = fmiRunner*;
static std::mutex fmiLock;  //!< lock for allowing multi-threaded access

static std::vector<std::unique_ptr<fmiRunner>>
    fmiRunnerInstances(1);  // vector of fmiRunner Instances

using compID = std::pair<unsigned int, id_type_t>;

runner_t getFmiRunner(fmi2Component comp)
{
    auto p = static_cast<compID*>(comp);
    std::lock_guard<std::mutex> lock(fmiLock);
    auto& runner = (p->first < fmiRunnerInstances.size()) ? fmiRunnerInstances[p->first] :
                                                            fmiRunnerInstances[0];
    if ((!runner) || (runner->GetID() != p->second)) {
        return nullptr;
    }
    return runner.get();
}

fmi2Status fmi2SetDebugLogging(fmi2Component comp,
                               fmi2Boolean loggingOn,
                               size_t nCategories,
                               const fmi2String categories[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    std::bitset<7> logCat = 0;
    logCat[0] = true;
    if (loggingOn != 0) {
        for (size_t ii = 0; ii < nCategories; ++ii) {
            std::string lcat(categories[ii]);
            if (lcat == "logError") {
                logCat[1] = true;
            } else if (lcat == "logWarning") {
                logCat[2] = true;
            } else if (lcat == "logSummary") {
                logCat[3] = true;
            } else if (lcat == "logNormal") {
                logCat[4] = true;
            } else if (lcat == "logDebug") {
                logCat[5] = true;
            } else if (lcat == "logTrace") {
                logCat[6] = true;
            } else {
                return fmi2Warning;
            }
        }
    }
    runner->setLoggingCategories(logCat);
    return fmi2OK;
}

/* Creation and destruction of FMU instances and setting debug status */
fmi2Component fmi2Instantiate(fmi2String instanceName,
                              fmi2Type fmuType,
                              fmi2String /*fmuGUID*/,
                              fmi2String fmuResourceLocation,
                              const fmi2CallbackFunctions* functions,
                              fmi2Boolean /*visible*/,
                              fmi2Boolean loggingOn)
{
    if ((fmuType == fmi2Type::fmi2CoSimulation) || (fmuType == fmi2Type::fmi2ModelExchange)) {
        auto locString = std::string(fmuResourceLocation);
        if (locString.compare(0, 6, "file:/") == 0) {
            if (locString[6] == '/') {
                if (locString[7] == '/') {
                    locString.erase(0, 8);
                } else {
                    locString.erase(0, 7);
                }
            } else {
                locString.erase(0, 6);
            }
        }
        try {
            auto fmiM = std::make_unique<fmi::fmiRunner>(instanceName,
                                                         locString,
                                                         functions,
                                                         (fmuType == fmi2Type::fmi2ModelExchange));
            auto rv = new compID;
            rv->second = fmiM->GetID();
            fmiM->fmiComp = static_cast<fmi2Component>(rv);
            std::lock_guard<std::mutex> arrayLock(fmiLock);
            if (loggingOn == fmi2False) {
                std::bitset<7> logCat{0};
                logCat[0] = true;
                logCat[1] = true;
                logCat[2] = true;
                logCat[3] = true;
                fmiM->setLoggingCategories(logCat);
            }
            fmiRunnerInstances.push_back(std::move(fmiM));
            rv->first = static_cast<unsigned int>(fmiRunnerInstances.size()) - 1;

            return static_cast<fmi2Component>(rv);
        }
        catch (const std::invalid_argument&) {
            return nullptr;
        }
    } else {
        return nullptr;
    }
}

void fmi2FreeInstance(fmi2Component comp)
{
    auto p = static_cast<compID*>(comp);
    if (getFmiRunner(comp) != nullptr) {
        std::lock_guard<std::mutex> lock(fmiLock);
        fmiRunnerInstances[p->first] = nullptr;
        delete p;
    }
}

/* Enter and exit initialization mode, terminate and reset */
fmi2Status fmi2SetupExperiment(fmi2Component comp,
                               fmi2Boolean toleranceDefined,
                               fmi2Real tolerance,
                               fmi2Real startTime,
                               fmi2Boolean stopTimeDefined,
                               fmi2Real stopTime)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    runner->getSim()->set("starttime", startTime);
    if (toleranceDefined != fmi2False) {
        runner->getSim()->set("tolerance", tolerance);
    }
    if (stopTimeDefined != fmi2False) {
        runner->getSim()->set("stoptime", stopTime);
    }
    return fmi2OK;
}
fmi2Status fmi2EnterInitializationMode(fmi2Component /*comp*/)
{
    // in GridDyn there is no difference between startup and initialization
    return fmi2OK;
}
fmi2Status fmi2ExitInitializationMode(fmi2Component comp)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    try {
        runner->simInitialize();
    }
    catch (const coreObjectException& e) {
        runner->logger(0, std::string("Simulation Initialization failed:") + e.what());
        return fmi2Error;
    }
    runner->UpdateOutputs();
    return fmi2OK;
}
fmi2Status fmi2Terminate(fmi2Component comp)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    runner->Finalize();
    fmi2FreeInstance(comp);

    return fmi2OK;
}
fmi2Status fmi2Reset(fmi2Component comp)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto res = runner->Reset();
    return (res == 0) ? fmi2OK : fmi2Error;
}

/* Getting and setting variable values */
fmi2Status
    fmi2GetReal(fmi2Component comp, const fmi2ValueReference vr[], size_t nvr, fmi2Real value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        value[ii] = runner->Get(vr[ii]);
        if (value[ii] < -1e45) {
            // send a log message
            ret = fmi2Warning;
        }
    }
    return ret;
}
fmi2Status fmi2GetInteger(fmi2Component comp,
                          const fmi2ValueReference vr[],
                          size_t nvr,
                          fmi2Integer value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        if (vr[ii] == 0) {
            value[ii] = runner->runAsynchronously() ? 1 : 0;
        } else {
            auto res = runner->Get(vr[ii]);
            if (res < -1e45) {
                ret = fmi2Warning;
            }
            value[ii] = static_cast<int>(res);
        }
    }
    return ret;
}
fmi2Status fmi2GetBoolean(fmi2Component comp,
                          const fmi2ValueReference vr[],
                          size_t nvr,
                          fmi2Boolean value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        if (vr[ii] == 0) {
            value[ii] = runner->runAsynchronously() ? fmi2True : fmi2False;
        } else {
            auto res = runner->Get(vr[ii]);
            if (res < -1e45) {
                ret = fmi2Warning;
            }
            value[ii] = (res > 0.1) ? fmi2True : fmi2False;
        }
    }
    return ret;
}
fmi2Status
    fmi2GetString(fmi2Component comp, const fmi2ValueReference vr[], size_t nvr, fmi2String value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        if (vr[ii] == 1) {
            runner->recordDirectory = runner->getSim()->getString("recorddirectory");
            value[ii] = runner->recordDirectory.c_str();
        } else {
            ret = fmi2Warning;
        }
    }
    return ret;
}

fmi2Status fmi2SetReal(fmi2Component comp,
                       const fmi2ValueReference vr[],
                       size_t nvr,
                       const fmi2Real value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        auto res = runner->Set(vr[ii], value[ii]);
        if (!res) {
            // printf("set of vr %d failed\n", vr[ii]);
            ret = fmi2Warning;
        }
    }
    return ret;
}
fmi2Status fmi2SetInteger(fmi2Component comp,
                          const fmi2ValueReference vr[],
                          size_t nvr,
                          const fmi2Integer value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        if (vr[ii] == 0) {
            runner->setAsynchronousMode(value[ii] > 0);
        } else {
            auto res = runner->Set(vr[ii], static_cast<double>(value[ii]));
            if (!res) {
                ret = fmi2Warning;
            }
        }
    }
    return ret;
}

fmi2Status fmi2SetBoolean(fmi2Component comp,
                          const fmi2ValueReference vr[],
                          size_t nvr,
                          const fmi2Boolean value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }

    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        if (vr[ii] == 0) {
            runner->setAsynchronousMode(value[ii] == fmi2True);
        } else {
            auto res = runner->Set(vr[ii], static_cast<double>(value[ii]));
            if (!res) {
                ret = fmi2Warning;
            }
        }
    }
    return ret;
}

fmi2Status fmi2SetString(fmi2Component comp,
                         const fmi2ValueReference vr[],
                         size_t nvr,
                         const fmi2String value[])
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    auto ret = fmi2OK;
    for (size_t ii = 0; ii < nvr; ++ii) {
        if (vr[ii] == 1) {
            runner->recordDirectory = value[ii];
            runner->getSim()->set("recorddirectory", runner->recordDirectory);
        } else {
            printf(" setting string %d to %s\n", vr[ii], value[ii]);
            auto res = runner->SetString(vr[ii], value[ii]);
            if (!res) {
                ret = fmi2Warning;
            }
        }
    }
    return ret;
}

/* Getting and setting the internal FMU state */
fmi2Status fmi2GetFMUstate(fmi2Component /*comp*/, fmi2FMUstate* /*FMUstate*/)
{
    return fmi2Discard;
}
fmi2Status fmi2SetFMUstate(fmi2Component /*comp*/, fmi2FMUstate /*FMUstate*/)
{
    return fmi2Discard;
}
fmi2Status fmi2FreeFMUstate(fmi2Component /*comp*/, fmi2FMUstate* FMUstate)
{
    if (FMUstate == nullptr) {
        return fmi2OK;
    }
    return fmi2Discard;
}
fmi2Status
    fmi2SerializedFMUstateSize(fmi2Component /*comp*/, fmi2FMUstate /*FMUstate*/, size_t* /*size*/)
{
    return fmi2Discard;
}
fmi2Status fmi2SerializeFMUstate(fmi2Component /*comp*/,
                                 fmi2FMUstate /*FMUstate*/,
                                 fmi2Byte /*serializedState*/[],
                                 size_t /*size*/)
{
    return fmi2Discard;
}
fmi2Status fmi2DeSerializeFMUstate(fmi2Component /*comp*/,
                                   const fmi2Byte /*serializedState*/[],
                                   size_t /*size*/,
                                   fmi2FMUstate* /*FMUstate*/)
{
    return fmi2Discard;
}

/* Getting partial derivatives */
fmi2Status fmi2GetDirectionalDerivative(fmi2Component /*comp*/,
                                        const fmi2ValueReference /*vUnknownRef*/[],
                                        size_t /*nUnknown*/,
                                        const fmi2ValueReference /*vKnownRef*/[],
                                        size_t /*unknown*/,
                                        const fmi2Real /*dvKnown*/[],
                                        fmi2Real /*dvUnknown*/[])
{
    return fmi2Discard;
}

/***************************************************
Types for Functions for FMI2 for Co-Simulation
****************************************************/

/* Simulating the slave */
fmi2Status fmi2SetRealInputDerivatives(fmi2Component /*comp*/,
                                       const fmi2ValueReference /*vr*/[],
                                       size_t /*nvr*/,
                                       const fmi2Integer /*order*/[],
                                       const fmi2Real /*value*/[])
{
    return fmi2Discard;
}
fmi2Status fmi2GetRealOutputDerivatives(fmi2Component /*comp*/,
                                        const fmi2ValueReference /*vr*/[],
                                        size_t /*nvr*/,
                                        const fmi2Integer /*order*/[],
                                        fmi2Real /*value*/[])
{
    return fmi2Discard;
}

fmi2Status fmi2DoStep(fmi2Component comp,
                      fmi2Real currentCommunicationPoint,
                      fmi2Real communicationStepSize,
                      fmi2Boolean /*noSetPriorPoint*/)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    if (runner->runAsynchronously()) {
        runner->StepAsync(currentCommunicationPoint + communicationStepSize);
        return fmi2Pending;
    }

    runner->Step(currentCommunicationPoint + communicationStepSize);
    return fmi2OK;
}
fmi2Status fmi2CancelStep(fmi2Component /*comp*/)
{
    return fmi2Discard;
}

/* Inquire slave status */
fmi2Status fmi2GetStatus(fmi2Component comp, const fmi2StatusKind s, fmi2Status* status)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    if ((s == fmi2DoStepStatus) || (s == fmi2PendingStatus)) {
        *status = (runner->isFinished()) ? fmi2OK : fmi2Pending;
        return fmi2OK;
    }
    return fmi2Discard;
}

fmi2Status fmi2GetRealStatus(fmi2Component comp, const fmi2StatusKind s, fmi2Real* status)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    if (s == fmi2LastSuccessfulTime) {
        *status = static_cast<fmi2Real>(runner->getSim()->getSimulationTime());
        return fmi2OK;
    }
    return fmi2Discard;
}

fmi2Status fmi2GetIntegerStatus(fmi2Component /*comp*/,
                                const fmi2StatusKind /*s*/,
                                fmi2Integer* /*status*/)
{
    return fmi2Discard;
}
fmi2Status fmi2GetBooleanStatus(fmi2Component comp, const fmi2StatusKind s, fmi2Boolean* status)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    if (s == fmi2LastSuccessfulTime) {
        *status = fmi2False;
        return fmi2OK;
    }
    return fmi2Discard;
}
fmi2Status fmi2GetStringStatus(fmi2Component comp, const fmi2StatusKind s, fmi2String* status)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    if ((s == fmi2DoStepStatus) || (s == fmi2PendingStatus)) {
        *status = (runner->isFinished()) ? "finished" : "pending";
        return fmi2OK;
    }
    return fmi2Discard;
}

/** model exchange functions*/

fmi2Status fmi2EnterEventMode(fmi2Component comp)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status fmi2NewDiscreteStates(fmi2Component comp, fmi2EventInfo* /* fmi2eventInfo */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status fmi2EnterContinuousTimeMode(fmi2Component comp)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status fmi2CompletedIntegratorStep(fmi2Component comp,
                                       fmi2Boolean /* noSetFMUStatePriorToCurrentPoint */,
                                       fmi2Boolean* /* enterEventMode */,
                                       fmi2Boolean* /* terminateSimulation */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

/* Providing independent variables and re-initialization of caching */
fmi2Status fmi2SetTime(fmi2Component comp, fmi2Real /* time */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status fmi2SetContinuousStates(fmi2Component comp, const fmi2Real /* x */[], size_t /* nx */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

/* Evaluation of the model equations */
fmi2Status fmi2GetDerivatives(fmi2Component comp, fmi2Real /* derivatives */[], size_t /* nx */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status
    fmi2GetEventIndicators(fmi2Component comp, fmi2Real /* eventIndicators */[], size_t /* ni */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status fmi2GetContinuousStates(fmi2Component comp, fmi2Real /* x */[], size_t /* nx */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}

fmi2Status fmi2GetNominalsOfContinuousStates(fmi2Component comp,
                                             fmi2Real /* x_nominals */[],
                                             size_t /* nx */)
{
    auto runner = getFmiRunner(comp);
    if (runner == nullptr) {
        return fmi2Error;
    }
    return fmi2Discard;
}
