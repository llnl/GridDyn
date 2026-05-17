/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "coreObject.h"
#include "utilities/dataDictionary.h"
#include <string>

/** these objects are intended to capture extra properties about a CoreObject that are not in the
common definition such as position information, metadata, etc
*/
template<class PropertyType>
class coreObjectProperty {
  private:
    std::string name_;
    utilities::dataDictionary<id_type_t, PropertyType> dictionary;

  public:
    coreObjectProperty(const std::string& name): name_(name) {}
    void set(CoreObject* obj, PropertyType data) { dictionary.update(obj->getID(), data); }
    PropertyType query(CoreObject* obj) { return dictionary.query(obj->getID()); }
    void clearProperty(CoreObject* obj) { dictionary.erase(obj->getID()); }
};
/** @brief loads a position object
*@details I don't know what a grid Position object looks like yet
@param[in] npos a gridPositionObject
*/
// void loadPosition (std::shared_ptr<gridPositionInfo> npos);
