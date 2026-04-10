/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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

static const IgnoreListType collectorIgnoreStrings{"file",
                                                   "name",
                                                   "column",
                                                   "offset",
                                                   "units",
                                                   "gain",
                                                   "bias",
                                                   "field",
                                                   "target",
                                                   "type"};

static const char collectorNameString[] = "collector";

int loadCollectorElement(std::shared_ptr<readerElement>& element, coreObject* obj, readerInfo& ri)
{
    int ret = FUNCTION_EXECUTION_SUCCESS;
    std::string name =
        ri.checkDefines(getElementField(element, nameString, readerConfig::defMatchType));
    std::string fileName = ri.checkDefines(
        getElementFieldOptions(element, {"file", "sink"}, readerConfig::defMatchType));
    std::string type =
        ri.checkDefines(getElementField(element, "type", readerConfig::defMatchType));

    auto col = ri.findCollector(name, fileName);
    if ((!type.empty()) && (name.empty()) && (fileName.empty())) {
        col = nullptr;
    }
    if (type.empty()) {
        if (element->getName() != collectorNameString) {
            type = element->getName();
        }
    }

    if (!(col)) {
        col = makeCollector(type, name);

        if (!fileName.empty()) {
            col->set("file", fileName);
        }
        ri.collectors.push_back(col);
    }

    gridGrabberInfo gdRI;
    name = getElementField(element, "target", readerConfig::defMatchType);
    if (!name.empty()) {
        name = ri.checkDefines(name);
        gdRI.m_target = name;
    }
    auto fieldList = getElementFieldMultiple(element, "field", readerConfig::defMatchType);

    if (!(fieldList.empty())) {
        gdRI.field = "";
        for (auto& fstr : fieldList) {
            fstr = ri.checkDefines(fstr);
            if (gdRI.field.empty()) {
                gdRI.field = fstr;
            } else {
                gdRI.field += "; " + fstr;
            }
        }
    }

    std::string elementText = getElementField(element, "bias", readerConfig::defMatchType);
    if (!elementText.empty()) {
        gdRI.bias = interpretString(elementText, ri);
    }
    elementText = getElementField(element, "gain", readerConfig::defMatchType);
    if (!elementText.empty()) {
        gdRI.gain = interpretString(elementText, ri);
    }
    elementText = getElementFieldOptions(element, {"units", "unit"}, readerConfig::defMatchType);
    if (!elementText.empty()) {
        elementText = ri.checkDefines(elementText);
        gdRI.outputUnits = units::unit_cast_from_string(elementText);
    }
    elementText = getElementField(element, "column", readerConfig::defMatchType);
    if (!elementText.empty()) {
        gdRI.column = static_cast<int>(interpretString(elementText, ri));
    }
    elementText = getElementField(element, "offset", readerConfig::defMatchType);
    if (!elementText.empty()) {
        gdRI.offset = static_cast<int>(interpretString(elementText, ri));
        if (!(gdRI.field.empty())) {
            WARNPRINT(READER_WARN_ALL,
                      "specifying offset in collector overrides field specification");
        }
    }
    // now load the other fields for the collector

    setAttributes(col.get(), element, collectorNameString, ri, collectorIgnoreStrings);
    setParams(col.get(), element, collectorNameString, ri, collectorIgnoreStrings);
    coreObject* targetObj = obj;

    if (!((gdRI.m_target.empty()) || (gdRI.m_target == obj->getName()))) {
        targetObj = locateObject(gdRI.m_target, obj);
    }
    if (targetObj != nullptr) {
        try {
            col->add(gdRI, targetObj);
            if (col->getName().empty()) {
                col->setName(targetObj->getName() + "_" + type);
            }
        }
        catch (const addFailureException&) {
            WARNPRINT(READER_WARN_IMPORTANT,
                      type << " for " << obj->getName() << "cannot find field " << gdRI.field);
        }
    } else {
        WARNPRINT(READER_WARN_IMPORTANT,
                  type << " for " << gdRI.m_target << "cannot find field " << gdRI.field);
    }

    if (col->getWarningCount() > 0) {
        for (const auto& warning : col->getWarnings()) {
            WARNPRINT(READER_WARN_IMPORTANT, "recorder " << col->getName() << " " << warning);
        }
    }
    return ret;
}

}  // namespace griddyn
