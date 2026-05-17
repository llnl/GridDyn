/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "nullObject.h"

#include <cassert>
#include <string>
namespace griddyn {
nullObject::nullObject(std::uint64_t nullCode) noexcept: CoreObject(nullCode)
{
    assert(nullCode <= 100);
    parent = this;
}

nullObject::nullObject(std::string_view objName): CoreObject(objName)
{
    parent = this;
}
CoreObject* nullObject::clone(CoreObject* obj) const
{
    if (obj != nullptr) {
        return CoreObject::clone(obj);
    }
    if (id < 100) {
        return CoreObject::clone(new nullObject(id));
    }
    return CoreObject::clone(new nullObject(getName()));
}

void nullObject::alert(CoreObject* /*obj*/, int /*code*/) {}
void nullObject::log(CoreObject* /*obj*/, print_level /*level*/, const std::string& /*message*/) {}
bool nullObject::shouldLog(print_level /*level*/) const
{
    return false;
}
CoreObject* nullObject::find(std::string_view /*object*/) const
{
    return nullptr;
}
CoreObject* nullObject::findByUserID(std::string_view /*typeName*/, index_t /*searchID*/) const
{
    return nullptr;
}

void nullObject::setParent(CoreObject* /*parentObj*/)
{
    // ignore it (null objects can't have parents)
}

}  // namespace griddyn
