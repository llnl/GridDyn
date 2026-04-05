/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiImport.h"

#include "fmiObjects.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/compiler-config.h"
#include "utilities/zipUtilities.h"
#include <array>
#include <cstdarg>
#include <map>

#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

using namespace std::filesystem;

fmiLibrary::fmiLibrary()
{
    information = std::make_shared<fmiInfo>();
}

fmiLibrary::fmiLibrary(const std::string& fmupath): fmiLibrary()
{
    loadFMU(fmupath);
}

fmiLibrary::fmiLibrary(const std::string& fmupath, const std::string& extractPath):
    extractDirectory(extractPath), fmuName(fmupath)
{
    information = std::make_shared<fmiInfo>();
    if (!exists(extractDirectory)) {
        create_directories(extractDirectory);
    }
    loadInformation();
}

fmiLibrary::~fmiLibrary() = default;

void fmiLibrary::close()
{
    soMeLoaded = false;
    soCoSimLoaded = false;
    lib = nullptr;
}

bool fmiLibrary::checkFlag(fmuCapabilityFlags flag) const
{
    return information->checkFlag(flag);
}

bool fmiLibrary::isSoLoaded(fmutype_t type) const
{
    switch (type) {
        case fmutype_t::modelExchange:
            return soMeLoaded;
        case fmutype_t::cosimulation:
            return soCoSimLoaded;
        default:
            return (soMeLoaded || soCoSimLoaded);
    }
}

void fmiLibrary::loadFMU(const std::string& fmupath)
{
    path ipath(fmupath);
    if (is_directory(ipath)) {
        extractDirectory = ipath;
    } else {
        fmuName = ipath;
        extractDirectory = fmuName.parent_path() / fmuName.stem();
    }
    loadInformation();
}

void fmiLibrary::loadFMU(const std::string& fmupath, const std::string& extractPath)
{
    extractDirectory = extractPath;
    fmuName = fmupath;
    loadInformation();
}

int fmiLibrary::getCounts(const std::string& countType) const
{
    auto cnt = size_t(-1);
    if (countType == "meobjects") {
        cnt = mecount;
    } else if (countType == "cosimobjects") {
        cnt = cosimcount;
    } else {
        cnt = information->getCounts(countType);
    }

    if (cnt == size_t(-1)) {
        return (-1);
    }
    return static_cast<int>(cnt);
}

void fmiLibrary::loadInformation()
{
    auto xmlfile = extractDirectory / "modelDescription.xml";
    if (!exists(xmlfile)) {
        auto res = extract();
        if (res != 0) {
            return;
        }
    }
    int res = information->loadFile(xmlfile.string());
    if (res != 0) {
        error = true;
        return;
    }
    xmlLoaded = true;

    // load the resources directory location if it exists
    if (exists(extractDirectory / "resources")) {
        resourceDir = extractDirectory / "resources";
    } else {
        resourceDir = "";
    }
}

std::string fmiLibrary::getTypes() const
{
    if (isSoLoaded()) {
        return std::string(baseFunctions.fmi2GetTypesPlatform());
    }
    return "";
}

std::string fmiLibrary::getVersion() const
{
    if (isSoLoaded()) {
        return std::string(baseFunctions.fmi2GetVersion());
    }
    return "";
}

int fmiLibrary::extract()
{
    int ret = utilities::unzip(fmuName.string(), extractDirectory.string());
    if (ret != 0) {
        error = true;
    }
    return ret;
}

std::unique_ptr<fmi2ModelExchangeObject>
    fmiLibrary::createModelExchangeObject(const std::string& name)
{
    if (!isSoLoaded()) {
        loadSharedLibrary(fmutype_t::modelExchange);
    }
    if (soMeLoaded) {
        if (!callbacks) {
            makeCallbackFunctions();
        }
        auto* comp =
            baseFunctions.fmi2Instantiate(name.c_str(),
                                          fmi2ModelExchange,
                                          information->getString("guid").c_str(),
                                          (R"raw(file:///)raw" + resourceDir.string()).c_str(),
                                          reinterpret_cast<fmi2CallbackFunctions*>(callbacks.get()),
                                          fmi2False,
                                          fmi2False);
        auto meobj = std::make_unique<fmi2ModelExchangeObject>(comp,
                                                               information,
                                                               commonFunctions,
                                                               ModelExchangeFunctions);
        ++mecount;
        return meobj;
    }

    return nullptr;
}

