/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "core/coreObject.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridDynDefinitions.hpp"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
static const stringVec indexAndNumber = {"index", "number"};
static const char nameString[] = "name";

std::string getObjectName(std::shared_ptr<readerElement>& element, readerInfo& ri)
{
    std::string newName = getElementField(element, nameString, readerConfig::defMatchType);
    if (!newName.empty()) {
        newName = ri.checkDefines(newName);
        if (!ri.prefix.empty()) {
            newName = ri.prefix + '_' + newName;
        }
    }
    return newName;
}

void setIndex(std::shared_ptr<readerElement>& element, coreObject* mobj, readerInfo& ri)
{
    std::string Index = getElementFieldOptions(element, indexAndNumber, readerConfig::defMatchType);
    if (!Index.empty()) {
        Index = ri.checkDefines(Index);
        double val = interpretString(Index, ri);
        mobj->locIndex = static_cast<int>(val);
    }
    // check if there is a purpose string which is used in some models
    std::string purp = getElementField(element, "purpose", readerConfig::defMatchType);
    if (!purp.empty()) {
        purp = ri.checkDefines(purp);
        try {
            mobj->set("purpose", purp);
        }
        catch (unrecognizedParameter&) {
            mobj->set("description", purp);
        }
    }
}

}  // namespace griddyn
