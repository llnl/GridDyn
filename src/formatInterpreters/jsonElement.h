/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "nlohmann/json.hpp"
#include <cstddef>
#include <string>

class jsonElement {
  public:
    using JsonValue = nlohmann::json;

    int elementIndex = 0;
    std::string name;
    std::size_t arrayIndex = 0;
    jsonElement() noexcept {}
    jsonElement(JsonValue vElement, std::string newName);

    void clear();
    const JsonValue& getElement() const { return (arraytype) ? element[arrayIndex] : element; }
    std::size_t count() const { return (arraytype) ? element.size() : std::size_t{1}; }
    bool isNull() const { return (arraytype) ? element[arrayIndex].is_null() : element.is_null(); }

  private:
    JsonValue element = nullptr;
    bool arraytype = false;
};
