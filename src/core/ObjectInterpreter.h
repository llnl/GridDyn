/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreObject.h"
#include <string>
#include <string_view>

namespace griddyn {
/** @brief class for constructing some info about an object
 generally used for interpreting an object string with object and field references and possibly
 units as well
*/
class ObjectInfo {
  public:
    CoreObject* mObject = nullptr;  //!< pointer to the object being referenced
    std::string mField;  //!< the field referenced
    units::unit mUnitType = units::defunit;  //!< the units corresponding to the reference

    /** @brief default constructor*/
    ObjectInfo() = default;
    /** @brief constructor with the string to interpret and a base object to begin the search
    process for
    @param[in] istring the input string containing the object and field reference
    @param[in] obj the object used as the basis for the search if needed
    */
    ObjectInfo(std::string_view istring, const CoreObject* obj);

    /** @brief load a string similar to the constructor except with an existing object
     the string should be of the form objA::subObject:field(units) const
    @param[in] istring the input string containing the object and field reference
            @param[in] obj the object used as the basis for the search if needed
            */
    void loadInfo(std::string_view istring, const CoreObject* obj);
    void LoadInfo(std::string_view istring, const CoreObject* obj)  // NOLINT
    {
        loadInfo(istring, obj);
    }
};

using ObjInfo = ObjectInfo;  // NOLINT(readability-identifier-naming)
using objInfo = ObjectInfo;  // NOLINT(readability-identifier-naming)

/** @brief locate a specific object by name
 the string should be of the form obj::subobj:field, or /obj/subobj?field,  field is optional but
"::" or "/" defines parent child relationships along the search path obj and subobj descriptions can
take a number of forms either the name or specific description if a parent can only contain 1 of
that type of object or type#N  where type is the type of subObject and N is the index number
starting from 0 or type$ID  where type is the type of subObject and ID is the user ID of the object
@param[in] istring the string containing the object name
@param[in] rootObj the object where the search is started
@param[in] rootSearch is set to true and the object can't be located from rootObj then the function
will attempt to locate a root object and start the search over from there.
@param[in] directFind if direct find is set to false then the find function is not called unless the
search string was modified to prevent recursion in some find calls
*/
CoreObject* locateObject(std::string_view istring,
                         const CoreObject* rootObj,
                         bool rootSearch = true,
                         bool directFind = true);

/** @brief locate a matching object in a new tree
meant to target cloning operations where pointers need to be mapped to a new hierarchy
@param[in] obj the existing object
@param[in] root the root of the new tree to locate a corresponding object
*/
CoreObject* findMatchingObject(CoreObject* obj, CoreObject* root);

}  // namespace griddyn
