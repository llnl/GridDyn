/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "readElement.h"
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>
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
// clang-format off
#define READERSIGNATURE \
    [](std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf, coreObject* parentObject)

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string,
                      std::function<coreObject*(std::shared_ptr<readerElement>&,
                                                readerInfo&,
                                                coreObject* parent)>,
                      std::less<>>
loadFunctionMap{
  {"genmodel", READERSIGNATURE{return ElementReader(currentElement, static_cast<GenModel *>(nullptr), "genmodel", readerInf, parentObject);}},
      {"exciter", READERSIGNATURE{return ElementReader (currentElement, static_cast<Exciter *>(nullptr), "exciter", readerInf, parentObject);}},
    {"governor", READERSIGNATURE{return ElementReader (currentElement, static_cast<Governor *>(nullptr), "governor", readerInf, parentObject);}},
    {"pss", READERSIGNATURE{return ElementReader (currentElement, static_cast<Stabilizer *>(nullptr), "pss", readerInf, parentObject);}},
    {"source", READERSIGNATURE{return ElementReader (currentElement, static_cast<Source *>(nullptr), "source", readerInf, parentObject);}},
    {"scheduler", READERSIGNATURE{return ElementReader (currentElement, static_cast<scheduler *>(nullptr), "scheduler", readerInf, parentObject);}},
    {"agc", READERSIGNATURE{return ElementReader (currentElement, static_cast<AGControl *>(nullptr), "agc", readerInf, parentObject);}},
    {"reservedispatcher",READERSIGNATURE{return ElementReader (currentElement, static_cast<reserveDispatcher *>(nullptr), "reservedispatcher", readerInf, parentObject);}},
    {"block", READERSIGNATURE{return ElementReader (currentElement, static_cast<Block *>(nullptr), "block", readerInf, parentObject);}},
    {"generator", READERSIGNATURE{return ElementReader (currentElement, static_cast<Generator *>(nullptr), "generator", readerInf, parentObject);}},
    {"load", READERSIGNATURE{return ElementReader (currentElement, static_cast<Load *>(nullptr), "load", readerInf, parentObject);}},
    {"extra", READERSIGNATURE{ return ElementReader(currentElement, static_cast<coreObject *>(nullptr), "extra", readerInf, parentObject); } },
    {"bus", READERSIGNATURE{return readBusElement (currentElement, readerInf, parentObject);}},
    {"relay", READERSIGNATURE{return readRelayElement (currentElement, readerInf, parentObject);}},
    {"area", READERSIGNATURE{return readAreaElement (currentElement, readerInf, parentObject);}},
    {"link", READERSIGNATURE{return readLinkElement (currentElement, readerInf, parentObject, false);}},
    {"econ", READERSIGNATURE{readEconElement (currentElement, readerInf, parentObject);return parentObject;}},
    {"array", READERSIGNATURE{readArrayElement (currentElement, readerInf, parentObject);return parentObject;}},
    {"if", READERSIGNATURE{loadConditionElement (currentElement, readerInf, parentObject);return parentObject;}}
};
// clang-format on

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const IgnoreListType customIgnore{"args",
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
                auto rval = loadFunctionMap.find(obname);
                if (rval != loadFunctionMap.end()) {
                    const auto* obj = rval->second(element, readerInf, parentObject);
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
