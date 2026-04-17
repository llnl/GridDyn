/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/gridDynVersion.hpp"
#include "griddyn_export.h"
#include "internal/griddyn_export_internal.h"
#include "runner/gridDynRunner.h"
#include <memory>
#include <string>
#include <utility>

using griddyn::buildFlags;
using griddyn::compiler;
using griddyn::coreTime;
using griddyn::executionFailure;
using griddyn::fileOperationError;
using griddyn::gridDynAction;
using griddyn::GriddynRunner;
using griddyn::invalidParameterValue;
using griddyn::kNullVal;
using griddyn::loadFile;
using griddyn::objectAddFailure;
using griddyn::objectRemoveFailure;
using griddyn::unrecognizedObjectException;
using griddyn::unrecognizedParameter;
using griddyn::versionString;

const char* gridDynGetVersion(void)
{
    return versionString;
}

const char* gridDynGetBuildFlags(void)
{
    return buildFlags;
}

const char* gridDynGetCompilerVersion(void)
{
    return compiler;
}

static constexpr const char* nullstrPtr = "";

const char emptyStr[] = "";

GridDynError gridDynErrorInitialize(void)
{
    GridDynError err;
    err.error_code = 0;
    err.message = nullstrPtr;
    return err;
}

/** clear an error object*/
void gridDynErrorClear(GridDynError* err)
{
    if (err != nullptr) {
        err->error_code = 0;
        err->message = nullstrPtr;
    }
}

/** this function is based on the lippencott function template
http://cppsecrets.blogspot.com/2013/12/using-lippincott-function-for.html
*/
static constexpr char unknown_err_string[] = "unknown error";

void griddynErrorHandler(GridDynError* err) noexcept
{
    if (err == nullptr) {
        return;
    }
    try {
        try {
            // this is intended to be a single '='
            if (std::exception_ptr eptr = std::current_exception()) {
                std::rethrow_exception(eptr);
            } else {
                // LCOV_EXCL_START
                err->error_code = griddyn_error_external_type;
                err->message = unknown_err_string;
                // LCOV_EXCL_STOP
            }
        }
        catch (const griddyn::unrecognizedObjectException& uoe) {
            err->error_code = griddyn_error_invalid_object;
            err->message = getMasterHolder()->addErrorString(uoe.what());
        }
        catch (const griddyn::objectAddFailure& af) {
            err->error_code = griddyn_error_add_failure;
            err->message = getMasterHolder()->addErrorString(af.what());
        }
        catch (const griddyn::objectRemoveFailure& rf) {
            err->error_code = griddyn_error_remove_failure;
            err->message = getMasterHolder()->addErrorString(rf.what());
        }
        catch (const griddyn::unrecognizedParameter& up) {
            err->error_code = griddyn_error_unknown_parameter;
            err->message = getMasterHolder()->addErrorString(up.what());
        }
        catch (const griddyn::invalidParameterValue& ip) {
            err->error_code = griddyn_error_invalid_parameter_value;
            err->message = getMasterHolder()->addErrorString(ip.what());
        }
        catch (const griddyn::executionFailure& ef) {
            err->error_code = griddyn_error_function_failure;
            err->message = getMasterHolder()->addErrorString(ef.what());
        }
        catch (const griddyn::cloneFailure& cf) {
            err->error_code = griddyn_error_other;
            err->message = getMasterHolder()->addErrorString(cf.what());
        }
        catch (const griddyn::fileOperationError& cf) {
            err->error_code = griddyn_error_file_load_failure;
            err->message = getMasterHolder()->addErrorString(cf.what());
        }
        catch (const std::exception& exc) {
            err->error_code = griddyn_error_other;
            err->message = getMasterHolder()->addErrorString(exc.what());
        }
        // LCOV_EXCL_START
        catch (...) {
            err->error_code = griddyn_error_external_type;
            err->message = unknown_err_string;
        }
        // LCOV_EXCL_STOP
    }
    // LCOV_EXCL_START
    catch (...) {
        err->error_code = griddyn_error_external_type;
        err->message = unknown_err_string;
    }
    // LCOV_EXCL_STOP
}

