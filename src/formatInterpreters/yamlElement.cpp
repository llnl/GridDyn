/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "yamlElement.h"

#include <string>

static const char nullStr[] = "";

yamlElement::yamlElement(const YAML::Node& vElement, std::string newName):
    name(newName), element(vElement)
{
    elementIndex = 0;

    if (element.IsSequence()) {
        arraytype = true;
        arrayIndex = 0;
        while ((arrayIndex < element.size()) && (element[arrayIndex].IsNull())) {
            ++arrayIndex;
        }
    }
}

void yamlElement::clear()
{
    element.reset();
    elementIndex = 0;
    arrayIndex = 0;
    arraytype = false;
    name = nullStr;
}

const YAML::Node yamlElement::getElement() const
{
    return (arraytype) ? (element[arrayIndex]) : element;
}
