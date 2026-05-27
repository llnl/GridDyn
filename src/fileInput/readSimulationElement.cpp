/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/measurement/Collector.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
using readerConfig::defMatchType;

namespace {
    void loadDefaultObjectTranslations(ReaderInfo& ReaderInformation);

    const IgnoreListType& simIgnoreFields()
    {
        static const auto* ignoreFields = new IgnoreListType{"version", "basepower"};
        return *ignoreFields;
    }

    bool isMasterObject(const CoreObject* searchObject, const GridSimulation* simulationObject);
}  // namespace

static constexpr char libstring[] = "library";
// read XML file
// CoreObject * readSimXMLFile(const std::string &fileName, CoreObject *gco, const std::string
// prefix, ReaderInfo *ri) const
GridSimulation* readSimulationElement(std::shared_ptr<ReaderElement>& element,
                                      ReaderInfo& ReaderInformation,
                                      CoreObject* searchObject,
                                      GridSimulation* simulationObject)
{
    // pointers
    const bool isMaster = isMasterObject(searchObject, simulationObject);

    auto riScope = ReaderInformation.newScope();

    loadDefines(element, ReaderInformation);
    loadDirectories(element, ReaderInformation);
    if (isMaster) {
        loadDefaultObjectTranslations(ReaderInformation);
    }
    loadTranslations(element, ReaderInformation);
    loadCustomSections(element, ReaderInformation);
    GridSimulation* simulation = elementReaderSetup(
        element, simulationObject, "simulation", ReaderInformation, searchObject);

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
    setIndex(element, simulation, ReaderInformation);
    // load any other attributes
    objSetAttributes(
        simulation, element, simulation->getName(), ReaderInformation, simIgnoreFields());

    if (isMaster) {
        const std::string basePowerText = getElementField(element, "basepower", defMatchType);
        if (!basePowerText.empty()) {
            const double basePowerValue = interpretString(basePowerText, ReaderInformation);
            ReaderInformation.base = basePowerValue;
            simulation->set("basepower", basePowerValue);
        }
    }
    // load the libraries
    if (element->hasElement(libstring)) {
        element->moveToFirstChild(libstring);
        while (element->isValid()) {
            readLibraryElement(element, ReaderInformation);
            element->moveToNextSibling(libstring);
        }
        element->moveToParent();
    }

    readImports(element, ReaderInformation, simulation, false);

    // load all other objects besides bus and area
    loadSubObjects(element, ReaderInformation, simulation);

    paramLoopElement(
        simulation, element, simulation->getName(), ReaderInformation, simIgnoreFields());

    // read imports marked final
    readImports(element, ReaderInformation, simulation, true);

    element->moveToFirstChild("solver");
    while (element->isValid()) {
        loadSolverElement(element, ReaderInformation, dynamic_cast<GridDynSimulation*>(simulation));
        element->moveToNextSibling("solver");
    }
    element->moveToParent();

    if (isMaster) {
        const int busCount = simulation->getInt("totalbuscount");
        const int linkCount = simulation->getInt("totallinkcount");

        LEVELPRINT(READER_NORMAL_PRINT, "loaded Power simulation " << simulation->getName());
        LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << busCount << " buses Loaded ");
        LEVELPRINT(READER_SUMMARY_PRINT, "Summary: " << linkCount << " links Loaded ");
        if (!ReaderInformation.collectors.empty()) {
            LEVELPRINT(READER_SUMMARY_PRINT,
                       "Summary: " << ReaderInformation.collectors.size() << " collectors Loaded ");
        }
        if (!ReaderInformation.events.empty()) {
            LEVELPRINT(READER_SUMMARY_PRINT,
                       "Summary: " << ReaderInformation.events.size() << " events Loaded ");
        }
        for (auto& col : ReaderInformation.collectors) {
            auto* owner = col->getOwner();
            if (owner != nullptr) {
                try {
                    owner->addHelper(col);
                }
                catch (const ObjectAddFailure&) {
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
        simulation->add(ReaderInformation.events);
    }

    ReaderInformation.closeScope(riScope);

    return simulation;
}

namespace {
    void loadDefaultObjectTranslations(ReaderInfo& ReaderInformation)
    {
        ReaderInformation.addTranslate("fuse", "relay");
        ReaderInformation.addTranslate("breaker", "relay");
        ReaderInformation.addTranslate("sensor", "relay");
        ReaderInformation.addTranslate("control", "relay");
        ReaderInformation.addTranslate("pmu", "relay");
        ReaderInformation.addTranslate("controlblock", "block");
        ReaderInformation.addTranslate("model", "genmodel");
        ReaderInformation.addTranslate("gen", "generator");
        ReaderInformation.addTranslate("transformer", "link");
        ReaderInformation.addTranslate("line", "link");
        ReaderInformation.addTranslate("tie", "link");
        ReaderInformation.addTranslate("subsystem", "link");
        ReaderInformation.addTranslate("busmodify", "bus");
        ReaderInformation.addTranslate("areamodify", "area");
        ReaderInformation.addTranslate("linkmodify", "link");
        ReaderInformation.addTranslate("gov", "governor");
        ReaderInformation.addTranslate("recorder", "collector");
        ReaderInformation.addTranslate("player", "event");
        ReaderInformation.addTranslate("scenario", "event");
        ReaderInformation.addTranslate("loop", "array");
    }

    bool isMasterObject(const CoreObject* searchObject, const GridSimulation* simulationObject)
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
