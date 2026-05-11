/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "griddyn/measurement/collector.h"
#include "griddyn/measurement/gridGrabbers.h"
#include "griddyn_export.h"
#include "internal/griddyn_export_internal.h"
#include <memory>
#include <vector>

using griddyn::collector;
using griddyn::createGrabber;
using griddyn::gridComponent;
using griddyn::gridGrabber;
using griddyn::kNullVal;

static constexpr char invalidQuery[] = "the Query object is not valid";
static constexpr char invalidComponent[] = "the Griddyn object is not valid";

GridDynSingleQuery
    gridDynSingleQueryCreate(GridDynObject obj, const char* queryString, GridDynError* err)
{
    gridComponent* component = getComponentPointer(obj);

    if (component == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto grabberCandidate = createGrabber(queryString, component);
    if (!grabberCandidate->loaded) {
        return nullptr;
    }
    if (grabberCandidate->vectorGrab) {
        return nullptr;
    }
    auto* grabber = grabberCandidate.release();
    return static_cast<GridDynSingleQuery>(grabber);
}

GridDynVectorQuery
    gridDynVectorQueryCreate(GridDynObject obj, const char* queryString, GridDynError* err)
{
    gridComponent* component = getComponentPointer(obj);

    if (component == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* queryCollector = new collector();
    queryCollector->add(queryString, component);

    return nullptr;
}

void gridDynSingleQueryFree(GridDynSingleQuery query)
{
    if (query != nullptr) {
        delete static_cast<gridGrabber*>(query);
    }
}

void gridDynVectorQueryFree(GridDynVectorQuery query)
{
    if (query != nullptr) {
        delete static_cast<collector*>(query);
    }
}

double gridDynSingleQueryRun(GridDynSingleQuery query, GridDynError* err)
{
    if (query == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidQuery);
        return kNullVal;
    }
    auto* grabber = static_cast<gridGrabber*>(query);
    return grabber->grabData();
}

void gridDynVectorQueryRun(GridDynVectorQuery query, double* data, int N, GridDynError* err)
{
    if (query == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidQuery);
        return;
    }
    auto* queryCollector = static_cast<collector*>(query);

    queryCollector->grabData(data, N);
}

void gridDynVectorQueryAppend(GridDynVectorQuery query,
                              GridDynObject obj,
                              const char* queryString,
                              GridDynError* err)
{
    if (query == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidQuery);
        return;
    }
    gridComponent* component = getComponentPointer(obj);

    if (component == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* queryCollector = static_cast<collector*>(query);

    queryCollector->add(queryString, component);
}

void gridDynSingleQueryUpdate(GridDynSingleQuery query,
                              GridDynObject obj,
                              const char* queryString,
                              GridDynError* err)
{
    if (query == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidQuery);
        return;
    }
    gridComponent* component = getComponentPointer(obj);

    if (component == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* grabber = static_cast<gridGrabber*>(query);
    grabber->updateField(queryString);
    grabber->updateObject(component);
    if (!grabber->loaded) {
        assignError(err, griddyn_error_query_load_failure, invalidQuery);
        return;
    }
}

void gridDynVectorQueryUpdate(GridDynVectorQuery query,
                              GridDynObject obj,
                              const char* queryString,
                              GridDynError* err)
{
    if (query == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidQuery);
        return;
    }
    gridComponent* component = getComponentPointer(obj);

    if (component == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* queryCollector = static_cast<collector*>(query);
    queryCollector->reset();
    queryCollector->add(queryString, component);
}
