/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "core/coreOwningPtr.hpp"
#include "core/objectFactory.hpp"
#include "griddyn/Area.h"
#include "griddyn/Block.h"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/Relay.h"
#include "griddyn/Source.h"
#include "griddyn/controllers/scheduler.h"
#include "griddyn/gridBus.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/gridSubModel.h"
#include "griddyn/relays/sensor.h"
#include "griddyn_export.h"
#include "internal/griddyn_export_internal.h"
#include <cstring>
#include <map>

using griddyn::Area;
using griddyn::Block;
using griddyn::CoreObject;
using griddyn::coreObjectFactory;
using griddyn::coreOwningPtr;
using griddyn::Exciter;
using griddyn::Generator;
using griddyn::GenModel;
using griddyn::Governor;
using griddyn::gridBus;
using griddyn::GridComponent;
using griddyn::gridDynSimulation;
using griddyn::GridSubModel;
using griddyn::kNullVal;
using griddyn::Link;
using griddyn::Load;
using griddyn::Relay;
using griddyn::scheduler;
using griddyn::sensor;
using griddyn::Source;

static constexpr char invalidComponent[] = "the Griddyn object is not valid";

GridDynObject createGridDynObject(GridComponent* comp)
{
    if (comp == nullptr) {
        return nullptr;
    }
    auto* componentPointer = new coreOwningPtr<GridComponent>(comp);
    return componentPointer;
}

GridComponent* getComponentPointer(GridDynObject obj)
{
    if (obj != nullptr) {
        auto* componentPointer = static_cast<coreOwningPtr<GridComponent>*>(obj);
        return componentPointer->get();
    }
    return nullptr;
}

const GridComponent* getConstComponentPointer(GridDynObject obj)
{
    if (obj != nullptr) {
        const auto* componentPointer = static_cast<coreOwningPtr<GridComponent> const*>(obj);
        return componentPointer->get();
    }
    return nullptr;
}

