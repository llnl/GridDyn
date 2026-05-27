/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridDynDefinitions.hpp"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
namespace {
    const stringVec& indexAndNumber()
    {
        static const auto* indexFields = new stringVec{"index", "number"};
        return *indexFields;
    }
}  // namespace

static constexpr char nameString[] = "name";

std::string getObjectName(std::shared_ptr<ReaderElement>& element, ReaderInfo& ReaderInformation)
{
    std::string newName = getElementField(element, nameString, readerConfig::defMatchType);
    if (!newName.empty()) {
        newName = ReaderInformation.checkDefines(newName);
        if (!ReaderInformation.prefix.empty()) {
            newName = ReaderInformation.prefix + '_' + newName;
        }
    }
    return newName;
}

void setIndex(std::shared_ptr<ReaderElement>& element,
              CoreObject* mainObject,
              ReaderInfo& ReaderInformation)
{
    std::string indexValue =
        getElementFieldOptions(element, indexAndNumber(), readerConfig::defMatchType);
    if (!indexValue.empty()) {
        indexValue = ReaderInformation.checkDefines(indexValue);
        const double interpretedIndex = interpretString(indexValue, ReaderInformation);
        mainObject->locIndex = static_cast<int>(interpretedIndex);
    }
    // check if there is a purpose string which is used in some models
    std::string purpose = getElementField(element, "purpose", readerConfig::defMatchType);
    if (!purpose.empty()) {
        purpose = ReaderInformation.checkDefines(purpose);
        try {
            mainObject->set("purpose", purpose);
        }
        catch (UnrecognizedParameter&) {
            mainObject->set("description", purpose);
        }
    }
}

}  // namespace griddyn


