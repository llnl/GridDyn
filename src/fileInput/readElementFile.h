/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreObject.h"
#include "formatInterpreters/readerElement.h"
#include "griddyn/simulation/gridSimulation.h"
#include "readElement.h"
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>

namespace griddyn {
void readConfigurationFields(std::shared_ptr<readerElement>& sim, readerInfo& readerInfoRef);

template<class RX>
CoreObject* loadElementFile(CoreObject* parentObject, const std::string& fileName, readerInfo* ri)
{
    static_assert(std::is_base_of<readerElement, RX>::value,
                  "classes must be inherited from CoreObject");
    // pointers

    CoreObject* gco = nullptr;
    bool rootSimFile = true;
    if (parentObject != nullptr) {
        auto rootObj = parentObject->getRoot();
        if (rootObj->getID() == parentObject->getID()) {
            rootSimFile = true;
            gco = parentObject;

        } else {
            rootSimFile = false;
        }
    } else {
        // set the warn count to 0 for the readerConfig namespace
        readerConfig::warnCount = 0;
    }

    // May need to create a readerInfo object this to ensure it gets deleted even under exception
    std::unique_ptr<readerInfo> rip = (ri == nullptr) ? std::make_unique<readerInfo>() : nullptr;
    readerInfo::scopeID riScope = 0;
    if (ri == nullptr) {
        ri = rip.get();  // we created the unique_ptr now

    } else {
        riScope = ri->newScope();
    }

    std::filesystem::path mainPath(fileName);

    ri->addDirectory(std::filesystem::current_path().string());
    ri->addDirectory(mainPath.parent_path().string());

    // read xml file from location
    LEVELPRINT(READER_SUMMARY_PRINT, "loading file " << fileName);

    auto doc = std::make_shared<RX>(fileName);

    if (!doc->isValid()) {
        WARNPRINT(READER_WARN_ALL, "Unable to open File: " << fileName);
        if (dynamic_cast<gridSimulation*>(parentObject)) {
            dynamic_cast<gridSimulation*>(parentObject)->setErrorCode(GS_INVALID_FILE_ERROR);
        }
        return nullptr;
    }
    auto sim = std::static_pointer_cast<readerElement>(doc);
    if (rootSimFile) {
        readConfigurationFields(sim, *ri);
    }
    gco = (rootSimFile) ?
        readSimulationElement(sim, *ri, nullptr, static_cast<gridSimulation*>(gco)) :
        parentObject;
    std::string name = sim->getName();
    if (!rootSimFile) {
        loadElementInformation(gco, sim, "import", *ri, {"version"});
    }
    if (!rip) {  // scope closing can be expensive so don't do it unless we need to
        ri->closeScope(riScope);
    }

    if (rootSimFile) {
        gco->set("sourcefile", fileName);
    }

    return gco;
}

}  // namespace griddyn

