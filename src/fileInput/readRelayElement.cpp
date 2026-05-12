/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "griddyn/Area.h"
#include "griddyn/Generator.h"
#include "griddyn/Relay.h"
#include "griddyn/gridBus.h"
#include "griddyn/loads/zipLoad.h"
#include "readElement.h"
#include <string>

namespace griddyn {
using readerConfig::defMatchType;
static const IgnoreListType relayIgnoreElements{"area", "sink", "source", "target"};
static const char relayComponentName[] = "relay";

// "aP" is the XML element passed from the reader
Relay* readRelayElement(std::shared_ptr<readerElement>& element,
                        readerInfo& ri,
                        coreObject* searchObject)
{
    auto riScope = ri.newScope();

    // check the rest of the elements

    // boiler plate code to setup the object from references or new object
    // check for the area field
    coreObject* defaultTargetObject = searchObject;
    Relay* relay = nullptr;
    searchObject = updateSearchObject<gridPrimary>(element, ri, searchObject);
    if (dynamic_cast<Area*>(searchObject) == nullptr) {
        if (searchObject != nullptr) {
            searchObject = searchObject->getRoot();
        }
    }

    std::string relayType = getElementField(element, "type", defMatchType);
    if (relayType.empty()) {  // if the relay is a subobject of specific type of object then adjust the
                         // relay to match
        std::string elementType = gmlc::utilities::convertToLowerCase(element->getName());
        if (elementType == relayComponentName) {
            relayType = getElementField(element, "ref", defMatchType);
            if (relayType.empty()) {
                // no type information so generate default relay of a specific type
                if (dynamic_cast<gridBus*>(defaultTargetObject) != nullptr) {
                    relay = static_cast<Relay*>(
                        coreObjectFactory::instance()->createObject(relayComponentName, "bus"));
                } else if (dynamic_cast<zipLoad*>(defaultTargetObject) != nullptr) {
                    relay = static_cast<Relay*>(
                        coreObjectFactory::instance()->createObject(relayComponentName, "load"));
                } else if (dynamic_cast<Generator*>(defaultTargetObject) != nullptr) {
                    relay = static_cast<Relay*>(
                        coreObjectFactory::instance()->createObject(relayComponentName, "gen"));
                }
            }
        }
    }
    relay = ElementReaderSetup(element, relay, relayComponentName, ri, searchObject);

    coreObject* targetObj = nullptr;
    std::string objectName = getElementField(element, "target", defMatchType);
    if (!objectName.empty()) {
        objectName = ri.checkDefines(objectName);
        targetObj = locateObject(objectName, searchObject);
        if (targetObj == nullptr) {
            WARNPRINT(READER_WARN_IMPORTANT, "Unable to locate target object " << objectName);
        }
    }

    if (targetObj != nullptr) {
        relay->setSource(targetObj);
        relay->setSink(targetObj);
    } else {
        objectName = getElementField(element, "source", defMatchType);
        if (objectName.empty()) {
            targetObj = defaultTargetObject;
        } else if (searchObject != nullptr) {
            objectName = ri.checkDefines(objectName);
            targetObj = locateObject(objectName, searchObject);
        }
        if (targetObj != nullptr) {
            relay->setSource(targetObj);
        }

        objectName = getElementField(element, "sink", defMatchType);
        if (objectName.empty()) {
            targetObj = defaultTargetObject;
        } else if (searchObject != nullptr) {
            objectName = ri.checkDefines(objectName);
            targetObj = locateObject(objectName, searchObject);
        }
        if (targetObj != nullptr) {
            relay->setSink(targetObj);
        }
    }

    loadElementInformation(relay, element, relayComponentName, ri, relayIgnoreElements);

    LEVELPRINT(READER_NORMAL_PRINT, "loaded relay " << relay->getName());

    ri.closeScope(riScope);
    return relay;
}

}  // namespace griddyn
