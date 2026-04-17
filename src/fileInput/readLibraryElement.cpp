/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "readElement.h"
#include "readerHelper.h"
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>

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
#define READSIGNATURE [](std::shared_ptr<readerElement> & cd, readerInfo & ri)

static const std::map<std::string,
                      std::function<coreObject*(std::shared_ptr<readerElement>&, readerInfo&)>>
    loadFunctionMap{
        // clang-format off
    {"genmodel", READSIGNATURE{return ElementReader (cd, static_cast<GenModel *>(nullptr), "genmodel", ri, nullptr);}},
    {"exciter", READSIGNATURE{return ElementReader (cd, static_cast<Exciter *>(nullptr), "exciter", ri, nullptr);}},
    {"governor", READSIGNATURE{return ElementReader (cd, static_cast<Governor *>(nullptr), "governor", ri, nullptr);}},
    {"pss", READSIGNATURE{return ElementReader (cd, static_cast<Stabilizer *>(nullptr), "pss", ri, nullptr);}},
    {"source", READSIGNATURE{return ElementReader (cd, static_cast<Source *>(nullptr), "source", ri, nullptr);}},
    {"controlblock", READSIGNATURE{return ElementReader (cd, static_cast<Block *>(nullptr), "controlblock", ri, nullptr);}},
    {"generator", READSIGNATURE{return ElementReader (cd, static_cast<Generator *>(nullptr), "generator", ri, nullptr);}},
    {"load", READSIGNATURE{return ElementReader (cd, static_cast<Load *>(nullptr), "load", ri, nullptr);}},
    {"bus", READSIGNATURE{return readBusElement (cd, ri, nullptr);}},
    {"relay", READSIGNATURE{return readRelayElement (cd, ri, nullptr);}},
    {"area", READSIGNATURE{return readAreaElement (cd, ri, nullptr);}},
    {"link", READSIGNATURE{return readLinkElement (cd, ri, nullptr, false);}},
    {"scheduler", READSIGNATURE{return ElementReader (cd, static_cast<scheduler *>(nullptr), "scheduler", ri, nullptr);}},
    {"agc", READSIGNATURE{return ElementReader (cd, static_cast<AGControl *>(nullptr), "agc", ri, nullptr);}},
    {"econ", READSIGNATURE{return readEconElement (cd, ri, nullptr);}},
    {"reservedispatcher",READSIGNATURE{return ElementReader (cd, static_cast<reserveDispatcher *>(nullptr), "reserveDispatcher", ri, nullptr);}},
// clang-format on
}
;

void readLibraryElement(std::shared_ptr<readerElement>& element, readerInfo& ri)
{
    auto riScope = ri.newScope();
    // readerInfo xm2;
    std::string baseName = element->getName();
    element->bookmark();

    loadDefines(element, ri);
    loadDirectories(element, ri);
    // loop through the other children
    element->moveToFirstChild();

    while (element->isValid()) {
        coreObject* obj = nullptr;
        std::string fieldName = gmlc::utilities::convertToLowerCase(element->getName());
        // std::cout<<"library model :"<<fieldName<<":\n";
        if ((fieldName == "define") || (fieldName == "recorder") || (fieldName == "event")) {
        } else {
            auto obname = ri.objectNameTranslate(fieldName);
            auto rval = loadFunctionMap.find(obname);
            if (rval != loadFunctionMap.end()) {
                std::string bname = element->getName();
                obj = rval->second(element, ri);
                assert(bname == element->getName());
            } else {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "Unrecognized object type " << fieldName << " in library");
            }
        }
        if (obj != nullptr) {
            std::vector<gridParameter> pf;
            bool found = ri.addLibraryObject(obj, pf);
            if (found) {
                LEVELPRINT(READER_VERBOSE_PRINT,
                           "adding " << fieldName << " " << obj->getName() << " to Library");
            } else {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "Duplicate library objects: ignoring second object " << obj->getName());
                removeReference(obj);
            }
        }
        element->moveToNextSibling();
    }

    element->restore();
    assert(element->getName() == baseName);
    ri.closeScope(riScope);
}

static const char defineString[] = "define";

