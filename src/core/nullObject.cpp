/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "nullObject.h"

#include <cassert>
#include <string>
namespace griddyn {
nullObject::nullObject(std::uint64_t nullCode) noexcept: coreObject(nullCode)
{
    assert(nullCode <= 100);
    parent = this;
}

nullObject::nullObject(std::string_view objName): coreObject(objName)
{
    parent = this;
}
coreObject* nullObject::clone(coreObject* obj) const
{
    if (obj != nullptr) {
        return coreObject::clone(obj);
    }
    if (id < 100) {
        return coreObject::clone(new nullObject(id));
    }
    return coreObject::clone(new nullObject(getName()));
}

void nullObject::alert(coreObject* /*obj*/, int /*code*/) {}
void nullObject::log(coreObject* /*obj*/, print_level /*level*/, const std::string& /*message*/) {}
coreObject* nullObject::find(std::string_view /*object*/) const
{
    return nullptr;
}
coreObject* nullObject::findByUserID(std::string_view /*typeName*/, index_t /*searchID*/) const
{
    return nullptr;
}

void nullObject::setParent(coreObject* /*parentObj*/)
{
    // ignore it (null objects can't have parents)
}

}  // namespace griddyn
