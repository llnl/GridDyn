/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/objectFactory.hpp"
#include "core/objectInterpreter.h"
#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridBus.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
namespace {
    const IgnoreListType& busIgnore()
    {
        static const auto* ignoreFields = new IgnoreListType{"area"};
        return *ignoreFields;
    }
}  // namespace
static const char busComponentName[] = "bus";
// "aP" is the XML element passed from the reader
gridBus* readBusElement(std::shared_ptr<readerElement>& element,
                        readerInfo& readerInformation,
                        CoreObject* searchObject)
{
    auto riScope = readerInformation.newScope();

    // boiler plate code to setup the object from references or new object
    // check for the area field

    gridBus* bus = ElementReaderSetup(
        element, static_cast<gridBus*>(nullptr), busComponentName, readerInformation, searchObject);

    std::string valType = getElementField(element, "type", readerConfig::defMatchType);
    if (!valType.empty()) {
        valType = readerInformation.checkDefines(valType);
        auto delimiterPos = valType.find_first_of(",;");
        if (delimiterPos != std::string::npos) {
            std::string primaryType = valType.substr(0, delimiterPos);
            std::string secondaryType = valType.substr(delimiterPos + 1);
            gmlc::utilities::stringOps::trimString(primaryType);
            gmlc::utilities::stringOps::trimString(secondaryType);
            try {
                bus->set("type", primaryType);
            }
            catch (const std::invalid_argument&) {
                WARNPRINT(READER_WARN_IMPORTANT, "Bus type parameter not found " << primaryType);
            }
            try {
                bus->set("type", secondaryType);
            }
            catch (const std::invalid_argument&) {
                WARNPRINT(READER_WARN_IMPORTANT, "Bus type parameter not found " << secondaryType);
            }
        } else {
            try  // type can mean two different things to a bus -either the actual type of the bus
                 // object or the
            // state type of the bus this catch will
            // will dismabiguate them since in a majority of cases we are not changing the type of
            // the object only how it interprets the state
            {
                bus->set("type", valType);
            }
            catch (const std::invalid_argument&)  // either invalidParameterValue or
                                                  // unrecognizedParameter depending on the actual
                                                  // model used
            {
                if (!coreObjectFactory::instance()->isValidType(busComponentName, valType)) {
                    WARNPRINT(READER_WARN_IMPORTANT, "Bus type parameter not found " << valType);
                }
            }
        }
    }
    loadElementInformation(bus, element, busComponentName, readerInformation, busIgnore());

    LEVELPRINT(READER_NORMAL_PRINT, "loaded Bus " << bus->getName());

    readerInformation.closeScope(riScope);
    return bus;
}

}  // namespace griddyn

