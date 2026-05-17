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
void coreObjectList::addIndexes(id_type_t objectID, const objectRecord& record)
{
    m_idsByName.emplace(record.name, objectID);
    m_idsByUserId.emplace(record.userID, objectID);
}

void coreObjectList::removeIndexes(id_type_t objectID, const objectRecord& record)
{
    auto nameIndex = m_idsByName.find(record.name);
    if (nameIndex != m_idsByName.end() && nameIndex->second == objectID) {
        m_idsByName.erase(nameIndex);
    }

    auto userRange = m_idsByUserId.equal_range(record.userID);
    for (auto userIndex = userRange.first; userIndex != userRange.second; ++userIndex) {
        if (userIndex->second == objectID) {
            m_idsByUserId.erase(userIndex);
            break;
        }
    }
}

bool coreObjectList::insert(CoreObject* obj, bool replace)
{
    if (obj == nullptr) {
        return false;
    }

    const auto objectID = obj->getID();
    const auto objectName = obj->getName();
    const auto existingById = m_objectsById.find(objectID);
    const auto existingName = m_idsByName.find(objectName);

    if (!replace) {
        if (existingById != m_objectsById.end()) {
            return false;
        }
        if (existingName != m_idsByName.end() && existingName->second != objectID) {
            return false;
        }
    }

    if (existingById != m_objectsById.end()) {
        removeIndexes(existingById->first, existingById->second);
        m_objectsById.erase(existingById);
    }

    if (existingName != m_idsByName.end()) {
        auto existingObject = m_objectsById.find(existingName->second);
        if (existingObject != m_objectsById.end()) {
            removeIndexes(existingObject->first, existingObject->second);
            m_objectsById.erase(existingObject);
        }
    }

    objectRecord record{obj, objectName, obj->getUserID()};
    m_objectsById.emplace(objectID, record);
    addIndexes(objectID, record);
    return true;
}

CoreObject* coreObjectList::find(std::string_view objName) const
{
    auto foundObject = m_idsByName.find(std::string{objName});
    if (foundObject != m_idsByName.end()) {
        auto objectIndex = m_objectsById.find(foundObject->second);
        if (objectIndex != m_objectsById.end()) {
            return objectIndex->second.object;
        }
    }
    return nullptr;
}

std::vector<CoreObject*> coreObjectList::find(index_t searchID) const
{
    std::vector<CoreObject*> out;
    auto userRange = m_idsByUserId.equal_range(searchID);
    for (auto userIndex = userRange.first; userIndex != userRange.second; ++userIndex) {
        auto objectIndex = m_objectsById.find(userIndex->second);
        if (objectIndex != m_objectsById.end()) {
            out.push_back(objectIndex->second.object);
        }
    }
    return out;
}

bool coreObjectList::remove(CoreObject* obj)
{
    if (obj == nullptr) {
        return false;
    }

    auto foundObject = m_objectsById.find(obj->getID());
    if (foundObject == m_objectsById.end()) {
        return false;
    }

    removeIndexes(foundObject->first, foundObject->second);
    m_objectsById.erase(foundObject);
    return true;
}

bool coreObjectList::remove(std::string_view objName)
{
    auto foundObject = m_idsByName.find(std::string{objName});
    if (foundObject == m_idsByName.end()) {
        return false;
    }

    auto objectIndex = m_objectsById.find(foundObject->second);
    if (objectIndex == m_objectsById.end()) {
        m_idsByName.erase(foundObject);
        return false;
    }

    removeIndexes(objectIndex->first, objectIndex->second);
    m_objectsById.erase(objectIndex);
    return true;
}

bool coreObjectList::isMember(const CoreObject* obj) const
{
    return (obj != nullptr) && (m_objectsById.find(obj->getID()) != m_objectsById.end());
}

void coreObjectList::deleteAll(CoreObject* parent)
{
    for (const auto& [objectID, record] : m_objectsById) {
        static_cast<void>(objectID);
        removeReference(record.object, parent);
    }
}

void coreObjectList::updateObject(CoreObject* obj)
{
    if (obj == nullptr) {
        return;
    }

    const auto objectID = obj->getID();
    auto foundObject = m_objectsById.find(objectID);
    if (foundObject == m_objectsById.end()) {
        return;
    }

    const auto oldName = foundObject->second.name;
    const auto oldUserId = foundObject->second.userID;
    const auto newName = obj->getName();
    const auto newUserId = obj->getUserID();

    if (oldName != newName) {
        auto conflictingName = m_idsByName.find(newName);
        if (conflictingName != m_idsByName.end() && conflictingName->second != objectID) {
            return;
        }
    }

    if (oldName != newName || oldUserId != newUserId) {
        removeIndexes(foundObject->first, foundObject->second);
        foundObject->second.name = newName;
        foundObject->second.userID = newUserId;
        foundObject->second.object = obj;
        addIndexes(foundObject->first, foundObject->second);
    } else {
        foundObject->second.object = obj;
    }
}

}  // namespace griddyn

