/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreObject.h"
#include <type_traits>
namespace griddyn {
/**
* @brief clone an object through its existing class hierarchy
@tparam A child class of the original object
@tparam B class of the parent object
* @param[in] originalObject of class A to be cloned
* @param[in] obj pointer of an object to clone to or a null pointer if a new object needs to be
created
* @return pointer to the cloned object
*/
template<class A, class B>
A* cloneBase(const A* originalObject, CoreObject* obj)
{
    static_assert(std::is_base_of<B, A>::value,
                  "classes A and B must have parent child relationship");
    static_assert(std::is_base_of<CoreObject, B>::value,
                  "classes must be inherited from CoreObject");
    static_assert(std::is_base_of<CoreObject, A>::value,
                  "classes must be inherited from CoreObject");
    A* clonedObject;
    if (obj == nullptr) {
        clonedObject = new A(originalObject->getName());
    } else {
        clonedObject = dynamic_cast<A*>(obj);
        if (clonedObject == nullptr) {
            // if we can't cast the pointer clone at the next lower level
            originalObject->B::clone(obj);
            return nullptr;
        }
    }
    // clone everything in the parent object and above
    originalObject->B::clone(clonedObject);
    return clonedObject;
}

}  // namespace griddyn
