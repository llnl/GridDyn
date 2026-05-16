/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiLibraryManager.h"

#include "fmiImport.h"
#include "fmiObjects.h"
#include <memory>
#include <string>

std::shared_ptr<FmiLibrary> FmiLibraryManager::getLibrary(const std::string& libFile)
{
    std::unique_lock<std::mutex> lock(libraryLock);
    auto fnd = quickReferenceLibraries.find(libFile);
    std::string fmilib;
    if (fnd != quickReferenceLibraries.end()) {
        fmilib = fnd->second;
    } else {
        fmilib = libFile;
    }
    auto fndLib = libraries.find(fmilib);
    if (fndLib != libraries.end()) {
        return fndLib->second;
    }
    lock.unlock();
    // this can be a big operation so free the lock while it is occurring
    auto newLib = std::make_shared<FmiLibrary>(libFile);
    lock.lock();
    libraries.emplace(fmilib, newLib);
    return newLib;
}

std::unique_ptr<Fmi2ModelExchangeObject>
    FmiLibraryManager::createModelExchangeObject(const std::string& fmuIdentifier,
                                                 const std::string& ObjectName)
{
    auto Lib = getLibrary(fmuIdentifier);
    return Lib->createModelExchangeObject(ObjectName);
}

std::unique_ptr<Fmi2CoSimObject>
    FmiLibraryManager::createCoSimulationObject(const std::string& fmuIdentifier,
                                                const std::string& ObjectName)
{
    auto Lib = getLibrary(fmuIdentifier);
    return Lib->createCoSimulationObject(ObjectName);
}

void FmiLibraryManager::loadBookMarkFile(const std::string& /*bookmarksFile*/)
{
    // TODO(phlpt): Load a bookmarks file.
}

void FmiLibraryManager::addShortCut(const std::string& name, const std::string& fmuLocation)
{
    const std::scoped_lock lock(libraryLock);
    quickReferenceLibraries.emplace(name, fmuLocation);
}

FmiLibraryManager& FmiLibraryManager::instance()
{
    static FmiLibraryManager s_instance;
    return s_instance;
}

FmiLibraryManager::FmiLibraryManager() = default;

FmiLibraryManager::~FmiLibraryManager() = default;