GridDynObject
    gridDynObjectCreate(const char* componentType, const char* objectType, GridDynError* err)
{
    auto* newObject = coreObjectFactory::instance()->createObject(componentType, objectType);
    if (newObject == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* comp = dynamic_cast<GridComponent*>(newObject);
    if (comp == nullptr) {
        return nullptr;
    }
    auto* componentPointer = new coreOwningPtr<GridComponent>(comp);
    return componentPointer;
}

GridDynObject gridDynObjectClone(GridDynObject obj, GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* newObject = comp->clone();
    auto* componentClone = dynamic_cast<GridComponent*>(newObject);
    if (componentClone == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* componentPointer = new coreOwningPtr<GridComponent>(componentClone);
    return componentPointer;
}

void gridDynObjectFree(GridDynObject obj)
{
    if (obj != nullptr) {
        auto* componentPointer = static_cast<coreOwningPtr<GridComponent>*>(obj);
        delete componentPointer;
    }
}

void gridDynObjectAdd(GridDynObject parentObject, GridDynObject objectToAdd, GridDynError* err)
{
    GridComponent* parent = getComponentPointer(parentObject);
    CoreObject* child = getComponentPointer(objectToAdd);

    try {
        parent->add(child);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynObjectRemove(GridDynObject parentObject,
                         GridDynObject objectToRemove,
                         GridDynError* err)
{
    GridComponent* parent = getComponentPointer(parentObject);
    CoreObject* child = getComponentPointer(objectToRemove);

    if ((parent == nullptr) || (child == nullptr)) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    try {
        parent->remove(child);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynObjectSetString(GridDynObject obj,
                            const char* parameter,
                            const char* value,
                            GridDynError* err)
{
    GridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    try {
        comp->set(parameter, value);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynObjectSetValue(GridDynObject obj,
                           const char* parameter,
                           double value,
                           GridDynError* err)
{
    GridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    try {
        comp->set(parameter, value);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynObjectSetValueUnits(GridDynObject obj,
                                const char* parameter,
                                double value,
                                const char* units,
                                GridDynError* err)
{
    GridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto unitType = (units == nullptr) ? units::defunit : units::unit_cast_from_string(units);
    try {
        comp->set(parameter, value, unitType);
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynObjectSetFlag(GridDynObject obj, const char* flag, int val, GridDynError* err)
{
    GridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    try {
        // ???
        comp->set(flag, static_cast<double>(val != 0));
    }
    catch (...) {
        griddynErrorHandler(err);
    }
}

void gridDynObjectGetString(GridDynObject obj,
                            const char* parameter,
                            char* value,
                            int valueLength,
                            int* actualSize,
                            GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto stringValue = comp->getString(parameter);
    strncpy(value, stringValue.c_str(), valueLength);
    if (actualSize != nullptr) {
        *actualSize = static_cast<int>(stringValue.size());
    }
}

static constexpr char unknownParameter[] = "the given parameter was not recognized";

double gridDynObjectGetValue(GridDynObject obj, const char* parameter, GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return kNullVal;
    }
    try {
        const double result = comp->get(parameter);
        if (result == kNullVal) {
            assignError(err, griddyn_error_unknown_parameter, unknownParameter);
        }
        return result;
    }
    catch (...) {
        griddynErrorHandler(err);
    }
    return kNullVal;
}

double gridDynObjectGetValueUnits(GridDynObject obj,
                                  const char* parameter,
                                  const char* units,
                                  GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return kNullVal;
    }

    auto unitType = (units == nullptr) ? units::defunit : units::unit_cast_from_string(units);
    try {
        const double result = comp->get(parameter, unitType);
        if (result == kNullVal) {
            assignError(err, griddyn_error_unknown_parameter, unknownParameter);
            return result;
        }
        return result;
    }
    catch (...) {
        griddynErrorHandler(err);
    }
    return kNullVal;
}

int gridDynObjectGetFlag(GridDynObject obj, const char* flag, GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return 0;
    }
    try {
        auto res = comp->getFlag(flag);
        return res ? 1 : 0;
    }
    catch (...) {
        griddynErrorHandler(err);
    }
    return 0;
}

GridDynObject gridDynObjectFind(GridDynObject obj, const char* objectToFind, GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* res = comp->find(objectToFind);
    if (res == nullptr) {
        return nullptr;
    }
    auto* compNew = dynamic_cast<GridComponent*>(res);
    if (compNew == nullptr) {
        return nullptr;
    }
    return createGridDynObject(compNew);
}

GridDynObject gridDynObjectGetSubObject(GridDynObject obj,
                                        const char* componentType,
                                        int objectIndex,
                                        GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);
    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }

    auto* res = comp->getSubObject(componentType, static_cast<index_t>(objectIndex));
    if (res == nullptr) {
        return nullptr;
    }
    auto* compNew = dynamic_cast<GridComponent*>(res);
    if (compNew == nullptr) {
        return nullptr;
    }
    return createGridDynObject(compNew);
}

GridDynObject gridDynObjectFindByUserId(GridDynObject obj,
                                        const char* componentType,
                                        int userId,
                                        GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* res = comp->findByUserID(componentType, static_cast<index_t>(userId));
    if (res == nullptr) {
        return nullptr;
    }
    auto* compNew = dynamic_cast<GridComponent*>(res);
    if (compNew == nullptr) {
        return nullptr;
    }
    return createGridDynObject(compNew);
}

GridDynObject gridDynObjectGetParent(GridDynObject obj, GridDynError* err)
{
    const auto* comp = getConstComponentPointer(obj);
    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return nullptr;
    }
    auto* compNew = dynamic_cast<GridComponent*>(comp->getParent());
    if (compNew == nullptr) {
        return nullptr;
    }
    return createGridDynObject(compNew);
}
static const char* invalid_str = "invalid";
static const char* bus_str = "bus";
static const char* area_str = "area";
static const char* link_str = "link";
static const char* load_str = "load";
static const char* generator_str = "generator";

static const char* sim_str = "simulation";
static const char* exciter_str = "exciter";
static const char* scheduler_str = "scheduler";
static const char* governor_str = "governor";
static const char* genModel_str = "genModel";
static const char* block_str = "block";
static const char* source_str = "source";
static const char* relay_str = "relay";
static const char* sensor_str = "sensor";
static const char* submodel_str = "submodel";
static const char* unknown_str = "unknown";

const char* gridDynObjectGetType(GridDynObject obj)
{
    const auto* comp = getConstComponentPointer(obj);

    if (comp == nullptr) {
        return invalid_str;
    }
    if (dynamic_cast<const gridBus*>(comp) != nullptr) {
        return bus_str;
    }
    if (dynamic_cast<const Link*>(comp) != nullptr) {
        return link_str;
    }
    if (dynamic_cast<const gridDynSimulation*>(comp) != nullptr) {
        return sim_str;
    }
    if (dynamic_cast<const Area*>(comp) != nullptr) {
        return area_str;
    }
    if (dynamic_cast<const Load*>(comp) != nullptr) {
        return load_str;
    }
    if (dynamic_cast<const Generator*>(comp) != nullptr) {
        return generator_str;
    }
    if (dynamic_cast<const Governor*>(comp) != nullptr) {
        return governor_str;
    }
    if (dynamic_cast<const Exciter*>(comp) != nullptr) {
        return exciter_str;
    }
    if (dynamic_cast<const GenModel*>(comp) != nullptr) {
        return genModel_str;
    }
    if (dynamic_cast<const scheduler*>(comp) != nullptr) {
        return scheduler_str;
    }
    if (dynamic_cast<const Source*>(comp) != nullptr) {
        return source_str;
    }
    if (dynamic_cast<const Block*>(comp) != nullptr) {
        return block_str;
    }
    if (dynamic_cast<const sensor*>(comp) != nullptr) {
        return sensor_str;
    }
    if (dynamic_cast<const Relay*>(comp) != nullptr) {
        return relay_str;
    }

    if (dynamic_cast<const GridSubModel*>(comp) != nullptr) {
        return submodel_str;
    }
    return unknown_str;
}
