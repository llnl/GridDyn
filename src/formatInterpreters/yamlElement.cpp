/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "yamlElement.h"

#include <string>

static const char nullStr[] = "";

yamlElement::yamlElement(const YAML::Node& vElement, std::string newName):
    mName(newName), mElement(vElement)
{
    mElementIndex = 0;

    if (mElement.IsSequence()) {
        mArrayType = true;
        mArrayIndex = 0;
        while ((mArrayIndex < mElement.size()) && (mElement[mArrayIndex].IsNull())) {
            ++mArrayIndex;
        }
    }
}

void yamlElement::clear()
{
    mElement.reset();
    mElementIndex = 0;
    mArrayIndex = 0;
    mArrayType = false;
    mName = nullStr;
}

const YAML::Node yamlElement::getElement() const
{
    return (mArrayType) ? (mElement[mArrayIndex]) : mElement;
}
