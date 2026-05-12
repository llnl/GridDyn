/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
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

namespace {
    void loadDefaultObjectTranslations(readerInfo& readerInformation);

    const IgnoreListType& simIgnoreFields()
    {
        static const auto* ignoreFields = new IgnoreListType{"version", "basepower"};
        return *ignoreFields;
    }

    bool isMasterObject(const coreObject* searchObject, const gridSimulation* simulationObject);
}  // namespace

static const char libstring[] = "library";
// read XML file
// coreObject * readSimXMLFile(const std::string &fileName, coreObject *gco, const std::string
// prefix, readerInfo *ri) const
gridSimulation* readSimulationElement(std::shared_ptr<readerElement>& element,
                                      readerInfo& readerInformation,
                                      coreObject* searchObject,
                                      gridSimulation* simulationObject)
{
    // pointers
    const bool isMaster = isMasterObject(searchObject, simulationObject);

    auto riScope = readerInformation.newScope();

    loadDefines(element, readerInformation);
    loadDirectories(element, readerInformation);
    if (isMaster) {
        loadDefaultObjectTranslations(readerInformation);
    }
    loadTranslations(element, readerInformation);
    loadCustomSections(element, readerInformation);
    gridSimulation* simulation = ElementReaderSetup(
        element, simulationObject, "simulation", readerInformation, searchObject);

    // load the simulation name and id
    const std::string simulationName = getElementField(element, "name", defMatchType);
    if (!simulationName.empty()) {
        simulation->setName(simulationName);
    }
    // load the file version
    const std::string versionString = getElementField(element, "version", defMatchType);
    if (!versionString.empty()) {
        simulation->set("version", versionString);
    }
    setIndex(element, simulation, readerInformation);
    // load any other attributes
    objSetAttributes(
        simulation, element, simulation->getName(), readerInformation, simIgnoreFields());

    if (isMaster) {
        const std::string basePowerText = getElementField(element, "basepower", defMatchType);
        if (!basePowerText.empty()) {
            const double basePowerValue = interpretString(basePowerText, readerInformation);
            readerInformation.base = basePowerValue;
            simulation->set("basepower", basePowerValue);
        }
    }
    // load the libraries
    if (element->hasElement(libstring)) {
        element->moveToFirstChild(libstring);
        while (element->isValid()) {
            readLibraryElement(element, readerInformation);
            element->moveToNextSibling(libstring);
        }
        element->moveToParent();
    }

    readImports(element, readerInformation, simulation, false);

    // load all other objects besides bus and area
    loadSubObjects(element, readerInformation, simulation);

    paramLoopElement(
        simulation, element, simulation->getName(), readerInformation, simIgnoreFields());

    // read imports marked final
    readImports(element, readerInformation, simulation, true);

    element->moveToFirstChild("solver");
    while (element->isValid()) {
        loadSolverElement(element, readerInformation, dynamic_cast<gridDynSimulation*>(simulation));
        element->moveToNextSibling("solver");
    }
    element->moveToParent();

    if (isMaster) {
        const int busCount = simulation->getInt("totalbuscount");
        const int linkCount = simulation->getInt("totallinkcount");

        LEVELPRINT(READER_NORMAL_PRINT, "loaded Power simulation " << simulation->getName());
        LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << busCount << " buses Loaded ");
        LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << linkCount << " links Loaded ");
        if (!readerInformation.collectors.empty()) {
            LEVELPRINT(READER_SUMMARY_PRINT,
                       "Summary: " << readerInformation.collectors.size() << " collectors Loaded ");
        }
        if (!readerInformation.events.empty()) {
            LEVELPRINT(READER_SUMMARY_PRINT,
                       "Summary: " << readerInformation.events.size() << " events Loaded ");
        }
        for (auto& col : readerInformation.collectors) {
            auto* owner = col->getOwner();
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
        simulation->add(readerInformation.events);
    }

    readerInformation.closeScope(riScope);

    return simulation;
}

namespace {
    void loadDefaultObjectTranslations(readerInfo& readerInformation)
    {
        readerInformation.addTranslate("fuse", "relay");
        readerInformation.addTranslate("breaker", "relay");
        readerInformation.addTranslate("sensor", "relay");
        readerInformation.addTranslate("control", "relay");
        readerInformation.addTranslate("pmu", "relay");
        readerInformation.addTranslate("controlblock", "block");
        readerInformation.addTranslate("model", "genmodel");
        readerInformation.addTranslate("gen", "generator");
        readerInformation.addTranslate("transformer", "link");
        readerInformation.addTranslate("line", "link");
        readerInformation.addTranslate("tie", "link");
        readerInformation.addTranslate("subsystem", "link");
        readerInformation.addTranslate("busmodify", "bus");
        readerInformation.addTranslate("areamodify", "area");
        readerInformation.addTranslate("linkmodify", "link");
        readerInformation.addTranslate("gov", "governor");
        readerInformation.addTranslate("recorder", "collector");
        readerInformation.addTranslate("player", "event");
        readerInformation.addTranslate("scenario", "event");
        readerInformation.addTranslate("loop", "array");
    }

    bool isMasterObject(const coreObject* searchObject, const gridSimulation* simulationObject)
    {
        if (searchObject != nullptr) {
            return (simulationObject != nullptr) ? (isSameObject(searchObject, simulationObject)) :
                                                   false;
        }
        return (simulationObject != nullptr) ? (simulationObject->isRoot()) : true;
        // return true if both are null since any new object would then be master
    }
}  // namespace

}  // namespace griddyn
