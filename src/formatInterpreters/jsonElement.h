#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "json/json.h"
#include <string>

class jsonElement {
  public:
    int elementIndex = 0;
    std::string name;
    Json::ArrayIndex arrayIndex = 0;
    jsonElement() noexcept {}
    jsonElement(Json::Value vElement, std::string newName);

    void clear();
    const Json::Value& getElement() const { return (arraytype) ? element[arrayIndex] : element; }
    Json::ArrayIndex count() const { return (arraytype) ? element.size() : Json::ArrayIndex(1); }
    bool isNull() const { return (arraytype) ? element[arrayIndex].isNull() : element.isNull(); }

  private:
    Json::Value element = Json::nullValue;
    bool arraytype = false;
};
