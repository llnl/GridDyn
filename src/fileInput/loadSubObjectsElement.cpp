/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "readElement.h"
#include <array>
#include <cstdio>
#include <iterator>
#include <string>

// A bunch of includes to load these kinds of objects
#include "griddyn/Area.h"
#include "griddyn/Block.h"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/Link.h"
#include "griddyn/Relay.h"
#include "griddyn/Source.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/controllers/AGControl.h"
#include "griddyn/controllers/reserveDispatcher.h"
#include "griddyn/controllers/scheduler.h"
#include "griddyn/gridBus.h"
#include "griddyn/loads/zipLoad.h"

namespace griddyn {
namespace {
    using load_function_t = CoreObject* (*)(std::shared_ptr<readerElement>&,
                                            readerInfo&,
                                            CoreObject* parent);

    struct load_function_entry {
        const char* name;
        load_function_t loader;
    };

    CoreObject* loadGenModel(std::shared_ptr<readerElement>& currentElement,
                             readerInfo& readerInf,
                             CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<GenModel*>(nullptr), "genmodel", readerInf, parentObject);
    }

    CoreObject* loadExciter(std::shared_ptr<readerElement>& currentElement,
                            readerInfo& readerInf,
                            CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Exciter*>(nullptr), "exciter", readerInf, parentObject);
    }

    CoreObject* loadGovernor(std::shared_ptr<readerElement>& currentElement,
                             readerInfo& readerInf,
                             CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Governor*>(nullptr), "governor", readerInf, parentObject);
    }

    CoreObject* loadPss(std::shared_ptr<readerElement>& currentElement,
                        readerInfo& readerInf,
                        CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Stabilizer*>(nullptr), "pss", readerInf, parentObject);
    }

    CoreObject* loadSource(std::shared_ptr<readerElement>& currentElement,
                           readerInfo& readerInf,
                           CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Source*>(nullptr), "source", readerInf, parentObject);
    }

    CoreObject* loadScheduler(std::shared_ptr<readerElement>& currentElement,
                              readerInfo& readerInf,
                              CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<scheduler*>(nullptr), "scheduler", readerInf, parentObject);
    }

    CoreObject* loadAgc(std::shared_ptr<readerElement>& currentElement,
                        readerInfo& readerInf,
                        CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<AGControl*>(nullptr), "agc", readerInf, parentObject);
    }

    CoreObject* loadReserveDispatcher(std::shared_ptr<readerElement>& currentElement,
                                      readerInfo& readerInf,
                                      CoreObject* parentObject)
    {
        return ElementReader(currentElement,
                             static_cast<reserveDispatcher*>(nullptr),
                             "reservedispatcher",
                             readerInf,
                             parentObject);
    }

    CoreObject* loadBlock(std::shared_ptr<readerElement>& currentElement,
                          readerInfo& readerInf,
                          CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Block*>(nullptr), "block", readerInf, parentObject);
    }

    CoreObject* loadGenerator(std::shared_ptr<readerElement>& currentElement,
                              readerInfo& readerInf,
                              CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Generator*>(nullptr), "generator", readerInf, parentObject);
    }

    CoreObject* loadLoad(std::shared_ptr<readerElement>& currentElement,
                         readerInfo& readerInf,
                         CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Load*>(nullptr), "load", readerInf, parentObject);
    }

    CoreObject* loadExtra(std::shared_ptr<readerElement>& currentElement,
                          readerInfo& readerInf,
                          CoreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<CoreObject*>(nullptr), "extra", readerInf, parentObject);
    }

    CoreObject* loadBus(std::shared_ptr<readerElement>& currentElement,
                        readerInfo& readerInf,
                        CoreObject* parentObject)
    {
        return readBusElement(currentElement, readerInf, parentObject);
    }

    CoreObject* loadRelay(std::shared_ptr<readerElement>& currentElement,
                          readerInfo& readerInf,
                          CoreObject* parentObject)
    {
        return readRelayElement(currentElement, readerInf, parentObject);
    }

    CoreObject* loadArea(std::shared_ptr<readerElement>& currentElement,
                         readerInfo& readerInf,
                         CoreObject* parentObject)
    {
        return readAreaElement(currentElement, readerInf, parentObject);
    }

    CoreObject* loadLink(std::shared_ptr<readerElement>& currentElement,
                         readerInfo& readerInf,
                         CoreObject* parentObject)
    {
        return readLinkElement(currentElement, readerInf, parentObject, false);
    }

    CoreObject* loadEcon(std::shared_ptr<readerElement>& currentElement,
                         readerInfo& readerInf,
                         CoreObject* parentObject)
    {
        readEconElement(currentElement, readerInf, parentObject);
        return parentObject;
    }

    CoreObject* loadArray(std::shared_ptr<readerElement>& currentElement,
                          readerInfo& readerInf,
                          CoreObject* parentObject)
    {
        readArrayElement(currentElement, readerInf, parentObject);
        return parentObject;
    }

    CoreObject* loadIf(std::shared_ptr<readerElement>& currentElement,
                       readerInfo& readerInf,
                       CoreObject* parentObject)
    {
        loadConditionElement(currentElement, readerInf, parentObject);
        return parentObject;
    }

    const std::array<load_function_entry, 19> loadFunctionMap{
        {{.name = "genmodel", .loader = &loadGenModel},
         {.name = "exciter", .loader = &loadExciter},
         {.name = "governor", .loader = &loadGovernor},
         {.name = "pss", .loader = &loadPss},
         {.name = "source", .loader = &loadSource},
         {.name = "scheduler", .loader = &loadScheduler},
         {.name = "agc", .loader = &loadAgc},
         {.name = "reservedispatcher", .loader = &loadReserveDispatcher},
         {.name = "block", .loader = &loadBlock},
         {.name = "generator", .loader = &loadGenerator},
         {.name = "load", .loader = &loadLoad},
         {.name = "extra", .loader = &loadExtra},
         {.name = "bus", .loader = &loadBus},
         {.name = "relay", .loader = &loadRelay},
         {.name = "area", .loader = &loadArea},
         {.name = "link", .loader = &loadLink},
         {.name = "econ", .loader = &loadEcon},
         {.name = "array", .loader = &loadArray},
         {.name = "if", .loader = &loadIf}}};

    // NOLINTNEXTLINE(bugprone-throwing-static-initialization)
    const IgnoreListType customIgnore{"args",
                                      "arg1",
                                      "arg2",
                                      "arg3",
                                      "arg4",
                                      "arg5",
                                      "arg6",
                                      "arg7",
                                      "arg8",
                                      "arg9",
                                      "arg0"};
}  // namespace