std::unique_ptr<fmi2CoSimObject> fmiLibrary::createCoSimulationObject(const std::string& name)
{
    if (!isSoLoaded()) {
        loadSharedLibrary(fmutype_t::cosimulation);
    }
    if (soCoSimLoaded) {
        if (!callbacks) {
            makeCallbackFunctions();
        }
        auto* comp =
            baseFunctions.fmi2Instantiate(name.c_str(),
                                          fmi2CoSimulation,
                                          information->getString("guid").c_str(),
                                          (R"raw(file:///)raw" + resourceDir.string()).c_str(),
                                          reinterpret_cast<fmi2CallbackFunctions*>(callbacks.get()),
                                          fmi2False,
                                          fmi2False);
        auto csobj =
            std::make_unique<fmi2CoSimObject>(comp, information, commonFunctions, CoSimFunctions);
        ++cosimcount;
        return csobj;
    }
    return nullptr;
}

void fmiLibrary::loadSharedLibrary(fmutype_t type)
{
    if (isSoLoaded()) {
        return;
    }
    auto sopath = findSoPath(type);
    bool loaded = false;
    if (!sopath.empty()) {
        lib = std::make_shared<boost::dll::shared_library>(sopath.string());
        if (lib->is_loaded()) {
            loaded = true;
        } else {
            printf("unable to load shared file %s\n", sopath.string().c_str());  // NOLINT
        }
    } else {
        printf("unable to locate shared file \n");  // NOLINT
    }
    if (loaded) {
        loadBaseFunctions();
        loadCommonFunctions();
        // Only load one or the other
        if (checkFlag(modelExchangeCapable) && type != fmutype_t::cosimulation) {
            loadModelExchangeFunctions();
            soMeLoaded = true;
            soCoSimLoaded = false;
        } else if (checkFlag(coSimulationCapable)) {
            loadCoSimFunctions();
            soCoSimLoaded = true;
            soMeLoaded = false;
        }
    }
}

path fmiLibrary::findSoPath(fmutype_t type)
{
    path sopath = extractDirectory / "binaries";

    std::string identifier;
    switch (type) {
        case fmutype_t::unknown:
        default:
            if (checkFlag(modelExchangeCapable))  // give priority to model Exchange
            {
                identifier = information->getString("meidentifier");
            } else if (checkFlag(coSimulationCapable)) {
                identifier = information->getString("cosimidentifier");
            } else {
                return "";
            }
            break;
        case fmutype_t::cosimulation:
            if (checkFlag(coSimulationCapable)) {
                identifier = information->getString("cosimidentifier");
            } else {
                return "";
            }
            break;
        case fmutype_t::modelExchange:
            if (checkFlag(modelExchangeCapable)) {
                identifier = information->getString("meidentifier");
            } else {
                return "";
            }
            break;
    }
    if IF_CONSTEXPR (sizeof(void*) == 8) {
#ifdef _WIN32
        sopath /= "win64";
        sopath /= identifier + ".dll";
#else
#    ifdef MACOS
        sopath /= "darwin64";
        sopath /= identifier + ".dylib";
#    else
        sopath /= "linux64";
        sopath /= identifier + ".so";
#    endif
#endif

        if (exists(sopath)) {
            return sopath;
        }
        printf("checking %s but doesn't exist\n", sopath.string().c_str());  // NOLINT
#ifdef MACOS
        sopath /= "darwin64";
        sopath /= identifier + ".so";
#endif
    } else {  // NOLINT
#ifdef _WIN32
        sopath /= "win32";
        sopath /= identifier + ".dll";
#else
#    ifdef MACOS
        sopath /= "darwin32";
        sopath /= identifier + ".dylib";
#    else
        sopath /= "linux32";
        sopath /= identifier + ".so";
#    endif
#endif
    }

    if (exists(sopath)) {
        return sopath;
    }

    return path("");
}

