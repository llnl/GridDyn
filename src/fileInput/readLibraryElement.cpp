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
namespace {
#define READSIGNATURE \
    [](std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string,
                      std::function<coreObject*(std::shared_ptr<readerElement>&, readerInfo&)>,
                      std::less<>>
    loadFunctionMap{
        // clang-format off
    {"genmodel", READSIGNATURE{return ElementReader (currentElement, static_cast<GenModel *>(nullptr), "genmodel", readerInf, nullptr);}},
    {"exciter", READSIGNATURE{return ElementReader (currentElement, static_cast<Exciter *>(nullptr), "exciter", readerInf, nullptr);}},
    {"governor", READSIGNATURE{return ElementReader (currentElement, static_cast<Governor *>(nullptr), "governor", readerInf, nullptr);}},
    {"pss", READSIGNATURE{return ElementReader (currentElement, static_cast<Stabilizer *>(nullptr), "pss", readerInf, nullptr);}},
    {"source", READSIGNATURE{return ElementReader (currentElement, static_cast<Source *>(nullptr), "source", readerInf, nullptr);}},
    {"controlblock", READSIGNATURE{return ElementReader (currentElement, static_cast<Block *>(nullptr), "controlblock", readerInf, nullptr);}},
    {"generator", READSIGNATURE{return ElementReader (currentElement, static_cast<Generator *>(nullptr), "generator", readerInf, nullptr);}},
    {"load", READSIGNATURE{return ElementReader (currentElement, static_cast<Load *>(nullptr), "load", readerInf, nullptr);}},
    {"bus", READSIGNATURE{return readBusElement (currentElement, readerInf, nullptr);}},
    {"relay", READSIGNATURE{return readRelayElement (currentElement, readerInf, nullptr);}},
    {"area", READSIGNATURE{return readAreaElement (currentElement, readerInf, nullptr);}},
    {"link", READSIGNATURE{return readLinkElement (currentElement, readerInf, nullptr, false);}},
    {"scheduler", READSIGNATURE{return ElementReader (currentElement, static_cast<scheduler *>(nullptr), "scheduler", readerInf, nullptr);}},
    {"agc", READSIGNATURE{return ElementReader (currentElement, static_cast<AGControl *>(nullptr), "agc", readerInf, nullptr);}},
    {"econ", READSIGNATURE{return readEconElement (currentElement, readerInf, nullptr);}},
    {"reservedispatcher",READSIGNATURE{return ElementReader (currentElement, static_cast<reserveDispatcher *>(nullptr), "reserveDispatcher", readerInf, nullptr);}},
// clang-format on
}
;
}  // namespace

void readLibraryElement(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
{
    auto riScope = readerInf.newScope();
    // readerInfo xm2;
    const std::string baseName = element->getName();
    element->bookmark();

    loadDefines(element, readerInf);
    loadDirectories(element, readerInf);
    // loop through the other children
    element->moveToFirstChild();

    while (element->isValid()) {
        coreObject* obj = nullptr;
        const std::string fieldName = gmlc::utilities::convertToLowerCase(element->getName());
        // std::cout<<"library model :"<<fieldName<<":\n";
        if ((fieldName == "define") || (fieldName == "recorder") || (fieldName == "event")) {
        } else {
            auto obname = readerInf.objectNameTranslate(fieldName);
            auto rval = loadFunctionMap.find(obname);
            if (rval != loadFunctionMap.end()) {
                const std::string bname = element->getName();
                obj = rval->second(element, readerInf);
                assert(bname == element->getName());
            } else {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "Unrecognized object type " << fieldName << " in library");
            }
        }
        if (obj != nullptr) {
            std::vector<gridParameter> paramFields;
            const bool found = readerInf.addLibraryObject(obj, paramFields);
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
    readerInf.closeScope(riScope);
}

static const char defineString[] = "define";

void loadDefines(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
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

        auto kcheck = readerInf.checkDefines(rep);
        if (def == kcheck) {
            WARNPRINT(READER_WARN_ALL,
                      "illegal recursive definition " << def << " name and value are equivalent");
            element->moveToNextSibling("define");  // next define
            continue;
        }
        // check for overloading
        if (element->hasAttribute("eval")) {
            const double val = interpretString(rep, readerInf);
            if (std::isnormal(val)) {
                if (std::abs(trunc(val) - val) < 1e-9) {
                    rep = std::to_string(static_cast<int>(val));
                } else {
                    rep = std::to_string(val);
                }
            }
        }

        if (locked) {
            readerInf.addLockedDefinition(def, rep);
        } else {
            readerInf.addDefinition(def, rep);
        }

        element->moveToNextSibling(defineString);  // next define
    }
    element->moveToParent();
}

static const char directoryString[] = "directory";

void loadDirectories(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
{
    // loop through all directory elements
    if (!element->hasElement(directoryString)) {
        return;
    }
    element->moveToFirstChild(directoryString);
    while (element->isValid()) {
        const std::string dfld = (element->hasAttribute("value")) ?
            element->getAttributeText("value") :
            element->getText();

        readerInf.addDirectory(dfld);
        element->moveToNextSibling(directoryString);
    }
    element->moveToParent();
}

static const char customString[] = "custom";
void loadCustomSections(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
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
        const int nargs = (args != kNullVal) ? static_cast<int>(args) : 0;
        readerInf.addCustomElement(name, element, nargs);
        element->moveToNextSibling(directoryString);
    }
    element->moveToParent();
}

static const char translateString[] = "translate";
void loadTranslations(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
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

        const std::string component = element->getAttributeText("component");

        if ((def.empty()) && (component.empty())) {
            WARNPRINT(READER_WARN_ALL, "neither name nor component specified in translation");
            element->moveToNextSibling(translateString);
            continue;
        }

        auto kcheck = readerInf.objectNameTranslate(component);
        if (def == kcheck) {
            WARNPRINT(READER_WARN_ALL,
                      "illegal recursive object name translation "
                          << def << " name and value are equivalent");
            element->moveToNextSibling(translateString);
            continue;
        }

        const std::string type = element->getAttributeText("type");

        if (type.empty()) {
            if ((def.empty()) && (component.empty())) {
                WARNPRINT(READER_WARN_ALL,
                          "both name and component must be specified with no type definition");
                element->moveToNextSibling(translateString);
                continue;
            }
            readerInf.addTranslate(def, component);
        } else {
            if (def.empty()) {
                readerInf.addTranslateType(component, type);
            } else if (component.empty()) {
                readerInf.addTranslateType(def, type);
            } else {
                readerInf.addTranslate(def, component, type);
            }
        }

        element->moveToNextSibling(translateString);
    }
    element->moveToParent();
}

}  // namespace griddyn
