/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ObjectInterpreter.h"

#include "gmlc/utilities/stringConversion.h"
#include <string>

namespace griddyn {
ObjectInfo::ObjectInfo(std::string_view istring, const CoreObject* obj)
{
    loadInfo(istring, obj);
}
void ObjectInfo::loadInfo(std::string_view istring, const CoreObject* obj)
{
    // get the object which to grab from
    size_t rlc = istring.find_last_of(":?");
    if (rlc != std::string::npos) {
        mObject = locateObject(istring.substr(0, rlc), obj);

        mField = std::string{istring.substr(rlc + 1, std::string::npos)};
    } else {
        mObject = const_cast<CoreObject*>(obj);
        mField = std::string{istring};
    }

    rlc = mField.find_first_of('(');
    if (rlc != std::string::npos) {
        const size_t rlc2 = mField.find_last_of(')');
        mUnitType =
            units::unit_cast(units::unit_from_string(mField.substr(rlc + 1, rlc2 - rlc - 1)));
        mField = gmlc::utilities::convertToLowerCase(mField.substr(0, rlc));
    } else {
        mField = gmlc::utilities::convertToLowerCase(mField);
    }

    gmlc::utilities::stringOps::trimString(mField);
}

// NOLINTNEXTLINE(misc-no-recursion)
CoreObject* locateObject(std::string_view istring,
                         const CoreObject* rootObj,
                         bool rootSearch,
                         bool directFind)
{
    CoreObject* obj = nullptr;
    std::string_view mname = istring;
    std::string secName = "_";
    // get the object which to grab from
    auto rlc = istring.find_first_of(":/?");
    char sep = ' ';
    if (rlc != std::string::npos) {
        mname = istring.substr(0, rlc);
        secName = std::string{istring.substr(rlc + 1)};
        sep = istring[rlc];
    }

    if (mname == rootObj->getName()) {
        obj = const_cast<CoreObject*>(rootObj);
    } else if ((mname[0] == '@') || (mname[0] == '/')) {
        // implies searching the parent object as well
        mname.remove_prefix(1);
        obj = rootObj->find(mname);
        if (obj == nullptr) {
            obj = rootObj->getParent()->find(mname);
        }
    } else {
        if ((mname != istring) || directFind) {
            obj = rootObj->find(mname);
        }
        if (obj == nullptr) {
            auto rlc2 = mname.find_last_of("#$!");
            if (rlc2 != std::string::npos) {
                auto type = gmlc::utilities::convertToLowerCase(std::string{mname.substr(0, rlc2)});
                if (type.empty()) {
                    type = "subobject";
                }
                auto num = std::string{mname.substr(rlc2 + 1)};
                auto onum = gmlc::utilities::numeric_conversion<int>(num, -1);
                if (onum >= 0) {
                    switch (mname[rlc2]) {
                        case '$':  //$ means get by id
                            obj = rootObj->findByUserID(type, onum);
                            break;
                        case '!':
                        case '#':
                            obj = rootObj->getSubObject(type, onum);
                            break;
                        default:
                            break;
                    }
                }
            } else if (rootSearch) {
                auto* rootObject2 = rootObj->getRoot();
                obj = rootObject2->find(mname);
            }
        }
    }

    if ((sep == '/') && (obj != nullptr)) {
        // we have a '/' so go into the sub model
        obj = locateObject(secName, obj, false);
    } else if ((secName[0] == ':') && (obj != nullptr)) {
        // we have a double colon so go deeper in the object using the
        // found object as the base
        obj = locateObject(secName.substr(1), obj, false);
    }
    return obj;
}

CoreObject* findMatchingObject(CoreObject* obj, CoreObject* root)
{
    const CoreObject* par = obj;

    stringVec stackNames;
    while (par->getName() != root->getName()) {
        stackNames.push_back(par->getName());
        par = par->getParent();
        if (par == nullptr) {
            break;
        }
    }
    // now trace back through the new root object
    CoreObject* matchObj = root;
    while (!stackNames.empty()) {
        matchObj = matchObj->find(stackNames.back());
        stackNames.pop_back();
        if (matchObj == nullptr) {
            break;
        }
    }
    if (matchObj != nullptr) {
        if (matchObj->getName() == obj->getName()) {
            return matchObj;
        }
    }
    return nullptr;
}

}  // namespace griddyn
