/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "core/coreExceptions.h"
#include "griddyn/events/Event.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn_export.h"
#include "internal/griddyn_export_internal.h"
#include "runner/gridDynRunner.h"
#include <memory>
#include <vector>

using griddyn::Event;
using griddyn::GriddynRunner;
using griddyn::change_code;
using griddyn::make_event;
using griddyn::object_update_mode;

static constexpr char invalidEvent[] = "the Event object is not valid";

GridDynEvent gridDynEventCreate(const char* eventString, GridDynObject obj, GridDynError* err)
{
    try {
        auto* evnt = new std::shared_ptr<Event>(make_event(eventString, getComponentPointer(obj)));
        if (evnt != nullptr) {
            return static_cast<GridDynEvent>(evnt);
        }
        return nullptr;
    }
    catch (...) {
        griddynErrorHandler(err);
    }
    return nullptr;
}

void gridDynEventFree(GridDynEvent evnt)
{
    if (evnt != nullptr) {
        auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);
        delete shr_event;
    }
}

void gridDynEventTrigger(GridDynEvent evnt, GridDynError* err)
{
    if (evnt == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidEvent);
        return;
    }
    auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);
    if (*shr_event) {
        if ((*shr_event)->trigger() >= change_code::no_change) {
            return;
        }
    }
    static constexpr char functionFail[] = "the event failed to execute";
    assignError(err, griddyn_error_function_failure, functionFail);
}

void gridDynEventSchedule(GridDynEvent evnt, GridDynSimulation sim, GridDynError* err)
{
    if (evnt == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidEvent);
        return;
    }
    auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);

    auto* runner = static_cast<GriddynRunner*>(sim);

    if (runner == nullptr) {
        static constexpr char invalidSimulation[] = "the Simulation object is not valid";
        assignError(err, griddyn_error_invalid_object, invalidSimulation);
        return;
    }
    try {
        runner->getSim()->add(*shr_event);
    }
    catch (...) {
        return griddynErrorHandler(err);
    }
}

void gridDynEventSetValue(GridDynEvent evnt, const char* parameter, double value, GridDynError* err)
{
    if (evnt == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidEvent);
        return;
    }
    auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);
    try {
        shr_event->operator->()->set(parameter, value);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynEventSetString(GridDynEvent evnt,
                           const char* parameter,
                           const char* value,
                           GridDynError* err)
{
    if (evnt == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidEvent);
        return;
    }
    auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);
    try {
        shr_event->operator->()->set(parameter, value);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynEventSetFlag(GridDynEvent evnt, const char* flag, int val, GridDynError* err)
{
    if (evnt == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidEvent);
        return;
    }
    auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);
    try {
        shr_event->operator->()->setFlag(flag, (val != 0));
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynEventSetTarget(GridDynEvent evnt, GridDynObject obj, GridDynError* err)
{
    if (evnt == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidEvent);
        return;
    }
    auto* shr_event = static_cast<std::shared_ptr<Event>*>(evnt);
    auto* comp = getComponentPointer(obj);
    if (comp == nullptr) {
        static constexpr char invalidComponent[] = "the target object is not valid";
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    try {
        shr_event->operator->()->updateObject(comp, object_update_mode::match);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}
