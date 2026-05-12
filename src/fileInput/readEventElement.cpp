/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/events/Event.h"
#include "readElement.h"
#include "readerHelper.h"
#include "units/units.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
static const char eventNameString[] = "event";

static const IgnoreListType eventIgnoreStrings{"file",
                                               "name",
                                               "column",
                                               "value",
                                               "va",
                                               "units",
                                               "description",
                                               "period",
                                               "time",
                                               "t",
                                               "offset",
                                               "units",
                                               "gain",
                                               "bias",
                                               "field",
                                               "target",
                                               "type"};

void readEventElement(std::shared_ptr<readerElement>& element,
                      EventInfo& eventInfo,
                      readerInfo& ri,
                      coreObject* obj)
{
    if (element->getName() != "event") {
        if (element->getName() != "scenario") {
            eventInfo.type = element->getName();
        }
    }
    // get the event strings that may be present
    auto eventString = element->getMultiText(", ");
    if (!eventString.empty()) {
        eventInfo.loadString(eventString, obj);
    }

    // check for the field attributes
    std::string name = getElementField(element, "name", readerConfig::defMatchType);
    if (!name.empty()) {
        name = ri.checkDefines(name);
        eventInfo.name = name;
    }

    // check for the field attributes
    std::string type = getElementField(element, "type", readerConfig::defMatchType);
    if (!type.empty()) {
        eventInfo.type = ri.checkDefines(type);
    }

    // check for the field attributes
    auto targetList = getElementFieldMultiple(element, "target", readerConfig::defMatchType);
    for (auto& targetName : targetList) {
        targetName = ri.checkDefines(targetName);
        eventInfo.targetObjs.push_back(locateObject(targetName, obj));
    }

    auto fieldList = getElementFieldMultiple(element, "field", readerConfig::defMatchType);
    for (auto& fieldName : fieldList) {
        fieldName = ri.checkDefines(fieldName);
        eventInfo.fieldList.push_back(fieldName);
    }
    auto unitList = getElementFieldMultiple(element, "units", readerConfig::defMatchType);
    for (auto& unitName : unitList) {
        unitName = ri.checkDefines(unitName);
        eventInfo.units.push_back(units::unit_cast_from_string(unitName));
    }

    eventInfo.description = getElementField(element, "description", readerConfig::defMatchType);

    std::string field = getElementField(element, "period", readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.period = interpretString(field, ri);
    }

    field = getElementFieldOptions(element, {"t", "time"}, readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.time = gmlc::utilities::str2vector<coreTime>(ri.checkDefines(field), negTime);
    } else {
        if (eventInfo.time.empty() && element->getName() == "scenario") {
            eventInfo.time.push_back(-1.0);
        }
    }

    field = getElementFieldOptions(element, {"value", "val"}, readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.value = gmlc::utilities::str2vector(ri.checkDefines(field), kNullVal);
    }

    name = getElementField(element, "file", readerConfig::defMatchType);
    if (!name.empty()) {
        ri.checkFileParam(name);
        eventInfo.file = name;
    }

    field = getElementField(element, "column", readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.columns.push_back(static_cast<int>(interpretString(field, ri)));
    }
}

int loadEventElement(std::shared_ptr<readerElement>& element, coreObject* obj, readerInfo& ri)
{
    int ret = FUNCTION_EXECUTION_SUCCESS;
    element->bookmark();
    EventInfo eventInfo;
    readEventElement(element, eventInfo, ri, obj);
    auto eventObject = make_event(eventInfo, obj);
    if (!eventObject) {
        WARNPRINT(READER_WARN_IMPORTANT, "unable to create an event of type " << eventInfo.type);
        return FUNCTION_EXECUTION_FAILURE;
    }
    setAttributes(eventObject.get(), element, eventNameString, ri, eventIgnoreStrings);
    setParams(eventObject.get(), element, eventNameString, ri, eventIgnoreStrings);

    if (!(eventObject->isArmed())) {
        WARNPRINT(READER_WARN_IMPORTANT,
                  "event for " << eventInfo.name << ":unable to load event");
        ret = FUNCTION_EXECUTION_FAILURE;
    } else {
        auto owner = eventObject->getOwner();
        if (owner != nullptr) {  // check if the event has a specified owner
            auto ownedEvent = std::shared_ptr<Event>(std::move(eventObject));
            try {  // this could throw in which case we can't move the object into it first
                owner->addHelper(ownedEvent);
            }
            catch (const objectAddFailure&) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "Event: " << ownedEvent->getName() << " unable to be added to "
                                    << owner->getName());
                ri.events.push_back(std::move(ownedEvent));
            }
        } else {  // if it doesn't just put it on the stack and deal with it later
            ri.events.push_back(std::move(eventObject));
        }
    }
    element->restore();
    return ret;
}

}  // namespace griddyn
