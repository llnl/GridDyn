/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ObjectInterpreter.h"

#include "gmlc/utilities/stringConversion.h"
#include <charconv>
#include <string>

namespace griddyn {
namespace {
    CoreObject* locateObjectImpl(std::string_view selector,
                                 const CoreObject* rootObj,
                                 bool rootSearch,
                                 bool directFind,
                                 bool allowFieldSuffix);
}  // namespace

ObjectInfo::ObjectInfo(std::string_view istring, const CoreObject* obj)
{
    loadInfo(istring, obj);
}
void ObjectInfo::loadInfo(std::string_view istring, const CoreObject* obj)
{
    // get the object which to grab from
    size_t rlc = istring.find_last_of(":?");
    if (rlc != std::string::npos) {
        mObject = locateObjectImpl(istring.substr(0, rlc), obj, true, true, false);

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

CoreObject* locateObject(std::string_view istring,
                         const CoreObject* rootObj,
                         bool rootSearch,
                         bool directFind)
{
    return locateObjectImpl(istring, rootObj, rootSearch, directFind, true);
}

namespace {
    // NOLINTNEXTLINE(misc-no-recursion)
    CoreObject* locateObjectImpl(std::string_view selector,
                                 const CoreObject* rootObj,
                                 bool rootSearch,
                                 bool directFind,
                                 bool allowFieldSuffix)
    {
        if (rootObj == nullptr || selector.empty()) {
            return nullptr;
        }
        const auto separator = selector.find_first_of(":/?");
        auto mname = selector.substr(0, separator);
        std::string_view remainder;
        bool hasChild = false;
        if (separator != std::string_view::npos) {
            const auto suffix = selector.substr(separator);
            if (suffix.starts_with("::")) {
                hasChild = true;
                remainder = suffix.substr(2);
            } else if (suffix.starts_with('/')) {
                hasChild = true;
                remainder = suffix.substr(1);
            } else if (!allowFieldSuffix) {
                // ObjectInfo has already removed the field from the selector.
                return nullptr;
            }
        }
        if (mname.empty()) {
            return nullptr;
        }

        CoreObject* obj = nullptr;
        if (mname == rootObj->getName()) {
            obj = const_cast<CoreObject*>(rootObj);
        } else if (mname.starts_with('@')) {
            // implies searching the parent object as well
            mname.remove_prefix(1);
            if (mname.empty()) {
                return nullptr;
            }
            obj = rootObj->find(mname);
            if (const auto* parent = rootObj->getParent(); obj == nullptr && parent != nullptr) {
                obj = parent->find(mname);
            }
        } else {
            if ((mname != selector) || directFind) {
                obj = rootObj->find(mname);
            }
            if (obj == nullptr) {
                auto rlc2 = mname.find_last_of("#$!");
                if (rlc2 != std::string::npos) {
                    auto type =
                        gmlc::utilities::convertToLowerCase(std::string{mname.substr(0, rlc2)});
                    if (type.empty()) {
                        type = "subobject";
                    }
                    auto number = mname.substr(rlc2 + 1);
                    if (number.starts_with('+')) {
                        number.remove_prefix(1);
                    }
                    const auto* end = number.data() + number.size();
                    int onum = 0;
                    const auto result = std::from_chars(number.data(), end, onum);
                    if (result.ec == std::errc{} && result.ptr == end && !number.starts_with('-')) {
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

        return hasChild ? locateObjectImpl(remainder, obj, false, true, allowFieldSuffix) : obj;
    }
}  // namespace

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