GridDynSimulation gridDynSimulationCreate(const char* type, const char* name, GridDynError* err)
{
    static constexpr char invalidSimType[] = "the given simtype is not valid in the shared library";
    GriddynRunner* sim;
    std::string typeStr(type);
    if (typeStr == "helics") {
        assignError(err, griddyn_error_invalid_parameter_value, invalidSimType);
        return nullptr;
    }
    if (typeStr == "buildfmu") {
        assignError(err, griddyn_error_invalid_parameter_value, invalidSimType);
        return nullptr;
    }
    if (typeStr == "dime") {
        assignError(err, griddyn_error_invalid_parameter_value, invalidSimType);
        return nullptr;
    }
    if (typeStr == "buildgdz") {
        assignError(err, griddyn_error_invalid_parameter_value, invalidSimType);
        return nullptr;
    }

    sim = new GriddynRunner();
    if (sim != nullptr) {
        sim->getSim()->setName(name);
    } else {
        static constexpr char creationFailure[] = "unable to create the simulation";
        assignError(err, griddyn_error_function_failure, creationFailure);
    }
    return reinterpret_cast<GridDynSimulation>(sim);
}

static constexpr char invalidSimulation[] = "the simulation object is not valid";

void gridDynSimulationFree(GridDynSimulation sim)
{
    if (sim != nullptr) {
        delete reinterpret_cast<GriddynRunner*>(sim);
    }
}

void gridDynSimulationInitializeFromString(GridDynSimulation sim,
                                           const char* initializationString,
                                           GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    runner->InitializeFromString(initializationString);
}

void gridDynSimulationInitializeFromArgs(GridDynSimulation sim,
                                         int argc,
                                         char* argv[],
                                         int ignoreUnrecognized,
                                         GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    runner->Initialize(argc, argv, (ignoreUnrecognized != 0));
}

void gridDynSimulationLoadfile(GridDynSimulation sim,
                               const char* fileName,
                               const char* fileType,
                               GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }

    try {
        auto typestr = std::string(fileType);
        if (typestr.empty()) {
            loadFile(runner->getSim().get(), fileName);
        } else {
            loadFile(runner->getSim().get(), fileName, nullptr, typestr);
        }
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynSimulationAddCommand(GridDynSimulation sim, const char* command, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    gridDynAction action(command);
    if (action.command != gridDynAction::gd_action_t::invalid) {
        runner->getSim()->add(action);
        return;
    }
    // return griddyn_add_failure;
}

void gridDynSimulationRun(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    try {
        runner->Run();
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

double gridDynSimulationRunTo(GridDynSimulation sim, double runToTime, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return kNullVal;
    }
    try {
        auto time = runner->Step(runToTime);
        return static_cast<double>(time);
    }
    catch (...) {
        griddynErrorHandler(err);
        return kNullVal;
    }
}

double gridDynSimulationStep(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return kNullVal;
    }
    runner->getSim()->step();
    return static_cast<double>(runner->getSim()->getSimulationTime());
}

void gridDynSimulationRunAsync(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    try {
        runner->RunAsync();
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynSimulationRunToAsync(GridDynSimulation sim, double runToTime, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    try {
        runner->StepAsync(runToTime);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynSimulationStepAsync(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
}

int gridDynSimulationGetStatus(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return griddyn_error_invalid_object;
    }
    coreTime tRet;
    auto res = runner->getStatus(tRet);
    return res;
}

GridDynObject getSimulationObject(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return nullptr;
    }
    runner->getSim()->addOwningReference();
    return createGridDynObject(runner->getSim().get());
}

void gridDynSimulationPowerflowInitialize(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    runner->getSim()->pFlowInitialize();
}

void gridDynSimulationPowerflow(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    runner->getSim()->powerflow();
}

void gridDynSimulationDynamicInitialize(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    runner->simInitialize();
}

void gridDynSimulationReset(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    auto val = runner->Reset();
    if (val != griddyn_ok) {
    }
}

double gridDynSimulationGetCurrentTime(GridDynSimulation sim, GridDynError* err)
{
    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return kNullVal;
    }
    return static_cast<double>(runner->getSim()->getSimulationTime());
}

MasterObjectHolder::MasterObjectHolder() noexcept {}

MasterObjectHolder::~MasterObjectHolder() {}

const char* MasterObjectHolder::addErrorString(std::string newError)
{
    auto estring = errorStrings.lock();
    estring->push_back(std::move(newError));
    auto& v = estring->back();
    return v.c_str();
}

std::shared_ptr<MasterObjectHolder> getMasterHolder()
{
    static auto instance = std::make_shared<MasterObjectHolder>();
    static gmlc::concurrency::TripWireTrigger tripTriggerholder;
    return instance;
}
