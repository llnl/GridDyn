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

    std::size_t mElementIndex = 0;
    std::string mName;
    std::size_t mArrayIndex = 0;
    jsonElement() noexcept {}
    jsonElement(JsonValue vElement, std::string newName);

    void clear();
    const JsonValue& getElement() const
    {
        return (mArrayType) ? mElement[mArrayIndex] : mElement;
    }
    std::size_t count() const { return (mArrayType) ? mElement.size() : std::size_t{1}; }
    bool isNull() const
    {
        return (mArrayType) ? mElement[mArrayIndex].is_null() : mElement.is_null();
    }

  private:
    JsonValue mElement = nullptr;
    bool mArrayType = false;
};
