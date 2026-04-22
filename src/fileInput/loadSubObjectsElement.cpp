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
    using load_function_t = coreObject* (*)(std::shared_ptr<readerElement>&,
                                            readerInfo&,
                                            coreObject* parent);

    struct load_function_entry {
        const char* name;
        load_function_t loader;
    };

    static coreObject* loadGenModel(std::shared_ptr<readerElement>& currentElement,
                                    readerInfo& readerInf,
                                    coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<GenModel*>(nullptr), "genmodel", readerInf, parentObject);
    }

    static coreObject* loadExciter(std::shared_ptr<readerElement>& currentElement,
                                   readerInfo& readerInf,
                                   coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Exciter*>(nullptr), "exciter", readerInf, parentObject);
    }

    static coreObject* loadGovernor(std::shared_ptr<readerElement>& currentElement,
                                    readerInfo& readerInf,
                                    coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Governor*>(nullptr), "governor", readerInf, parentObject);
    }

    static coreObject* loadPss(std::shared_ptr<readerElement>& currentElement,
                               readerInfo& readerInf,
                               coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Stabilizer*>(nullptr), "pss", readerInf, parentObject);
    }

    static coreObject* loadSource(std::shared_ptr<readerElement>& currentElement,
                                  readerInfo& readerInf,
                                  coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Source*>(nullptr), "source", readerInf, parentObject);
    }

    static coreObject* loadScheduler(std::shared_ptr<readerElement>& currentElement,
                                     readerInfo& readerInf,
                                     coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<scheduler*>(nullptr), "scheduler", readerInf, parentObject);
    }

    static coreObject* loadAgc(std::shared_ptr<readerElement>& currentElement,
                               readerInfo& readerInf,
                               coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<AGControl*>(nullptr), "agc", readerInf, parentObject);
    }

    static coreObject* loadReserveDispatcher(std::shared_ptr<readerElement>& currentElement,
                                             readerInfo& readerInf,
                                             coreObject* parentObject)
    {
        return ElementReader(currentElement,
                             static_cast<reserveDispatcher*>(nullptr),
                             "reservedispatcher",
                             readerInf,
                             parentObject);
    }

    static coreObject* loadBlock(std::shared_ptr<readerElement>& currentElement,
                                 readerInfo& readerInf,
                                 coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Block*>(nullptr), "block", readerInf, parentObject);
    }

    static coreObject* loadGenerator(std::shared_ptr<readerElement>& currentElement,
                                     readerInfo& readerInf,
                                     coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Generator*>(nullptr), "generator", readerInf, parentObject);
    }

    static coreObject* loadLoad(std::shared_ptr<readerElement>& currentElement,
                                readerInfo& readerInf,
                                coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<Load*>(nullptr), "load", readerInf, parentObject);
    }

    static coreObject* loadExtra(std::shared_ptr<readerElement>& currentElement,
                                 readerInfo& readerInf,
                                 coreObject* parentObject)
    {
        return ElementReader(
            currentElement, static_cast<coreObject*>(nullptr), "extra", readerInf, parentObject);
    }

    static coreObject* loadBus(std::shared_ptr<readerElement>& currentElement,
                               readerInfo& readerInf,
                               coreObject* parentObject)
    {
        return readBusElement(currentElement, readerInf, parentObject);
    }

    static coreObject* loadRelay(std::shared_ptr<readerElement>& currentElement,
                                 readerInfo& readerInf,
                                 coreObject* parentObject)
    {
        return readRelayElement(currentElement, readerInf, parentObject);
    }

    static coreObject* loadArea(std::shared_ptr<readerElement>& currentElement,
                                readerInfo& readerInf,
                                coreObject* parentObject)
    {
        return readAreaElement(currentElement, readerInf, parentObject);
    }

    static coreObject* loadLink(std::shared_ptr<readerElement>& currentElement,
                                readerInfo& readerInf,
                                coreObject* parentObject)
    {
        return readLinkElement(currentElement, readerInf, parentObject, false);
    }

    static coreObject* loadEcon(std::shared_ptr<readerElement>& currentElement,
                                readerInfo& readerInf,
                                coreObject* parentObject)
    {
        readEconElement(currentElement, readerInf, parentObject);
        return parentObject;
    }

    static coreObject* loadArray(std::shared_ptr<readerElement>& currentElement,
                                 readerInfo& readerInf,
                                 coreObject* parentObject)
    {
        readArrayElement(currentElement, readerInf, parentObject);
        return parentObject;
    }

    static coreObject* loadIf(std::shared_ptr<readerElement>& currentElement,
                              readerInfo& readerInf,
                              coreObject* parentObject)
    {
        loadConditionElement(currentElement, readerInf, parentObject);
        return parentObject;
    }

    const std::array<load_function_entry, 19> loadFunctionMap{
        {{"genmodel", &loadGenModel},
         {"exciter", &loadExciter},
         {"governor", &loadGovernor},
         {"pss", &loadPss},
         {"source", &loadSource},
         {"scheduler", &loadScheduler},
         {"agc", &loadAgc},
         {"reservedispatcher", &loadReserveDispatcher},
         {"block", &loadBlock},
         {"generator", &loadGenerator},
         {"load", &loadLoad},
         {"extra", &loadExtra},
         {"bus", &loadBus},
         {"relay", &loadRelay},
         {"area", &loadArea},
         {"link", &loadLink},
         {"econ", &loadEcon},
         {"array", &loadArray},
         {"if", &loadIf}}};

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
                    readerInfo& readerInf,
                    coreObject* parentObject)
{
    // read areas first to set them up for other things to call
    if (element->hasElement("area")) {
        element->moveToFirstChild("area");
        while (element->isValid()) {
            readAreaElement(element, readerInf, parentObject);
            element->moveToNextSibling("area");  // next area
        }
        element->moveToParent();
    }

    // then load the buses as the nodes
    if (element->hasElement("bus")) {
        element->moveToFirstChild("bus");
        while (element->isValid()) {
            readBusElement(element, readerInf, parentObject);
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
            loadElementInformation(parentObject, element, fieldName, readerInf, IgnoreListType{});
        } else {
            // std::cout<<"library model :"<<fieldName<<":\n";
            auto obname = readerInf.objectNameTranslate(fieldName);
            if (obname == "collector") {
                loadCollectorElement(element, parentObject, readerInf);
            } else if (obname == "event") {
                loadEventElement(element, parentObject, readerInf);
            } else {
                const auto* const reader =
                    std::find_if(loadFunctionMap.data(),
                                 loadFunctionMap.data() + loadFunctionMap.size(),
                                 [&obname](const auto& entry) { return entry.name == obname; });
                if (reader != loadFunctionMap.data() + loadFunctionMap.size()) {
                    const auto* obj = reader->loader(element, readerInf, parentObject);
                    if ((obj->isRoot()) && (obj != parentObject)) {
                        WARNPRINT(READER_WARN_IMPORTANT,
                                  obj->getName() << " not owned by any other object");
                    }
                } else if (readerInf.isCustomElement(obname)) {
                    auto customElementPair = readerInf.getCustomElement(obname);
                    auto scopeID = readerInf.newScope();
                    loadDefines(element, readerInf);
                    char argVal = '1';
                    std::string argName = "arg";
                    for (int argNum = 1; argNum <= customElementPair.second; ++argVal, ++argNum) {
                        argName.push_back(argVal);
                        auto argumentValue = getElementField(element, argName);
                        if (!argumentValue.empty()) {
                            readerInf.addDefinition(argName, argumentValue);
                        } else {
                            argumentValue = getElementField(customElementPair.first, argName);
                            if (!argumentValue.empty()) {
                                readerInf.addDefinition(argName, argumentValue);
                            } else {
                                WARNPRINT(READER_WARN_IMPORTANT,
                                          "custom element " << argName << " not specified");
                            }
                        }
                        argName.pop_back();
                    }
                    loadElementInformation(
                        parentObject, customElementPair.first, obname, readerInf, customIgnore);
                    readerInf.closeScope(scopeID);
                }
            }
        }
        element->moveToNextSibling();
    }
    element->moveToParent();
}

}  // namespace griddyn
