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
class readerElement;

namespace griddyn {
class gridParameter;
class helperObject;

// struct for holding and passing the information in Element reader files
class readerInfo;

class coreObject;

class zipLoad;
class Generator;
class Area;
class Link;
class EventInfo;
class gridSimulation;
class gridDynSimulation;
class Relay;
class gridBus;
class gridPrimary;
class gridSecondary;
class gridSubModel;

gridBus* readBusElement(std::shared_ptr<readerElement>& element,
                        readerInfo& readerInformation,
                        coreObject* searchObject = nullptr);
Relay* readRelayElement(std::shared_ptr<readerElement>& element,
                        readerInfo& readerInformation,
                        coreObject* searchObject = nullptr);

// zipLoad * readLoadElement (std::shared_ptr<readerElement> &element, readerInfo &ri, coreObject
// *searchObject = nullptr); Generator * readGeneratorElement (std::shared_ptr<readerElement>
// &element, readerInfo *ri, coreObject *searchObject = nullptr);
Link* readLinkElement(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation,
                      coreObject* searchObject = nullptr,
                      bool warnlink = true);
Area* readAreaElement(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation,
                      coreObject* searchObject = nullptr);
gridSimulation* readSimulationElement(std::shared_ptr<readerElement>& element,
                                      readerInfo& readerInformation,
                                      coreObject* searchObject = nullptr,
                                      gridSimulation* simulationObject = nullptr);

coreObject* readEconElement(std::shared_ptr<readerElement>& element,
                            readerInfo& readerInformation,
                            coreObject* searchObject = nullptr);
void readArrayElement(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation,
                      coreObject* parentObject);
void loadConditionElement(std::shared_ptr<readerElement>& element,
                          readerInfo& readerInformation,
                          coreObject* parentObject);
void loadSubObjects(std::shared_ptr<readerElement>& element,
                    readerInfo& readerInformation,
                    coreObject* parentObject);

void readImports(std::shared_ptr<readerElement>& element,
                 readerInfo& readerInformation,
                 coreObject* parentObject,
                 bool finalFlag);

void loadDefines(std::shared_ptr<readerElement>& element,
                 readerInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp
void loadDirectories(std::shared_ptr<readerElement>& element,
                     readerInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp
void loadTranslations(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp
void loadCustomSections(std::shared_ptr<readerElement>& element,
                        readerInfo& readerInformation);  // NOTE: defined in readLibraryElement.cpp

void loadSolverElement(std::shared_ptr<readerElement>& element,
                       readerInfo& readerInformation,
                       gridDynSimulation* parentObject);
void readLibraryElement(std::shared_ptr<readerElement>& element, readerInfo& readerInformation);

using IgnoreListType = std::unordered_set<std::string>;
// using IgnoreListType = boost::container::flat_set<std::string>;

void loadElementInformation(coreObject* obj,
                            std::shared_ptr<readerElement>& element,
                            const std::string& objectName,
                            readerInfo& readerInfoRef,
                            const IgnoreListType& ignoreList);

void objSetAttributes(coreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      readerInfo& readerInfoRef,
                      const IgnoreListType& ignoreList);

void paramLoopElement(coreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      readerInfo& readerInfoRef,
                      const IgnoreListType& ignoreList);

void setParams(helperObject* obj,
               std::shared_ptr<readerElement>& element,
               const std::string& component,
               readerInfo& readerInfoRef,
               const IgnoreListType& ignoreList);
void setAttributes(helperObject* obj,
                   std::shared_ptr<readerElement>& element,
                   const std::string& component,
                   readerInfo& readerInfoRef,
                   const IgnoreListType& ignoreList);

int loadEventElement(std::shared_ptr<readerElement>& element,
                     coreObject* obj,
                     readerInfo& readerInformation);
int loadCollectorElement(std::shared_ptr<readerElement>& element,
                         coreObject* obj,
                         readerInfo& readerInformation);

gridParameter getElementParam(const std::shared_ptr<readerElement>& element);
void getElementParam(const std::shared_ptr<readerElement>& element, gridParameter& param);

std::string findElementName(
    std::shared_ptr<readerElement>& element,
    const std::string& ename,
    readerConfig::match_type matching = readerConfig::match_type::strict_case_match);

std::string getElementField(
    std::shared_ptr<readerElement>& element,
    const std::string& ename,
    readerConfig::match_type matching = readerConfig::match_type::strict_case_match);
std::string getElementAttribute(
    std::shared_ptr<readerElement>& element,
    const std::string& ename,
    readerConfig::match_type matching = readerConfig::match_type::strict_case_match);
std::string getElementFieldOptions(
    std::shared_ptr<readerElement>& element,
    const stringVec& names,
    readerConfig::match_type matching = readerConfig::match_type::strict_case_match);
stringVec getElementFieldMultiple(
    std::shared_ptr<readerElement>& element,
    const std::string& ename,
    readerConfig::match_type matching = readerConfig::match_type::strict_case_match);

void setIndex(std::shared_ptr<readerElement>& element,
              coreObject* mainObject,
              readerInfo& readerInformation);
std::string getObjectName(std::shared_ptr<readerElement>& element, readerInfo& readerInformation);

coreObject* getParent(std::shared_ptr<readerElement>& element,
                      readerInfo& readerInformation,
                      coreObject* parentObject,
                      const std::string& alternateName = "");

// This set of constants and functions is to allow templating of the object type but getting an
// alternative string for the parent type
static const char emptyString[] = "";
static const char areaTypeString[] = "area";
static const char busTypeString[] = "bus";

inline const std::string& parentSearchComponent(coreObject*)
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
