/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tomlElement.h"

#include <string>
#include <utility>

static constexpr char nullStr[] = "";

namespace {
bool isTomlNodeEmpty(const toml::node& value)
{
    if (const auto* table = value.as_table()) {
        return table->empty();
    }
    if (const auto* array = value.as_array()) {
        return array->empty();
    }
    return false;
}
}  // namespace

TomlElement::TomlElement(const toml::node& vElement, std::string newName):
    mName(std::move(newName)), mElement(&vElement)
{
    mElementIndex = 0;

    if (const auto* arr = mElement->as_array()) {
        mArrayType = true;
        mArrayIndex = 0;
        while ((mArrayIndex < arr->size()) && (isTomlNodeEmpty((*arr)[mArrayIndex]))) {
            ++mArrayIndex;
        }
    }
}

const toml::node& TomlElement::getElement() const
{
    if (mArrayType) {
        const auto& arr = *mElement->as_array();
        if (mArrayIndex < arr.size()) {
            return arr[mArrayIndex];
        }
    }
    // An empty table represents an unavailable cursor value.
    static const toml::table emptyTable;
    return (mElement != nullptr) ? *mElement : emptyTable;
}

std::size_t TomlElement::count() const
{
    return (mArrayType) ? mElement->as_array()->size() : std::size_t{1};
}

bool TomlElement::isNull() const
{
    if (mArrayType) {
        const auto& arr = *mElement->as_array();
        return (mArrayIndex >= arr.size()) || isTomlNodeEmpty(arr[mArrayIndex]);
    }
    return (mElement == nullptr) || isTomlNodeEmpty(*mElement);
}

void TomlElement::clear()
{
    mElement = nullptr;
    mElementIndex = 0;
    mArrayIndex = 0;
    mArrayType = false;
    mName = nullStr;
}
