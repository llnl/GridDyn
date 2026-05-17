/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "readElement.h"
#include "readerHelper.h"
#include <numeric>
#include <string>
#include <vector>

namespace griddyn {
namespace {
    const IgnoreListType& ignoreArrayVariables()
    {
        static const auto* ignoreVariables =
            new IgnoreListType{"count", "loopvariable", "interval", "start", "stop"};
        return *ignoreVariables;
    }

    int readElementInteger(std::shared_ptr<readerElement>& element,
                           const std::string& name,
                           readerInfo& readerInformation,
                           int defValue);
}  // namespace
// "aP" is the XML element passed from the reader
void readArrayElement(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation,
                      CoreObject* parentObject)
{
    auto riScope = readerInformation.newScope();
    std::vector<int> indices;

    loadDefines(element, readerInformation);
    loadDirectories(element, readerInformation);
    // loop through the other children
    //  cd = aP->FirstChildElement (false);
    std::string loopVariable = getElementField(element, "loopvariable", readerConfig::defMatchType);
    if (loopVariable.empty()) {
        loopVariable = "#index";
    }

    readerInformation.setKeyObject(parentObject);
    const int count = readElementInteger(element, "count", readerInformation, -1);
    const int start = readElementInteger(element, "start", readerInformation, 1);
    const int stop = readElementInteger(element, "stop", readerInformation, -1);
    const int interval = readElementInteger(element, "interval", readerInformation, 1);
    readerInformation.setKeyObject(nullptr);
    if (count > 0) {
        indices.resize(count);
        if (interval == 1) {
            std::iota(indices.begin(), indices.end(), start);
        } else {
            int val = start;
            for (auto& indexValue : indices) {
                indexValue = val;
                val += interval;
            }
        }
    } else {
        if (stop > start) {
            int val = start;
            while (val <= stop) {
                indices.push_back(val);
                val += interval;
            }
        } else {
            WARNPRINT(READER_WARN_IMPORTANT, "unable to create array");
            return;
        }
    }

    // fill the vector

    for (auto ind : indices) {
        readerInformation.addDefinition(loopVariable, std::to_string(ind));
        loadElementInformation(
            parentObject, element, "array", readerInformation, ignoreArrayVariables());
    }

    readerInformation.closeScope(riScope);
}

namespace {
    int readElementInteger(std::shared_ptr<readerElement>& element,
                           const std::string& name,
                           readerInfo& readerInformation,
                           int defValue)
    {
        int returnValue = defValue;
        auto strVal = getElementField(element, name, readerConfig::defMatchType);
        if (strVal.empty()) {
            return returnValue;
        }

        returnValue = gmlc::utilities::numeric_conversionComplete<int>(strVal, -kBigINT);
        if (returnValue == -kBigINT)  // we have a more complicated string
        {
            const double val = interpretString(strVal, readerInformation);
            if ((val > 0) && (static_cast<int>(val) < kBigINT)) {
                returnValue = static_cast<int>(val);
            } else {
                returnValue = defValue;
                WARNPRINT(READER_WARN_IMPORTANT, "Unable to interpret start variable");
            }
        }

        return returnValue;
    }
}  // namespace

}  // namespace griddyn

