/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/measurement/collector.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
using readerConfig::defMatchType;

void loadDefaultObjectTranslations(readerInfo& ri);

static const IgnoreListType simIgnoreFields{"version", "basepower"};

bool isMasterObject(const coreObject* searchObject, const gridSimulation* gs);

static const char libstring[] = "library";
// read XML file
// coreObject * readSimXMLFile(const std::string &fileName, coreObject *gco, const std::string
// prefix, readerInfo *ri) const
gridSimulation* readSimulationElement(std::shared_ptr<readerElement>& element,
                                      readerInfo& ri,
                                      coreObject* searchObject,
                                      gridSimulation* gs)
{
    // pointers
    bool masterObject = isMasterObject(searchObject, gs);

    auto riScope = ri.newScope();

    loadDefines(element, ri);
    loadDirectories(element, ri);
    if (masterObject) {
        loadDefaultObjectTranslations(ri);
    }
    loadTranslations(element, ri);
    loadCustomSections(element, ri);
    gridSimulation* simulation = ElementReaderSetup(element, gs, "simulation", ri, searchObject);

    // load the simulation name and id
    std::string ename = getElementField(element, "name", defMatchType);
    if (!ename.empty()) {
        simulation->setName(ename);
    }
    // load the file version
    std::string vers = getElementField(element, "version", defMatchType);
    if (!vers.empty()) {
        simulation->set("version", vers);
    }
    setIndex(element, simulation, ri);
    // load any other attributes
    objSetAttributes(simulation, element, simulation->getName(), ri, simIgnoreFields);

    if (masterObject) {
        std::string pname = getElementField(element, "basepower", defMatchType);
        if (!pname.empty()) {
            double val = interpretString(pname, ri);
            ri.base = val;
            simulation->set("basepower", val);
        }
    }
    // load the libraries
    if (element->hasElement(libstring)) {
        element->moveToFirstChild(libstring);
        while (element->isValid()) {
            readLibraryElement(element, ri);
            element->moveToNextSibling(libstring);
        }
        element->moveToParent();
    }

    readImports(element, ri, simulation, false);

    // load all other objects besides bus and area
    loadSubObjects(element, ri, simulation);

    paramLoopElement(simulation, element, simulation->getName(), ri, simIgnoreFields);

    // read imports marked final
    readImports(element, ri, simulation, true);

    element->moveToFirstChild("solver");
    while (element->isValid()) {
        loadSolverElement(element, ri, dynamic_cast<gridDynSimulation*>(simulation));
        element->moveToNextSibling("solver");
    }
    element->moveToParent();

    if (masterObject) {
        int busCount = simulation->getInt("totalbuscount");
        int linkCount = simulation->getInt("totallinkcount");

        LEVELPRINT(READER_NORMAL_PRINT, "loaded Power simulation " << simulation->getName());
        LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << busCount << " buses Loaded ");
        LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << linkCount << " links Loaded ");
        if (!ri.collectors.empty()) {
            LEVELPRINT(READER_SUMMARY_PRINT,
                       "Summary: " << ri.collectors.size() << " collectors Loaded ");
        }
        if (!ri.events.empty()) {
            LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << ri.events.size() << " events Loaded ");
        }
        for (auto& col : ri.collectors) {
            auto owner = col->getOwner();
            if (owner != nullptr) {
                try {
                    owner->addHelper(col);
                }
                catch (const objectAddFailure&) {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Collector: " << col->getName() << " unable to be added to "
                                            << owner->getName());
                    simulation->add(col);
                }
            } else {
                simulation->add(col);
            }
        }
        // add the events
        simulation->add(ri.events);
    }

    ri.closeScope(riScope);

    return simulation;
}

void loadDefaultObjectTranslations(readerInfo& ri)
{
    ri.addTranslate("fuse", "relay");
    ri.addTranslate("breaker", "relay");
    ri.addTranslate("sensor", "relay");
    ri.addTranslate("control", "relay");
    ri.addTranslate("pmu", "relay");
    ri.addTranslate("controlblock", "block");
    ri.addTranslate("model", "genmodel");
    ri.addTranslate("gen", "generator");
    ri.addTranslate("transformer", "link");
    ri.addTranslate("line", "link");
    ri.addTranslate("tie", "link");
    ri.addTranslate("subsystem", "link");
    ri.addTranslate("busmodify", "bus");
    ri.addTranslate("areamodify", "area");
    ri.addTranslate("linkmodify", "link");
    ri.addTranslate("gov", "governor");
    ri.addTranslate("recorder", "collector");
    ri.addTranslate("player", "event");
    ri.addTranslate("scenario","event");
    ri.addTranslate("loop", "array");
}

bool isMasterObject(const coreObject* searchObject, const gridSimulation* gs)
{
    if (searchObject != nullptr) {
        return (gs != nullptr) ? (isSameObject(searchObject, gs)) : false;
    }
    return (gs != nullptr) ? (gs->isRoot()) : true;
    // return true if both are null since any new object would then be master
}

}  // namespace griddyn
