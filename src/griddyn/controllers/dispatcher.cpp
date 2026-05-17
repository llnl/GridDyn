/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dispatcher.h"

#include "core/coreExceptions.h"
#include <string>

namespace griddyn {
dispatcher::dispatcher(const std::string& objName): CoreObject(objName) {}

dispatcher::~dispatcher() = default;
CoreObject* dispatcher::clone(CoreObject* /*obj*/) const
{
    return nullptr;
}

void dispatcher::moveSchedulers(dispatcher* /*dis*/) {}
double dispatcher::initialize(coreTime /*time0*/, double /*dispatch*/)
{
    return 0;
}

double dispatcher::updateP(coreTime /*time*/, double /*required*/, double /*targetTime*/)
{
    return 0;
}
double dispatcher::testP(coreTime /*time*/, double /*required*/, double /*targetTime*/)
{
    return 0;
}

void dispatcher::add(CoreObject* /*obj*/)
{
    throw(objectAddFailure(this));
}
void dispatcher::add(scheduler* /*sched*/)
{
    throw(objectAddFailure(this));
}
void dispatcher::remove(CoreObject* /*obj*/) {}
void dispatcher::remove(scheduler* /*sched*/) {}

void dispatcher::set(std::string_view param, std::string_view val)
{
    return CoreObject::set(param, val);
}
void dispatcher::set(std::string_view param, double val, units::unit unitType)
{
    return CoreObject::set(param, val, unitType);
}

void dispatcher::checkGen() {}

void dispatcher::dispatch(double /*level*/) {}

}  // namespace griddyn
