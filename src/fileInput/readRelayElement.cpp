/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
    coreObject* keyObject = searchObject;
    Relay* relay = nullptr;
    searchObject = updateSearchObject<gridPrimary>(element, ri, searchObject);
    if (dynamic_cast<Area*>(searchObject) == nullptr) {
        if (searchObject != nullptr) {
            searchObject = searchObject->getRoot();
        }
    }

    std::string name = getElementField(element, "type", defMatchType);
    if (name.empty()) {  // if the relay is a subobject of specific type of object then adjust the
                         // relay to match
        std::string valType = gmlc::utilities::convertToLowerCase(element->getName());
        if (valType == relayComponentName) {
            name = getElementField(element, "ref", defMatchType);
            if (name.empty()) {
                // no type information so generate default relay of a specific type
                if (dynamic_cast<gridBus*>(keyObject) != nullptr) {
                    relay = static_cast<Relay*>(
                        coreObjectFactory::instance()->createObject(relayComponentName, "bus"));
                } else if (dynamic_cast<zipLoad*>(keyObject) != nullptr) {
                    relay = static_cast<Relay*>(
                        coreObjectFactory::instance()->createObject(relayComponentName, "load"));
                } else if (dynamic_cast<Generator*>(keyObject) != nullptr) {
                    relay = static_cast<Relay*>(
                        coreObjectFactory::instance()->createObject(relayComponentName, "gen"));
                }
            }
        }
    }
    relay = ElementReaderSetup(element, relay, relayComponentName, ri, searchObject);

    coreObject* targetObj = nullptr;
    std::string objName = getElementField(element, "target", defMatchType);
    if (!objName.empty()) {
        objName = ri.checkDefines(objName);
        targetObj = locateObject(objName, searchObject);
        if (targetObj == nullptr) {
            WARNPRINT(READER_WARN_IMPORTANT, "Unable to locate target object " << objName);
        }
    }

    if (targetObj != nullptr) {
        relay->setSource(targetObj);
        relay->setSink(targetObj);
    } else {
        objName = getElementField(element, "source", defMatchType);
        if (objName.empty()) {
            targetObj = keyObject;
        } else if (searchObject != nullptr) {
            objName = ri.checkDefines(objName);
            targetObj = locateObject(objName, searchObject);
        }
        if (targetObj != nullptr) {
            relay->setSource(targetObj);
        }

        objName = getElementField(element, "sink", defMatchType);
        if (objName.empty()) {
            targetObj = keyObject;
        } else if (searchObject != nullptr) {
            objName = ri.checkDefines(objName);
            targetObj = locateObject(objName, searchObject);
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
