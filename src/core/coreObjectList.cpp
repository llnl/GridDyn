/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "coreObjectList.h"

#include <string>
#include <string_view>
#include <vector>
namespace griddyn {
bool coreObjectList::insert(coreObject* obj, bool replace)
{
    auto inp = m_objects.insert(obj);
    if (inp.second) {
        return true;
    }
    if (replace) {
        m_objects.replace(inp.first, obj);
        return true;
    }
    return false;
}
coreObject* coreObjectList::find(std::string_view objName) const
{
    auto foundObject = m_objects.get<name>().find(std::string{objName});
    if (foundObject != m_objects.get<name>().end()) {
        return (*foundObject);
    }
    return nullptr;
}

std::vector<coreObject*> coreObjectList::find(index_t searchID) const
{
    auto foundObject = m_objects.get<uid>().lower_bound(searchID);
    auto foundObjectEnd = m_objects.get<uid>().upper_bound(searchID);
    std::vector<coreObject*> out;
    while (foundObject != foundObjectEnd) {
        if ((*foundObject)->getUserID() == searchID) {
            out.push_back(*foundObject);
        }
        ++foundObject;
    }
    return out;
}

bool coreObjectList::remove(coreObject* obj)
{
    auto foundObject = m_objects.get<id>().find(obj->getID());
    if (foundObject != m_objects.get<id>().end()) {
        m_objects.erase(foundObject);
        return true;
    }
    return false;
}

bool coreObjectList::remove(std::string_view objName)
{
    auto foundObject = m_objects.get<name>().find(std::string{objName});
    if (foundObject != m_objects.get<name>().end()) {
        // I don't know why I have to do this find on the id index
        // Not understanding these multindex objects well enough I guess
        auto foundById = m_objects.get<id>().find((*foundObject)->getID());
        m_objects.erase(foundById);

        return true;
    }
    return false;
}

bool coreObjectList::isMember(const coreObject* obj) const
{
    auto foundObject = m_objects.get<id>().find(obj->getID());
    return (foundObject != m_objects.get<id>().end());
}

void coreObjectList::deleteAll(coreObject* parent)
{
    for (auto* objectPtr : m_objects) {
        removeReference(objectPtr, parent);
    }
}

void coreObjectList::updateObject(coreObject* obj)
{
    auto foundObject = m_objects.get<id>().find(obj->getID());
    m_objects.replace(foundObject, obj);
}

}  // namespace griddyn
