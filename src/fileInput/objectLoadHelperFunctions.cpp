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

std::string getObjectName(std::shared_ptr<readerElement>& element,
                          readerInfo& readerInformation)
{
    std::string newName = getElementField(element, nameString, readerConfig::defMatchType);
    if (!newName.empty()) {
        newName = readerInformation.checkDefines(newName);
        if (!readerInformation.prefix.empty()) {
            newName = readerInformation.prefix + '_' + newName;
        }
    }
    return newName;
}

void setIndex(std::shared_ptr<readerElement>& element,
              coreObject* mainObject,
              readerInfo& readerInformation)
{
    std::string indexValue =
        getElementFieldOptions(element, indexAndNumber, readerConfig::defMatchType);
    if (!indexValue.empty()) {
        indexValue = readerInformation.checkDefines(indexValue);
        const double interpretedIndex = interpretString(indexValue, readerInformation);
        mainObject->locIndex = static_cast<int>(interpretedIndex);
    }
    // check if there is a purpose string which is used in some models
    std::string purpose = getElementField(element, "purpose", readerConfig::defMatchType);
    if (!purpose.empty()) {
        purpose = readerInformation.checkDefines(purpose);
        try {
            mainObject->set("purpose", purpose);
        }
        catch (unrecognizedParameter&) {
            mainObject->set("description", purpose);
        }
    }
}

}  // namespace griddyn
