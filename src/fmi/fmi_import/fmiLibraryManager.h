/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

class FmiLibrary;
class Fmi2ModelExchangeObject;
class Fmi2CoSimObject;

/** singleton class for managing fmi library objects*/
class FmiLibraryManager {
  private:
    std::map<std::string, std::shared_ptr<FmiLibrary>> libraries;
    std::map<std::string, std::string> quickReferenceLibraries;
    mutable std::mutex libraryLock;

  public:
    ~FmiLibraryManager();
    std::shared_ptr<FmiLibrary> getLibrary(const std::string& libFile);
    std::unique_ptr<Fmi2ModelExchangeObject>
        createModelExchangeObject(const std::string& fmuIdentifier, const std::string& ObjectName);
    std::unique_ptr<Fmi2CoSimObject> createCoSimulationObject(const std::string& fmuIdentifier,
                                                              const std::string& ObjectName);
    void loadBookMarkFile(const std::string& bookmarksFile);
    void addShortCut(const std::string& name, const std::string& fmuLocation);
    static FmiLibraryManager& instance();

  private:
    FmiLibraryManager();
};
