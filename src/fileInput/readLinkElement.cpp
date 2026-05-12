/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "griddyn/Link.h"
#include "griddyn/gridBus.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
static const IgnoreListType linkIgnoreElements{"to", "from"};
static const char linkComponentName[] = "link";
// aP is the link element
Link* readLinkElement(std::shared_ptr<readerElement>& element,
                      readerInfo& ri,
                      coreObject* searchObject,
                      bool warnlink)
{
    auto riScope = ri.newScope();

    // run the boilerplate code to setup the object
    Link* linkObject = ElementReaderSetup(
        element, static_cast<Link*>(nullptr), linkComponentName, ri, searchObject);

    // from bus
    std::string busName = getElementField(element, "from", readerConfig::defMatchType);
    if (busName.empty()) {
        if (warnlink) {
            WARNPRINT(READER_WARN_IMPORTANT, "link must specify a 'from' bus");
        }
    } else if (searchObject != nullptr) {
        busName = ri.checkDefines(busName);
        auto locatedObject = locateObject(busName, searchObject);
        auto bus = dynamic_cast<gridBus*>(locatedObject);
        if (bus != nullptr) {
            try {
                linkObject->updateBus(bus, 1);
            }
            catch (const objectAddFailure& oaf) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unable to load 'from' bus " << busName << oaf.what());
            }
        } else if (warnlink) {
            WARNPRINT(READER_WARN_IMPORTANT, "link must specify a 'from' bus");
        }
    }

    // to bus
    busName = getElementField(element, "to", readerConfig::defMatchType);
    if (busName.empty()) {
        if (warnlink) {
            WARNPRINT(READER_WARN_IMPORTANT, "link must specify a 'to' bus");
        }
    } else if (searchObject != nullptr) {
        busName = ri.checkDefines(busName);
        auto locatedObject = locateObject(busName, searchObject);
        auto bus = dynamic_cast<gridBus*>(locatedObject);
        if (bus != nullptr) {
            try {
                linkObject->updateBus(bus, 2);
            }
            catch (const objectAddFailure& oaf) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unable to load 'to' bus " << busName << " error: " << oaf.what());
            }
        } else if (warnlink) {
            WARNPRINT(READER_WARN_IMPORTANT, "link must specify a 'to' bus");
        }
    }

    // properties from link attributes

    loadElementInformation(linkObject, element, linkComponentName, ri, linkIgnoreElements);

    LEVELPRINT(READER_NORMAL_PRINT, "loaded link " << linkObject->getName());

    ri.closeScope(riScope);
    return linkObject;
}

}  // namespace griddyn
