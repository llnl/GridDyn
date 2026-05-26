/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
namespace {
    const IgnoreListType& linkIgnoreElements()
    {
        static const auto* ignoreElements = new IgnoreListType{"to", "from"};
        return *ignoreElements;
    }
}  // namespace
static constexpr char linkComponentName[] = "link";
// aP is the link element
Link* readLinkElement(std::shared_ptr<readerElement>& element,
                      ReaderInfo& ReaderInformation,
                      CoreObject* searchObject,
                      bool warnlink)
{
    auto riScope = ReaderInformation.newScope();

    // run the boilerplate code to setup the object
    Link* linkObject = elementReaderSetup(
        element, static_cast<Link*>(nullptr), linkComponentName, ReaderInformation, searchObject);

    // from bus
    std::string busName = getElementField(element, "from", readerConfig::defMatchType);
    if (busName.empty()) {
        if (warnlink) {
            WARNPRINT(READER_WARN_IMPORTANT, "link must specify a 'from' bus");
        }
    } else if (searchObject != nullptr) {
        busName = ReaderInformation.checkDefines(busName);
        auto* locatedObject = locateObject(busName, searchObject);
        auto* bus = dynamic_cast<GridBus*>(locatedObject);
        if (bus != nullptr) {
            try {
                linkObject->updateBus(bus, 1);
            }
            catch (const ObjectAddFailure& oaf) {
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
        busName = ReaderInformation.checkDefines(busName);
        auto* locatedObject = locateObject(busName, searchObject);
        auto* bus = dynamic_cast<GridBus*>(locatedObject);
        if (bus != nullptr) {
            try {
                linkObject->updateBus(bus, 2);
            }
            catch (const ObjectAddFailure& oaf) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unable to load 'to' bus " << busName << " error: " << oaf.what());
            }
        } else if (warnlink) {
            WARNPRINT(READER_WARN_IMPORTANT, "link must specify a 'to' bus");
        }
    }

    // properties from link attributes

    loadElementInformation(
        linkObject, element, linkComponentName, ReaderInformation, linkIgnoreElements());

    LEVELPRINT(READER_NORMAL_PRINT, "loaded link " << linkObject->getName());

    ReaderInformation.closeScope(riScope);
    return linkObject;
}

}  // namespace griddyn
