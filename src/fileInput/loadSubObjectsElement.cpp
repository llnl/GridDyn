/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
// clang-format off
#define READERSIGNATURE [](std::shared_ptr<readerElement> & cd, readerInfo & ri, coreObject * parent)

static const std::map<std::string, std::function<coreObject *(std::shared_ptr<readerElement> &, readerInfo &, coreObject *parent)>>
loadFunctionMap{
  {"genmodel", READERSIGNATURE{return ElementReader(cd, static_cast<GenModel *>(nullptr), "genmodel", ri, parent);}},
      {"exciter", READERSIGNATURE{return ElementReader (cd, static_cast<Exciter *>(nullptr), "exciter", ri, parent);}},
    {"governor", READERSIGNATURE{return ElementReader (cd, static_cast<Governor *>(nullptr), "governor", ri, parent);}},
    {"pss", READERSIGNATURE{return ElementReader (cd, static_cast<Stabilizer *>(nullptr), "pss", ri, parent);}},
    {"source", READERSIGNATURE{return ElementReader (cd, static_cast<Source *>(nullptr), "source", ri, parent);}},
    {"scheduler", READERSIGNATURE{return ElementReader (cd, static_cast<scheduler *>(nullptr), "scheduler", ri, parent);}},
    {"agc", READERSIGNATURE{return ElementReader (cd, static_cast<AGControl *>(nullptr), "agc", ri, parent);}},
    {"reservedispatcher",READERSIGNATURE{return ElementReader (cd, static_cast<reserveDispatcher *>(nullptr), "reservedispatcher", ri, parent);}},
    {"block", READERSIGNATURE{return ElementReader (cd, static_cast<Block *>(nullptr), "block", ri, parent);}},
    {"generator", READERSIGNATURE{return ElementReader (cd, static_cast<Generator *>(nullptr), "generator", ri, parent);}},
    {"load", READERSIGNATURE{return ElementReader (cd, static_cast<Load *>(nullptr), "load", ri, parent);}},
    {"extra", READERSIGNATURE{ return ElementReader(cd, static_cast<coreObject *>(nullptr), "extra", ri, parent); } },
    {"bus", READERSIGNATURE{return readBusElement (cd, ri, parent);}},
    {"relay", READERSIGNATURE{return readRelayElement (cd, ri, parent);}},
    {"area", READERSIGNATURE{return readAreaElement (cd, ri, parent);}},
    {"link", READERSIGNATURE{return readLinkElement (cd, ri, parent, false);}},
    {"econ", READERSIGNATURE{readEconElement (cd, ri, parent);return parent;}},
    {"array", READERSIGNATURE{readArrayElement (cd, ri, parent);return parent;}},
    {"if", READERSIGNATURE{loadConditionElement (cd, ri, parent);return parent;}}
};
// clang-format on

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

void loadSubObjects(std::shared_ptr<readerElement>& element,
                    readerInfo& ri,
                    coreObject* parentObject)
{
    // read areas first to set them up for other things to call
    if (element->hasElement("area")) {
        element->moveToFirstChild("area");
        while (element->isValid()) {
            readAreaElement(element, ri, parentObject);
            element->moveToNextSibling("area");  // next area
        }
        element->moveToParent();
    }

    // then load the buses as the nodes
    if (element->hasElement("bus")) {
        element->moveToFirstChild("bus");
        while (element->isValid()) {
            readBusElement(element, ri, parentObject);
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
            loadElementInformation(parentObject,
                                   element,
                                   fieldName,
                                   ri,
                                   IgnoreListType{});
        } else {
            // std::cout<<"library model :"<<fieldName<<":\n";
            auto obname = ri.objectNameTranslate(fieldName);
            if (obname == "collector") {
                loadCollectorElement(element, parentObject, ri);
            } else if (obname == "event") {
                loadEventElement(element, parentObject, ri);
            } else {
                auto rval = loadFunctionMap.find(obname);
                if (rval != loadFunctionMap.end()) {
                    coreObject* obj = rval->second(element, ri, parentObject);
                    if ((obj->isRoot()) && (obj != parentObject)) {
                        WARNPRINT(READER_WARN_IMPORTANT,
                                  obj->getName() << " not owned by any other object");
                    }
                } else if (ri.isCustomElement(obname)) {
                    auto customElementPair = ri.getCustomElement(obname);
                    auto scopeID = ri.newScope();
                    loadDefines(element, ri);
                    char argVal = '1';
                    std::string argName = "arg";
                    for (int argNum = 1; argNum <= customElementPair.second; ++argVal, ++argNum) {
                        argName.push_back(argVal);
                        auto av = getElementField(element, argName);
                        if (!av.empty()) {
                            ri.addDefinition(argName, av);
                        } else {
                            av = getElementField(customElementPair.first, argName);
                            if (!av.empty()) {
                                ri.addDefinition(argName, av);
                            } else {
                                WARNPRINT(READER_WARN_IMPORTANT,
                                          "custom element " << argName << " not specified");
                            }
                        }
                        argName.pop_back();
                    }
                    loadElementInformation(
                        parentObject, customElementPair.first, obname, ri, customIgnore);
                    ri.closeScope(scopeID);
                }
            }
        }
        element->moveToNextSibling();
    }
    element->moveToParent();
}

}  // namespace griddyn
