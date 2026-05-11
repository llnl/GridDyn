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
    mName(std::move(newName)), mElement(std::move(vElement))
{
    mElementIndex = 0;

    if (mElement.is_array()) {
        mArrayType = true;
        mArrayIndex = 0;
        while ((mArrayIndex < mElement.size()) && (isJsonNodeEmpty(mElement[mArrayIndex]))) {
            ++mArrayIndex;
        }
    }
}

void jsonElement::clear()
{
    mElement = nullptr;
    mElementIndex = 0;
    mArrayIndex = 0;
    mArrayType = false;
    mName = nullStr;
}
