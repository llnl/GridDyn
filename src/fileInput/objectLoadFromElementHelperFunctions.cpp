/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/ObjectInterpreter.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
CoreObject* getParent(std::shared_ptr<readerElement>& element,
                      ReaderInfo& ReaderInformation,
                      CoreObject* parentObject,
                      const std::string& alternateName)
{
    std::string parentName = getElementField(element, "parent", readerConfig::defMatchType);
    if (!parentName.empty()) {
        parentName = ReaderInformation.checkDefines(parentName);
        return locateObject(parentName, parentObject);
    }
    if (!alternateName.empty()) {
        parentName = getElementAttribute(element, alternateName, readerConfig::defMatchType);
        if (!parentName.empty()) {
            parentName = ReaderInformation.checkDefines(parentName);
            return locateObject(parentName, parentObject);
        }
    }
    return nullptr;
}

}  // namespace griddyn
