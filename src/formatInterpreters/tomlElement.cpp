/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tomlElement.h"

#include <string>
#include <utility>

static const char nullStr[] = "";

namespace {
bool isTomlNodeEmpty(const toml::ordered_value& value)
{
    return value.is_empty() || ((value.is_table() || value.is_array()) && value.size() == 0);
}
}  // namespace

tomlElement::tomlElement(toml::ordered_value vElement, std::string newName):
    mName(std::move(newName)), mElement(std::move(vElement))
{
    mElementIndex = 0;

    if (mElement.is_array()) {
        mArrayType = true;
        mArrayIndex = 0;
        const auto& arr = mElement.as_array();
        while ((mArrayIndex < arr.size()) && (isTomlNodeEmpty(arr[mArrayIndex]))) {
            ++mArrayIndex;
        }
    }
}

const toml::ordered_value& tomlElement::getElement() const
{
    if (mArrayType) {
        const auto& arr = mElement.as_array(std::nothrow);
        if (mArrayIndex < arr.size()) {
            return arr[mArrayIndex];
        }
    }
    return mElement;
}

std::size_t tomlElement::count() const
{
    return (mArrayType) ? mElement.as_array(std::nothrow).size() : std::size_t{1};
}

bool tomlElement::isNull() const
{
    if (mArrayType) {
        const auto& arr = mElement.as_array(std::nothrow);
        return (mArrayIndex >= arr.size()) || isTomlNodeEmpty(arr[mArrayIndex]);
    }
    return isTomlNodeEmpty(mElement);
}

void tomlElement::clear()
{
    mElement = toml::ordered_value{};
    mElementIndex = 0;
    mArrayIndex = 0;
    mArrayType = false;
    mName = nullStr;
}
