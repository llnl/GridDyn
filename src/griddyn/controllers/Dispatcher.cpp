/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Dispatcher.h"

#include "core/CoreExceptions.h"
#include <string>

namespace griddyn {
Dispatcher::Dispatcher(const std::string& objName): CoreObject(objName) {}

Dispatcher::~Dispatcher() = default;
CoreObject* Dispatcher::clone(CoreObject* /*obj*/) const
{
    return nullptr;
}

void Dispatcher::moveSchedulers(Dispatcher* /*dis*/) {}
double Dispatcher::initialize(coreTime /*time0*/, double /*dispatch*/)
{
    return 0;
}

double Dispatcher::updateP(coreTime /*time*/, double /*required*/, double /*targetTime*/)
{
    return 0;
}
double Dispatcher::testP(coreTime /*time*/, double /*required*/, double /*targetTime*/)
{
    return 0;
}

void Dispatcher::add(CoreObject* /*obj*/)
{
    throw(ObjectAddFailure(this));
}
void Dispatcher::add(scheduler* /*sched*/)
{
    throw(ObjectAddFailure(this));
}
void Dispatcher::remove(CoreObject* /*obj*/) {}
void Dispatcher::remove(scheduler* /*sched*/) {}

void Dispatcher::set(std::string_view param, std::string_view val)
{
    CoreObject::set(param, val);
}
void Dispatcher::set(std::string_view param, double val, units::unit unitType)
{
    CoreObject::set(param, val, unitType);
}

void Dispatcher::checkGen() {}

void Dispatcher::dispatch(double /*level*/) {}

}  // namespace griddyn
