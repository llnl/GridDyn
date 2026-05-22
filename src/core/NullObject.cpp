/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "NullObject.h"

#include <cassert>
#include <string>
namespace griddyn {
NullObject::NullObject(std::uint64_t nullCode) noexcept: CoreObject(nullCode)
{
    assert(nullCode <= 100);
    parent = this;
}

NullObject::NullObject(std::string_view objName): CoreObject(objName)
{
    parent = this;
}
CoreObject* NullObject::clone(CoreObject* obj) const
{
    if (obj != nullptr) {
        return CoreObject::clone(obj);
    }
    if (id < 100) {
        return CoreObject::clone(new NullObject(id));
    }
    return CoreObject::clone(new NullObject(getName()));
}

void NullObject::alert(CoreObject* /*obj*/, int /*code*/) {}
void NullObject::log(CoreObject* /*obj*/, PrintLevel /*level*/, const std::string& /*message*/) {}
bool NullObject::shouldLog(PrintLevel /*level*/) const
{
    return false;
}
CoreObject* NullObject::find(std::string_view /*object*/) const
{
    return nullptr;
}
CoreObject* NullObject::findByUserID(std::string_view /*typeName*/, index_t /*searchID*/) const
{
    return nullptr;
}

void NullObject::setParent(CoreObject* /*parentObj*/)
{
    // ignore it (null objects can't have parents)
}

}  // namespace griddyn