void loadDefines(std::shared_ptr<readerElement>& element, readerInfo& ri)
{
    if (!element->hasElement(defineString)) {
        return;
    }
    std::string def;
    std::string rep;

    // loop through all define elements
    element->moveToFirstChild(defineString);
    while (element->isValid()) {
        if (element->hasAttribute("name")) {
            def = element->getAttributeText("name");
        } else if (element->hasAttribute("string")) {
            def = element->getAttributeText("string");
        } else {
            WARNPRINT(READER_WARN_ALL, "define element with no name or string attribute");
            element->moveToNextSibling(defineString);  // next define
            continue;
        }
        if (element->hasAttribute("value")) {
            rep = element->getAttributeText("value");
        } else if (element->hasAttribute("replacement")) {
            rep = element->getAttributeText("replacement");
        } else {
            rep = element->getText();
        }
        bool locked = false;
        if (element->hasAttribute("locked")) {
            auto lockstr = element->getAttributeText("locked");
            locked = ((lockstr == "true") || (lockstr == "1"));
        }

        auto kcheck = ri.checkDefines(rep);
        if (def == kcheck) {
            WARNPRINT(READER_WARN_ALL,
                      "illegal recursive definition " << def << " name and value are equivalent");
            element->moveToNextSibling("define");  // next define
            continue;
        }
        // check for overloading
        if (element->hasAttribute("eval")) {
            double val = interpretString(rep, ri);
            if (std::isnormal(val)) {
                if (std::abs(trunc(val) - val) < 1e-9) {
                    rep = std::to_string(static_cast<int>(val));
                } else {
                    rep = std::to_string(val);
                }
            }
        }

        if (locked) {
            ri.addLockedDefinition(def, rep);
        } else {
            ri.addDefinition(def, rep);
        }

        element->moveToNextSibling(defineString);  // next define
    }
    element->moveToParent();
}

static const char directoryString[] = "directory";

void loadDirectories(std::shared_ptr<readerElement>& element, readerInfo& ri)
{
    // loop through all directory elements
    if (!element->hasElement(directoryString)) {
        return;
    }
    element->moveToFirstChild(directoryString);
    while (element->isValid()) {
        std::string dfld = (element->hasAttribute("value")) ? element->getAttributeText("value") :
                                                              element->getText();

        ri.addDirectory(dfld);
        element->moveToNextSibling(directoryString);
    }
    element->moveToParent();
}

static const char customString[] = "custom";
void loadCustomSections(std::shared_ptr<readerElement>& element, readerInfo& ri)
{
    if (!element->hasElement(customString)) {
        return;
    }
    element->moveToFirstChild(customString);
    while (element->isValid()) {
        auto name = getElementField(element, "name");
        if (name.empty()) {
            WARNPRINT(READER_WARN_ALL, "name not specified for custom object");
            element->moveToNextSibling(customString);
            continue;
        }
        auto args = element->getAttributeValue("args");
        int nargs = (args != kNullVal) ? static_cast<int>(args) : 0;
        ri.addCustomElement(name, element, nargs);
        element->moveToNextSibling(directoryString);
    }
    element->moveToParent();
}

static const char translateString[] = "translate";
void loadTranslations(std::shared_ptr<readerElement>& element, readerInfo& ri)
{
    if (!element->hasElement(translateString)) {
        return;
    }
    // loop through all define elements
    element->moveToFirstChild(translateString);
    while (element->isValid()) {
        std::string def;
        if (element->hasAttribute("name")) {
            def = element->getAttributeText("name");
        } else if (element->hasAttribute("string")) {
            def = element->getAttributeText("string");
        }

        std::string component = element->getAttributeText("component");

        if ((def.empty()) && (component.empty())) {
            WARNPRINT(READER_WARN_ALL, "neither name nor component specified in translation");
            element->moveToNextSibling(translateString);
            continue;
        }

        auto kcheck = ri.objectNameTranslate(component);
        if (def == kcheck) {
            WARNPRINT(READER_WARN_ALL,
                      "illegal recursive object name translation "
                          << def << " name and value are equivalent");
            element->moveToNextSibling(translateString);
            continue;
        }

        std::string type = element->getAttributeText("type");

        if (type.empty()) {
            if ((def.empty()) && (component.empty())) {
                WARNPRINT(READER_WARN_ALL,
                          "both name and component must be specified with no type definition");
                element->moveToNextSibling(translateString);
                continue;
            }
            ri.addTranslate(def, component);
        } else {
            if (def.empty()) {
                ri.addTranslateType(component, type);
            } else if (component.empty()) {
                ri.addTranslateType(def, type);
            } else {
                ri.addTranslate(def, component, type);
            }
        }

        element->moveToNextSibling(translateString);
    }
    element->moveToParent();
}

}  // namespace griddyn
