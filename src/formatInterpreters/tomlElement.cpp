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
    name(std::move(newName)), element(std::move(vElement))
{
    elementIndex = 0;

    if (element.is_array()) {
        arraytype = true;
        arrayIndex = 0;
        const auto& arr = element.as_array();
        while ((arrayIndex < arr.size()) && (isTomlNodeEmpty(arr[arrayIndex]))) {
            ++arrayIndex;
        }
    }
}

const toml::ordered_value& tomlElement::getElement() const
{
    if (arraytype) {
        const auto& arr = element.as_array(std::nothrow);
        if (arrayIndex < arr.size()) {
            return arr[arrayIndex];
        }
    }
    return element;
}

std::size_t tomlElement::count() const
{
    return (arraytype) ? element.as_array(std::nothrow).size() : std::size_t{1};
}

bool tomlElement::isNull() const
{
    if (arraytype) {
        const auto& arr = element.as_array(std::nothrow);
        return (arrayIndex >= arr.size()) || isTomlNodeEmpty(arr[arrayIndex]);
    }
    return isTomlNodeEmpty(element);
}

void tomlElement::clear()
{
    element = toml::ordered_value{};
    elementIndex = 0;
    arrayIndex = 0;
    arraytype = false;
    name = nullStr;
}
