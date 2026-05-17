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

namespace {
    const IgnoreListType& eventIgnoreStrings()
    {
        static const auto* ignoreStrings = new IgnoreListType{"file",
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
        return *ignoreStrings;
    }
}  // namespace

static void readEventElement(std::shared_ptr<readerElement>& element,
                             EventInfo& eventInfo,
                             readerInfo& readerInformation,
                             CoreObject* obj)
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
        name = readerInformation.checkDefines(name);
        eventInfo.name = name;
    }

    // check for the field attributes
    const std::string type = getElementField(element, "type", readerConfig::defMatchType);
    if (!type.empty()) {
        eventInfo.type = readerInformation.checkDefines(type);
    }

    // check for the field attributes
    auto targetList = getElementFieldMultiple(element, "target", readerConfig::defMatchType);
    for (auto& targetName : targetList) {
        targetName = readerInformation.checkDefines(targetName);
        eventInfo.targetObjs.push_back(locateObject(targetName, obj));
    }

    auto fieldList = getElementFieldMultiple(element, "field", readerConfig::defMatchType);
    for (auto& fieldName : fieldList) {
        fieldName = readerInformation.checkDefines(fieldName);
        eventInfo.fieldList.push_back(fieldName);
    }
    auto unitList = getElementFieldMultiple(element, "units", readerConfig::defMatchType);
    for (auto& unitName : unitList) {
        unitName = readerInformation.checkDefines(unitName);
        eventInfo.units.push_back(units::unit_cast_from_string(unitName));
    }

    eventInfo.description = getElementField(element, "description", readerConfig::defMatchType);

    std::string field = getElementField(element, "period", readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.period = interpretString(field, readerInformation);
    }

    field = getElementFieldOptions(element, {"t", "time"}, readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.time =
            gmlc::utilities::str2vector<coreTime>(readerInformation.checkDefines(field), negTime);
    } else {
        if (eventInfo.time.empty() && element->getName() == "scenario") {
            eventInfo.time.emplace_back(-1.0);
        }
    }

    field = getElementFieldOptions(element, {"value", "val"}, readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.value =
            gmlc::utilities::str2vector(readerInformation.checkDefines(field), kNullVal);
    }

    name = getElementField(element, "file", readerConfig::defMatchType);
    if (!name.empty()) {
        readerInformation.checkFileParam(name);
        eventInfo.file = name;
    }

    field = getElementField(element, "column", readerConfig::defMatchType);
    if (!field.empty()) {
        eventInfo.columns.push_back(static_cast<int>(interpretString(field, readerInformation)));
    }
}

int loadEventElement(std::shared_ptr<readerElement>& element,
                     CoreObject* obj,
                     readerInfo& readerInformation)
{
    int returnValue = FUNCTION_EXECUTION_SUCCESS;
    element->bookmark();
    EventInfo eventInfo;
    readEventElement(element, eventInfo, readerInformation, obj);
    auto eventObject = make_event(eventInfo, obj);
    if (!eventObject) {
        WARNPRINT(READER_WARN_IMPORTANT, "unable to create an event of type " << eventInfo.type);
        return FUNCTION_EXECUTION_FAILURE;
    }
    setAttributes(
        eventObject.get(), element, eventNameString, readerInformation, eventIgnoreStrings());
    setParams(eventObject.get(), element, eventNameString, readerInformation, eventIgnoreStrings());

    if (!eventObject->isArmed()) {
        WARNPRINT(READER_WARN_IMPORTANT, "event for " << eventInfo.name << ":unable to load event");
        returnValue = FUNCTION_EXECUTION_FAILURE;
    } else {
        auto* owner = eventObject->getOwner();
        if (owner != nullptr) {  // check if the event has a specified owner
            auto ownedEvent = std::shared_ptr<Event>(std::move(eventObject));
            try {  // this could throw in which case we can't move the object into it first
                owner->addHelper(ownedEvent);
            }
            catch (const objectAddFailure&) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "Event: " << ownedEvent->getName() << " unable to be added to "
                                    << owner->getName());
                readerInformation.events.push_back(std::move(ownedEvent));
            }
        } else {  // if it doesn't just put it on the stack and deal with it later
            readerInformation.events.push_back(std::move(eventObject));
        }
    }
    element->restore();
    return returnValue;
}

}  // namespace griddyn
