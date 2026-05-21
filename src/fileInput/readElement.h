/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers

#include "fileInput.h"
#include "readerHelper.h"
#include <memory>
#include <string>
#include <unordered_set>

// forward declarations
// NOLINTNEXTLINE(readability-identifier-naming)
class readerElement;

namespace griddyn {
class GridParameter;
class HelperObject;

// struct for holding and passing the information in Element reader files
class ReaderInfo;

class CoreObject;

class ZipLoad;
class Generator;
class GridArea;
class Link;
class EventInfo;
class GridSimulation;
class GridDynSimulation;
class Relay;
class GridBus;
// NOLINTNEXTLINE(readability-identifier-naming)
class gridPrimary;
// NOLINTNEXTLINE(readability-identifier-naming)
class gridSecondary;
class GridSubModel;

GridBus* readBusElement(std::shared_ptr<readerElement>& element,
                        ReaderInfo& readerInformation,
                        CoreObject* searchObject = nullptr);
Relay* readRelayElement(std::shared_ptr<readerElement>& element,
                        ReaderInfo& readerInformation,
                        CoreObject* searchObject = nullptr);

// ZipLoad * readLoadElement (std::shared_ptr<readerElement> &element, readerInfo &ri, CoreObject
// *searchObject = nullptr); Generator * readGeneratorElement (std::shared_ptr<readerElement>
// &element, readerInfo *ri, CoreObject *searchObject = nullptr);
Link* readLinkElement(std::shared_ptr<readerElement>& element,
                      ReaderInfo& readerInformation,
                      CoreObject* searchObject = nullptr,
                      bool warnlink = true);
GridArea* readGridAreaElement(std::shared_ptr<readerElement>& element,
                              ReaderInfo& readerInformation,
                              CoreObject* searchObject = nullptr);
GridSimulation* readSimulationElement(std::shared_ptr<readerElement>& element,
                                      ReaderInfo& readerInformation,
                                      CoreObject* searchObject = nullptr,
                                      GridSimulation* simulationObject = nullptr);

CoreObject* readEconElement(std::shared_ptr<readerElement>& element,
                            ReaderInfo& readerInformation,
                            CoreObject* searchObject = nullptr);
void readArrayElement(std::shared_ptr<readerElement>& element,
                      ReaderInfo& readerInformation,
                      CoreObject* parentObject);
void loadConditionElement(std::shared_ptr<readerElement>& element,
                          ReaderInfo& readerInformation,
                          CoreObject* parentObject);
void loadSubObjects(std::shared_ptr<readerElement>& element,
                    ReaderInfo& readerInformation,
                    CoreObject* parentObject);

void readImports(std::shared_ptr<readerElement>& element,
                 ReaderInfo& readerInformation,
                 CoreObject* parentObject,
                 bool finalFlag);

void loadDefines(std::shared_ptr<readerElement>& element,
                 ReaderInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp
void loadDirectories(std::shared_ptr<readerElement>& element,
                     ReaderInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp
void loadTranslations(std::shared_ptr<readerElement>& element,
                      ReaderInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp
void loadCustomSections(std::shared_ptr<readerElement>& element,
                        ReaderInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp

void loadSolverElement(std::shared_ptr<readerElement>& element,
                       ReaderInfo& readerInformation,
                       GridDynSimulation* parentObject);
void readLibraryElement(std::shared_ptr<readerElement>& element, ReaderInfo& readerInformation);

using IgnoreListType = std::unordered_set<std::string>;
// using IgnoreListType = boost::container::flat_set<std::string>;

void loadElementInformation(CoreObject* obj,
                            std::shared_ptr<readerElement>& element,
                            const std::string& objectName,
                            ReaderInfo& readerInfoRef,
                            const IgnoreListType& ignoreList);

void objSetAttributes(CoreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      ReaderInfo& readerInfoRef,
                      const IgnoreListType& ignoreList);

void paramLoopElement(CoreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      ReaderInfo& readerInfoRef,
                      const IgnoreListType& ignoreList);

void setParams(HelperObject* obj,
               std::shared_ptr<readerElement>& element,
               const std::string& component,
               ReaderInfo& readerInfoRef,
               const IgnoreListType& ignoreList);
void setAttributes(HelperObject* obj,
                   std::shared_ptr<readerElement>& element,
                   const std::string& component,
                   ReaderInfo& readerInfoRef,
                   const IgnoreListType& ignoreList);

int loadEventElement(std::shared_ptr<readerElement>& element,
                     CoreObject* obj,
                     ReaderInfo& readerInformation);
int loadCollectorElement(std::shared_ptr<readerElement>& element,
                         CoreObject* obj,
                         ReaderInfo& readerInformation);

GridParameter getElementParam(const std::shared_ptr<readerElement>& element);
void getElementParam(const std::shared_ptr<readerElement>& element, GridParameter& param);

std::string
    findElementName(std::shared_ptr<readerElement>& element,
                    const std::string& ename,
                    readerConfig::MatchType matching = readerConfig::MatchType::STRICT_CASE_MATCH);

std::string
    getElementField(std::shared_ptr<readerElement>& element,
                    const std::string& ename,
                    readerConfig::MatchType matching = readerConfig::MatchType::STRICT_CASE_MATCH);
std::string getElementAttribute(
    std::shared_ptr<readerElement>& element,
    const std::string& ename,
    readerConfig::MatchType matching = readerConfig::MatchType::STRICT_CASE_MATCH);
std::string getElementFieldOptions(
    std::shared_ptr<readerElement>& element,
    const stringVec& names,
    readerConfig::MatchType matching = readerConfig::MatchType::STRICT_CASE_MATCH);
stringVec getElementFieldMultiple(
    std::shared_ptr<readerElement>& element,
    const std::string& ename,
    readerConfig::MatchType matching = readerConfig::MatchType::STRICT_CASE_MATCH);

void setIndex(std::shared_ptr<readerElement>& element,
              CoreObject* mainObject,
              ReaderInfo& readerInformation);
std::string getObjectName(std::shared_ptr<readerElement>& element, ReaderInfo& readerInformation);

CoreObject* getParent(std::shared_ptr<readerElement>& element,
                      ReaderInfo& readerInformation,
                      CoreObject* parentObject,
                      const std::string& alternateName = "");

// This set of constants and functions is to allow templating of the object type but getting an
// alternative string for the parent type
static const char emptyString[] = "";
static const char areaTypeString[] = "area";
static const char busTypeString[] = "bus";

inline const std::string& parentSearchComponent(CoreObject*)
{
    static const std::string emptyStringRef{emptyString};
    return emptyStringRef;
}

inline const std::string& parentSearchComponent(gridPrimary*)
{
    static const std::string areaTypeStringRef{areaTypeString};
    return areaTypeStringRef;
}

inline const std::string& parentSearchComponent(gridSecondary*)
{
    static const std::string busTypeStringRef{busTypeString};
    return busTypeStringRef;
}
}  // namespace griddyn
