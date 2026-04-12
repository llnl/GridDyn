/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dispatcher.h"

#include "core/coreExceptions.h"
#include <string>

namespace griddyn {
dispatcher::dispatcher(const std::string& objName): coreObject(objName) {}

dispatcher::~dispatcher() = default;
coreObject* dispatcher::clone(coreObject* /*obj*/) const
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

void dispatcher::add(coreObject* /*obj*/)
{
    throw(objectAddFailure(this));
}
void dispatcher::add(scheduler* /*sched*/)
{
    throw(objectAddFailure(this));
}
void dispatcher::remove(coreObject* /*obj*/) {}
void dispatcher::remove(scheduler* /*sched*/) {}

void dispatcher::set(const std::string& param, const std::string& val)
{
    return coreObject::set(param, val);
}
void dispatcher::set(const std::string& param, double val, units::unit unitType)
{
    return coreObject::set(param, val, unitType);
}

void dispatcher::checkGen() {}

void dispatcher::dispatch(double /*level*/) {}

}  // namespace griddyn
