#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <map>
#include <memory>
#include <mutex>
#include <string>

class fmiLibrary;
class fmi2ModelExchangeObject;
class fmi2CoSimObject;

/** singleton class for managing fmi library objects*/
class fmiLibraryManager {
  private:
    std::map<std::string, std::shared_ptr<fmiLibrary>> libraries;
    std::map<std::string, std::string> quickReferenceLibraries;
    mutable std::mutex libraryLock;

  public:
    ~fmiLibraryManager();
    std::shared_ptr<fmiLibrary> getLibrary(const std::string& libFile);
    std::unique_ptr<fmi2ModelExchangeObject>
        createModelExchangeObject(const std::string& fmuIdentifier, const std::string& ObjectName);
    std::unique_ptr<fmi2CoSimObject> createCoSimulationObject(const std::string& fmuIdentifier,
                                                              const std::string& ObjectName);
    void loadBookMarkFile(const std::string& bookmarksFile);
    void addShortCut(const std::string& name, const std::string& fmuLocation);
    static fmiLibraryManager& instance();

  private:
    fmiLibraryManager();
};