void fmiLibrary::loadBaseFunctions()
{
    baseFunctions.fmi2GetVersion = lib->get<fmi2GetVersionTYPE>("fmi2GetVersion");
    baseFunctions.fmi2GetTypesPlatform = lib->get<fmi2GetTypesPlatformTYPE>("fmi2GetTypesPlatform");
    baseFunctions.fmi2Instantiate = lib->get<fmi2InstantiateTYPE>("fmi2Instantiate");
}

// TODO(PT): move these to the constructors of the function libraries
void fmiLibrary::loadCommonFunctions()
{
    commonFunctions = std::make_shared<fmiCommonFunctions>();

    commonFunctions->lib = lib;
    commonFunctions->fmi2SetDebugLogging = lib->get<fmi2SetDebugLoggingTYPE>("fmi2SetDebugLogging");

    /* Creation and destruction of FMU instances and setting debug status */

    commonFunctions->fmi2FreeInstance = lib->get<fmi2FreeInstanceTYPE>("fmi2FreeInstance");

    /* Enter and exit initialization mode, terminate and reset */
    commonFunctions->fmi2SetupExperiment = lib->get<fmi2SetupExperimentTYPE>("fmi2SetupExperiment");
    commonFunctions->fmi2EnterInitializationMode =
        lib->get<fmi2EnterInitializationModeTYPE>("fmi2EnterInitializationMode");
    commonFunctions->fmi2ExitInitializationMode =
        lib->get<fmi2ExitInitializationModeTYPE>("fmi2ExitInitializationMode");
    commonFunctions->fmi2Terminate = lib->get<fmi2TerminateTYPE>("fmi2Terminate");
    commonFunctions->fmi2Reset = lib->get<fmi2ResetTYPE>("fmi2Reset");

    /* Getting and setting variable values */
    commonFunctions->fmi2GetReal = lib->get<fmi2GetRealTYPE>("fmi2GetReal");
    commonFunctions->fmi2GetInteger = lib->get<fmi2GetIntegerTYPE>("fmi2GetInteger");
    commonFunctions->fmi2GetBoolean = lib->get<fmi2GetBooleanTYPE>("fmi2GetBoolean");
    commonFunctions->fmi2GetString = lib->get<fmi2GetStringTYPE>("fmi2GetString");

    commonFunctions->fmi2SetReal = lib->get<fmi2SetRealTYPE>("fmi2SetReal");
    commonFunctions->fmi2SetInteger = lib->get<fmi2SetIntegerTYPE>("fmi2SetInteger");
    commonFunctions->fmi2SetBoolean = lib->get<fmi2SetBooleanTYPE>("fmi2SetBoolean");
    commonFunctions->fmi2SetString = lib->get<fmi2SetStringTYPE>("fmi2SetString");

    /* Getting and setting the internal FMU state */
    commonFunctions->fmi2GetFMUstate = lib->get<fmi2GetFMUstateTYPE>("fmi2GetFMUstate");
    commonFunctions->fmi2SetFMUstate = lib->get<fmi2SetFMUstateTYPE>("fmi2SetFMUstate");
    commonFunctions->fmi2FreeFMUstate = lib->get<fmi2FreeFMUstateTYPE>("fmi2FreeFMUstate");
    commonFunctions->fmi2SerializedFMUstateSize =
        lib->get<fmi2SerializedFMUstateSizeTYPE>("fmi2SerializedFMUstateSize");
    commonFunctions->fmi2SerializeFMUstate =
        lib->get<fmi2SerializeFMUstateTYPE>("fmi2SerializeFMUstate");
    commonFunctions->fmi2DeSerializeFMUstate =
        lib->get<fmi2DeSerializeFMUstateTYPE>("fmi2DeSerializeFMUstate");

    /* Getting partial derivatives */
    commonFunctions->fmi2GetDirectionalDerivative =
        lib->get<fmi2GetDirectionalDerivativeTYPE>("fmi2GetDirectionalDerivative");
}
void fmiLibrary::loadModelExchangeFunctions()
{
    ModelExchangeFunctions = std::make_shared<fmiModelExchangeFunctions>();

    ModelExchangeFunctions->lib = lib;
    ModelExchangeFunctions->fmi2EnterEventMode =
        lib->get<fmi2EnterEventModeTYPE>("fmi2EnterEventMode");
    ModelExchangeFunctions->fmi2NewDiscreteStates =
        lib->get<fmi2NewDiscreteStatesTYPE>("fmi2NewDiscreteStates");
    ModelExchangeFunctions->fmi2EnterContinuousTimeMode =
        lib->get<fmi2EnterContinuousTimeModeTYPE>("fmi2EnterContinuousTimeMode");
    ModelExchangeFunctions->fmi2CompletedIntegratorStep =
        lib->get<fmi2CompletedIntegratorStepTYPE>("fmi2CompletedIntegratorStep");

    /* Providing independent variables and re-initialization of caching */
    ModelExchangeFunctions->fmi2SetTime = lib->get<fmi2SetTimeTYPE>("fmi2SetTime");
    ModelExchangeFunctions->fmi2SetContinuousStates =
        lib->get<fmi2SetContinuousStatesTYPE>("fmi2SetContinuousStates");

    /* Evaluation of the model equations */
    ModelExchangeFunctions->fmi2GetDerivatives =
        lib->get<fmi2GetDerivativesTYPE>("fmi2GetDerivatives");
    ModelExchangeFunctions->fmi2GetEventIndicators =
        lib->get<fmi2GetEventIndicatorsTYPE>("fmi2GetEventIndicators");
    ModelExchangeFunctions->fmi2GetContinuousStates =
        lib->get<fmi2GetContinuousStatesTYPE>("fmi2GetContinuousStates");
    ModelExchangeFunctions->fmi2GetNominalsOfContinuousStates =
        lib->get<fmi2GetNominalsOfContinuousStatesTYPE>("fmi2GetNominalsOfContinuousStates");
}
void fmiLibrary::loadCoSimFunctions()
{
    CoSimFunctions = std::make_shared<fmiCoSimFunctions>();
    CoSimFunctions->lib = lib;
    CoSimFunctions->fmi2SetRealInputDerivatives =
        lib->get<fmi2SetRealInputDerivativesTYPE>("fmi2SetRealInputDerivatives");
    CoSimFunctions->fmi2GetRealOutputDerivatives =
        lib->get<fmi2GetRealOutputDerivativesTYPE>("fmi2GetRealOutputDerivatives");

    CoSimFunctions->fmi2DoStep = lib->get<fmi2DoStepTYPE>("fmi2DoStep");
    CoSimFunctions->fmi2CancelStep = lib->get<fmi2CancelStepTYPE>("fmi2CancelStep");

    /* Inquire slave status */
    CoSimFunctions->fmi2GetStatus = lib->get<fmi2GetStatusTYPE>("fmi2GetStatus");
    CoSimFunctions->fmi2GetRealStatus = lib->get<fmi2GetRealStatusTYPE>("fmi2GetRealStatus");
    CoSimFunctions->fmi2GetIntegerStatus =
        lib->get<fmi2GetIntegerStatusTYPE>("fmi2GetIntegerStatus");
    CoSimFunctions->fmi2GetBooleanStatus =
        lib->get<fmi2GetBooleanStatusTYPE>("fmi2GetBooleanStatus");
    CoSimFunctions->fmi2GetStringStatus = lib->get<fmi2GetStringStatusTYPE>("fmi2GetStringStatus");
}

void fmiLibrary::makeCallbackFunctions()
{
    callbacks = std::make_shared<fmi2CallbackFunctions_nc>();
    callbacks->allocateMemory = &calloc;
    callbacks->freeMemory = &free;
    callbacks->logger = &loggerFunc;
    callbacks->componentEnvironment = static_cast<void*>(this);
}

#define STRING_BUFFER_SIZE 1000
// NOLINTNEXTLINE
void loggerFunc(fmi2ComponentEnvironment /* compEnv */,
                fmi2String /* instanceName */,
                fmi2Status /* status */,
                fmi2String /* category */,
                fmi2String message,
                ...)
{
    std::array<char, STRING_BUFFER_SIZE> temp;  // NOLINT
    va_list arglist;
    va_start(arglist, message);  // NOLINT
    vsnprintf(temp.data(), STRING_BUFFER_SIZE, message, arglist);
    va_end(arglist);
    printf("%s\n", temp.data());  // NOLINT
}
