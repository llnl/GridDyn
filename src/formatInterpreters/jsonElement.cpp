/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "jsonElement.h"

#include <string>
#include <utility>

static const char nullStr[] = "";

static bool isJsonNodeEmpty(const jsonElement::JsonValue& value)
{
    return value.is_null() || ((value.is_object() || value.is_array()) && value.empty());
}

jsonElement::jsonElement(JsonValue vElement, std::string newName):
    name(std::move(newName)), element(std::move(vElement))
{
    elementIndex = 0;

    if (element.is_array()) {
        arraytype = true;
        arrayIndex = 0;
        while ((arrayIndex < element.size()) && (isJsonNodeEmpty(element[arrayIndex]))) {
            ++arrayIndex;
        }
    }
}

void jsonElement::clear()
{
    element = nullptr;
    elementIndex = 0;
    arrayIndex = 0;
    arraytype = false;
    name = nullStr;
}