void loadSubObjects(std::shared_ptr<readerElement>& element,
                    readerInfo& readerInformation,
                    CoreObject* parentObject)
{
    // read areas first to set them up for other things to call
    if (element->hasElement("area")) {
        element->moveToFirstChild("area");
        while (element->isValid()) {
            readAreaElement(element, readerInformation, parentObject);
            element->moveToNextSibling("area");  // next area
        }
        element->moveToParent();
    }

    // then load the buses as the nodes
    if (element->hasElement("bus")) {
        element->moveToFirstChild("bus");
        while (element->isValid()) {
            readBusElement(element, readerInformation, parentObject);
            element->moveToNextSibling("bus");  // next bus
        }
        element->moveToParent();
    }

    element->moveToFirstChild();
    while (element->isValid()) {
        auto fieldName = gmlc::utilities::convertToLowerCase(element->getName());
        if ((fieldName == "bus") || (fieldName == "area")) {
            element->moveToNextSibling();
            continue;
        }

        if (fieldName == "local")  // shortcut to do more loading on the parent object most useful
                                   // in loops to add
        // stacked parameters and imports
        {
            loadElementInformation(
                parentObject, element, fieldName, readerInformation, IgnoreListType{});
        } else {
            // std::cout<<"library model :"<<fieldName<<":\n";
            auto objectName = readerInformation.objectNameTranslate(fieldName);
            if (objectName == "collector") {
                loadCollectorElement(element, parentObject, readerInformation);
            } else if (objectName == "event") {
                loadEventElement(element, parentObject, readerInformation);
            } else {
                const auto* const reader =
                    std::find_if(loadFunctionMap.data(),
                                 loadFunctionMap.data() + loadFunctionMap.size(),
                                 [&objectName](const auto& entry) {
                                     return entry.name == objectName;
                                 });
                if (reader != loadFunctionMap.data() + loadFunctionMap.size()) {
                    const auto* object = reader->loader(element, readerInformation, parentObject);
                    if ((object->isRoot()) && (object != parentObject)) {
                        WARNPRINT(READER_WARN_IMPORTANT,
                                  object->getName() << " not owned by any other object");
                    }
                } else if (readerInformation.isCustomElement(objectName)) {
                    auto customElementData = readerInformation.getCustomElement(objectName);
                    auto scopeId = readerInformation.newScope();
                    loadDefines(element, readerInformation);
                    char argVal = '1';
                    std::string argName = "arg";
                    for (int argNum = 1; argNum <= customElementData.second; ++argVal, ++argNum) {
                        argName.push_back(argVal);
                        auto argumentValue = getElementField(element, argName);
                        if (!argumentValue.empty()) {
                            readerInformation.addDefinition(argName, argumentValue);
                        } else {
                            argumentValue = getElementField(customElementData.first, argName);
                            if (!argumentValue.empty()) {
                                readerInformation.addDefinition(argName, argumentValue);
                            } else {
                                WARNPRINT(READER_WARN_IMPORTANT,
                                          "custom element " << argName << " not specified");
                            }
                        }
                        argName.pop_back();
                    }
                    loadElementInformation(parentObject,
                                           customElementData.first,
                                           objectName,
                                           readerInformation,
                                           customIgnore);
                    readerInformation.closeScope(scopeId);
                }
            }
        }
        element->moveToNextSibling();
    }
    element->moveToParent();
}

}  // namespace griddyn
