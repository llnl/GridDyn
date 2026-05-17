/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "coreObject.h"
#include <type_traits>
namespace griddyn {
/**
* @brief template helper function for the getParameter String function
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

/**
 * @brief template helper function for the getParameter String function
 * @param[in] cobj object to query for inherited parameter strings
 * @param[out] pstr list of available parameter strings matching the requested type
 * @param[in] numStr local parameter strings that take numeric arguments
 * @param[in] strStr local parameter strings that take string arguments
 * @param[in] flagStr local flag strings
 * @param[in] pstype requested parameter string category to return
 */
template<class A, class B>
void getParamString(const A* cobj,
                    stringVec& pstr,
                    const stringVec& numStr,
                    const stringVec& strStr,
                    const stringVec& flagStr,
                    paramStringType pstype)
{
    static_assert(std::is_base_of<B, A>::value,
                  "classes A and B must have parent child relationship");
    static_assert(std::is_base_of<CoreObject, B>::value,
                  "classes must be inherited from CoreObject");
    static_assert(std::is_base_of<CoreObject, A>::value,
                  "classes must be inherited from CoreObject");
    switch (pstype) {
        case paramStringType::all:
            pstr.reserve(pstr.size() + numStr.size());
            pstr.insert(pstr.end(), numStr.begin(), numStr.end());
            cobj->B::getParameterStrings(pstr, paramStringType::numeric);
            pstr.reserve(pstr.size() + strStr.size() + 1);
            pstr.emplace_back("#");
            pstr.insert(pstr.end(), strStr.begin(), strStr.end());
            cobj->B::getParameterStrings(pstr, paramStringType::str);
            break;
        case paramStringType::localnum:
            pstr = numStr;
            break;
        case paramStringType::localstr:
            pstr = strStr;
            break;
        case paramStringType::localflags:
            pstr = flagStr;
            break;
        case paramStringType::numeric:
            pstr.reserve(pstr.size() + numStr.size());
            pstr.insert(pstr.end(), numStr.begin(), numStr.end());
            cobj->B::getParameterStrings(pstr, paramStringType::numeric);
            break;
        case paramStringType::str:
            pstr.reserve(pstr.size() + strStr.size());
            pstr.insert(pstr.end(), strStr.begin(), strStr.end());
            cobj->B::getParameterStrings(pstr, paramStringType::str);
            break;
        case paramStringType::flags:
            pstr.reserve(pstr.size() + flagStr.size());
            pstr.insert(pstr.end(), flagStr.begin(), flagStr.end());
            cobj->B::getParameterStrings(pstr, paramStringType::flags);
            break;
    }
}

}  // namespace griddyn
