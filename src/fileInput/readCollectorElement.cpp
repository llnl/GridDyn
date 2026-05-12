/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "core/factoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "griddyn/measurement/collector.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
static const char nameString[] = "name";

namespace {
    const IgnoreListType& collectorIgnoreStrings()
    {
        static const auto* ignoreStrings = new IgnoreListType{
            "file", "name", "column", "offset", "units", "gain", "bias", "field", "target", "type"};
        return *ignoreStrings;
    }
}

static const char collectorNameString[] = "collector";

int loadCollectorElement(std::shared_ptr<readerElement>& element,
                         coreObject* obj,
                         readerInfo& readerInformation)
{
    const int ret = FUNCTION_EXECUTION_SUCCESS;
    std::string name =
        readerInformation.checkDefines(getElementField(element, nameString, readerConfig::defMatchType));
    const std::string fileName = readerInformation.checkDefines(
        getElementFieldOptions(element, {"file", "sink"}, readerConfig::defMatchType));
    std::string type =
        readerInformation.checkDefines(getElementField(element, "type", readerConfig::defMatchType));

    auto collectorObject = readerInformation.findCollector(name, fileName);
    if ((!type.empty()) && (name.empty()) && (fileName.empty())) {
        collectorObject = nullptr;
    }
    if (type.empty()) {
        if (element->getName() != collectorNameString) {
            type = element->getName();
        }
    }

    if (!collectorObject) {
        collectorObject = makeCollector(type, name);

        if (!fileName.empty()) {
            collectorObject->set("file", fileName);
        }
        readerInformation.collectors.push_back(collectorObject);
    }

    gridGrabberInfo grabberInfo;
    name = getElementField(element, "target", readerConfig::defMatchType);
    if (!name.empty()) {
        name = readerInformation.checkDefines(name);
        grabberInfo.m_target = name;
    }
    auto fieldList = getElementFieldMultiple(element, "field", readerConfig::defMatchType);

    if (!fieldList.empty()) {
        grabberInfo.field = "";
        for (auto& fieldString : fieldList) {
            fieldString = readerInformation.checkDefines(fieldString);
            if (grabberInfo.field.empty()) {
                grabberInfo.field = fieldString;
            } else {
                grabberInfo.field += "; " + fieldString;
            }
        }
    }

    std::string elementText = getElementField(element, "bias", readerConfig::defMatchType);
    if (!elementText.empty()) {
        grabberInfo.bias = interpretString(elementText, readerInformation);
    }
    elementText = getElementField(element, "gain", readerConfig::defMatchType);
    if (!elementText.empty()) {
        grabberInfo.gain = interpretString(elementText, readerInformation);
    }
    elementText = getElementFieldOptions(element, {"units", "unit"}, readerConfig::defMatchType);
    if (!elementText.empty()) {
        elementText = readerInformation.checkDefines(elementText);
        grabberInfo.outputUnits = units::unit_cast_from_string(elementText);
    }
    elementText = getElementField(element, "column", readerConfig::defMatchType);
    if (!elementText.empty()) {
        grabberInfo.column = static_cast<int>(interpretString(elementText, readerInformation));
    }
    elementText = getElementField(element, "offset", readerConfig::defMatchType);
    if (!elementText.empty()) {
        grabberInfo.offset = static_cast<int>(interpretString(elementText, readerInformation));
        if (!grabberInfo.field.empty()) {
            WARNPRINT(READER_WARN_ALL,
                      "specifying offset in collector overrides field specification");
        }
    }
    // now load the other fields for the collector

    setAttributes(collectorObject.get(),
                  element,
                  collectorNameString,
                  readerInformation,
                  collectorIgnoreStrings());
    setParams(collectorObject.get(),
              element,
              collectorNameString,
              readerInformation,
              collectorIgnoreStrings());
    coreObject* targetObj = obj;

    if (!grabberInfo.m_target.empty() && (grabberInfo.m_target != obj->getName())) {
        targetObj = locateObject(grabberInfo.m_target, obj);
    }
    if (targetObj != nullptr) {
        try {
            collectorObject->add(grabberInfo, targetObj);
            if (collectorObject->getName().empty()) {
                collectorObject->setName(targetObj->getName() + "_" + type);
            }
        }
        catch (const addFailureException&) {
            WARNPRINT(READER_WARN_IMPORTANT,
                      type << " for " << obj->getName()
                           << "cannot find field " << grabberInfo.field);
        }
    } else {
        WARNPRINT(READER_WARN_IMPORTANT,
                  type << " for " << grabberInfo.m_target << "cannot find field "
                       << grabberInfo.field);
    }

    if (collectorObject->getWarningCount() > 0) {
        for (const auto& warning : collectorObject->getWarnings()) {
            WARNPRINT(READER_WARN_IMPORTANT,
                      "recorder " << collectorObject->getName() << " " << warning);
        }
    }
    return ret;
}

}  // namespace griddyn
