/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_CORE_COREOBJECTTEMPLATES_HPP_
#define ___W_GRIDDYN_GRIDDYN_SRC_CORE_COREOBJECTTEMPLATES_HPP_

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
A* cloneBase(const A* originalObject, coreObject* obj)
{
    static_assert(std::is_base_of<B, A>::value,
                  "classes A and B must have parent child relationship");
    static_assert(std::is_base_of<coreObject, B>::value,
                  "classes must be inherited from coreObject");
    static_assert(std::is_base_of<coreObject, A>::value,
                  "classes must be inherited from coreObject");
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
 * @param[in] gridCOreObject
 * @param[out] return list of available parameter strings  that match the requested type
 * @param[in] list of parameter strings that take numbers as arguments
 * @param[in] list of parameter strings that take strings as arguments
 * @param[in] list of flags
 * @param[in] paramStringType to determine which parameter strings to return
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
    static_assert(std::is_base_of<coreObject, B>::value,
                  "classes must be inherited from coreObject");
    static_assert(std::is_base_of<coreObject, A>::value,
                  "classes must be inherited from coreObject");
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
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_CORE_COREOBJECTTEMPLATES_HPP_
