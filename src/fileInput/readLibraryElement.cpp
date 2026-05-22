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
#include <array>
#include <cassert>
#include <string>
#include <vector>

// A bunch of includes to load these kinds of objects
#include "griddyn/Block.h"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/Relay.h"
#include "griddyn/Source.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/controllers/AGControl.h"
#include "griddyn/controllers/ReserveDispatcher.h"
#include "griddyn/controllers/Scheduler.h"
#include "griddyn/loads/ZipLoad.h"

namespace griddyn {
namespace {
    using load_function_t = CoreObject* (*)(std::shared_ptr<readerElement>&, readerInfo&);

    struct LoadFunctionEntry {
        const char* mName;
        load_function_t mLoader;
    };

    CoreObject* loadGenModel(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<GenModel*>(nullptr), "genmodel", readerInf, nullptr);
    }

    CoreObject* loadExciter(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<Exciter*>(nullptr), "exciter", readerInf, nullptr);
    }

    CoreObject* loadGovernor(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<Governor*>(nullptr), "governor", readerInf, nullptr);
    }

    CoreObject* loadPss(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<Stabilizer*>(nullptr), "pss", readerInf, nullptr);
    }

    CoreObject* loadSource(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<Source*>(nullptr), "source", readerInf, nullptr);
    }

    CoreObject* loadControlBlock(std::shared_ptr<readerElement>& currentElement,
                                 readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<GridBlock*>(nullptr), "controlblock", readerInf, nullptr);
    }

    CoreObject* loadGenerator(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<Generator*>(nullptr), "generator", readerInf, nullptr);
    }

    CoreObject* loadLoad(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<GridLoad*>(nullptr), "load", readerInf, nullptr);
    }

    CoreObject* loadBus(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return readBusElement(currentElement, readerInf, nullptr);
    }

    CoreObject* loadRelay(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return readRelayElement(currentElement, readerInf, nullptr);
    }

    CoreObject* loadArea(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return readGridAreaElement(currentElement, readerInf, nullptr);
    }

    CoreObject* loadLink(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return readLinkElement(currentElement, readerInf, nullptr, false);
    }

    CoreObject* loadScheduler(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<scheduler*>(nullptr), "scheduler", readerInf, nullptr);
    }

    CoreObject* loadAgc(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return elementReader(
            currentElement, static_cast<AGControl*>(nullptr), "agc", readerInf, nullptr);
    }

    CoreObject* loadEcon(std::shared_ptr<readerElement>& currentElement, readerInfo& readerInf)
    {
        return readEconElement(currentElement, readerInf, nullptr);
    }

    CoreObject* loadReserveDispatcher(std::shared_ptr<readerElement>& currentElement,
                                      readerInfo& readerInf)
    {
        return elementReader(currentElement,
                             static_cast<ReserveDispatcher*>(nullptr),
                             "reserveDispatcher",
                             readerInf,
                             nullptr);
    }

    constexpr std::array<LoadFunctionEntry, 16> loadFunctionMap{{
        // clang-format off
    {.mName = "genmodel", .mLoader = &loadGenModel},
    {.mName = "exciter", .mLoader = &loadExciter},
    {.mName = "governor", .mLoader = &loadGovernor},
    {.mName = "pss", .mLoader = &loadPss},
    {.mName = "source", .mLoader = &loadSource},
    {.mName = "controlblock", .mLoader = &loadControlBlock},
    {.mName = "generator", .mLoader = &loadGenerator},
    {.mName = "load", .mLoader = &loadLoad},
    {.mName = "bus", .mLoader = &loadBus},
    {.mName = "relay", .mLoader = &loadRelay},
    {.mName = "area", .mLoader = &loadArea},
    {.mName = "link", .mLoader = &loadLink},
    {.mName = "scheduler", .mLoader = &loadScheduler},
    {.mName = "agc", .mLoader = &loadAgc},
    {.mName = "econ", .mLoader = &loadEcon},
    {.mName = "reservedispatcher", .mLoader = &loadReserveDispatcher}
        // clang-format on
    }};
}  // namespace

void readLibraryElement(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
{
    auto riScope = readerInf.newScope();
    element->bookmark();

    loadDefines(element, readerInf);
    loadDirectories(element, readerInf);
    element->moveToFirstChild();

    while (element->isValid()) {
        CoreObject* obj = nullptr;
        const std::string fieldName = gmlc::utilities::convertToLowerCase(element->getName());
        if ((fieldName == "define") || (fieldName == "recorder") || (fieldName == "event")) {
        } else {
            auto translatedName = readerInf.objectNameTranslate(fieldName);
            const auto* const reader = std::find_if(loadFunctionMap.data(),
                                                    loadFunctionMap.data() + loadFunctionMap.size(),
                                                    [&translatedName](const auto& entry) {
                                                        return entry.mName == translatedName;
                                                    });
            if (reader != loadFunctionMap.data() + loadFunctionMap.size()) {
                obj = reader->mLoader(element, readerInf);
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
    readerInf.closeScope(riScope);
}

static constexpr char defineString[] = "define";

void loadDefines(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
{
    if (!element->hasElement(defineString)) {
        return;
    }
    std::string def;
    std::string rep;

    element->moveToFirstChild(defineString);
    while (element->isValid()) {
        if (element->hasAttribute("name")) {
            def = element->getAttributeText("name");
        } else if (element->hasAttribute("string")) {
            def = element->getAttributeText("string");
        } else {
            WARNPRINT(READER_WARN_ALL, "define element with no name or string attribute");
            element->moveToNextSibling(defineString);
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
            element->moveToNextSibling(defineString);
            continue;
        }
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

        element->moveToNextSibling(defineString);
    }
    element->moveToParent();
}

static constexpr char directoryString[] = "directory";

void loadDirectories(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
{
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

static constexpr char customString[] = "custom";
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
        element->moveToNextSibling(customString);
    }
    element->moveToParent();
}

static constexpr char translateString[] = "translate";
void loadTranslations(std::shared_ptr<readerElement>& element, readerInfo& readerInf)
{
    if (!element->hasElement(translateString)) {
        return;
    }
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
