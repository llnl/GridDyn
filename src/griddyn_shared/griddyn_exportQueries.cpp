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
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto val = createGrabber(queryString, comp);
    if (!val->loaded) {
        return nullptr;
    }
    if (val->vectorGrab) {
        return nullptr;
    }
    auto* grabber = val.release();
    return static_cast<GridDynSingleQuery>(grabber);
}

GridDynVectorQuery
    gridDynVectorQueryCreate(GridDynObject obj, const char* queryString, GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* mquery = new collector();
    mquery->add(queryString, comp);

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
    auto* mGrabber = static_cast<collector*>(query);

    mGrabber->grabData(data, N);
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
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* col = static_cast<collector*>(query);

    col->add(queryString, comp);
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
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* grabber = static_cast<gridGrabber*>(query);
    grabber->updateField(queryString);
    grabber->updateObject(comp);
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
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* col = static_cast<collector*>(query);
    col->reset();
    col->add(queryString, comp);
}
