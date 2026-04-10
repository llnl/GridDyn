/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/objectInterpreter.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
coreObject* getParent(std::shared_ptr<readerElement>& element,
                      readerInfo& ri,
                      coreObject* parentObject,
                      const std::string& alternateName)
{
    std::string parentName = getElementField(element, "parent", readerConfig::defMatchType);
    if (!parentName.empty()) {
        parentName = ri.checkDefines(parentName);
        return locateObject(parentName, parentObject);
    }
    if (!alternateName.empty()) {
        parentName = getElementAttribute(element, alternateName, readerConfig::defMatchType);
        if (!parentName.empty()) {
            parentName = ri.checkDefines(parentName);
            return locateObject(parentName, parentObject);
        }
    }
    return nullptr;
}

}  // namespace griddyn
