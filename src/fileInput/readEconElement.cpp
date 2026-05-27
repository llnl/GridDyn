/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/ObjectInterpreter.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "readElement.h"
#include "readerHelper.h"

#ifdef ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#    include "optimization/gridOptObjects.h"
#    include "optimization/models/gridAreaOpt.h"
#    include "optimization/optObjectFactory.h"
#endif

#include "gmlc/utilities/stringOps.h"
#include <string>

namespace griddyn {

#ifndef ENABLE_OPTIMIZATION_LIBRARY
CoreObject* readEconElement(std::shared_ptr<ReaderElement>& /*element*/,
                            ReaderInfo& /*ri*/,
                            CoreObject* searchObject)
{
    return searchObject;
}

#else
namespace {
    const IgnoreListType& econIgnoreElements()
    {
        static const auto* ignoreElements =
            new IgnoreListType{"mode", "objecttype", "retype", "parent"};
        return *ignoreElements;
    }
}  // namespace
CoreObject* readEconElement(std::shared_ptr<ReaderElement>& element,
                            ReaderInfo& ReaderInformation,
                            CoreObject* searchObject)
{
    // get the optimization root
    auto optimizationRoot = dynamic_cast<gridDynOptimization*>(searchObject->getRoot());
    if (optimizationRoot ==
        nullptr)  // there is no optimization engine defined so ignore the economic data
    {
        return nullptr;
    }
    gridOptObject* optObject = nullptr;
    std::string targetName;
    auto riScope = ReaderInformation.newScope();
    // run the boilerplate code to setup the object
    // lnk = XMLReaderSetup(aP, lnk, "econ", ri, searchObject);
    std::string objectType;
    std::string ename;
    auto optObjectFactory = coreOptObjectFactory::instance();
    CoreObject* obj;
    CoreObject* targetObject = nullptr;
    gridOptObject* parentOptObject = nullptr;

    loadDefines(element, ReaderInformation);
    loadDirectories(element, ReaderInformation);
    if (searchObject != nullptr) {
        targetName =
            getElementFieldOptions(element, {"target", "source"}, readerConfig::defMatchType);
        if (targetName.empty()) {
            targetObject = searchObject;
        } else {
            targetName = ReaderInformation.checkDefines(targetName);
            targetObject = locateObject(targetName, searchObject);
        }
        optObject = optimizationRoot->getOptimizationObject(targetObject);
        if (optObject != nullptr)  // check for retyping
        {
            // if we need to do a type override
            auto optMode = getElementField(element, "retype", readerConfig::defMatchType);
            if (!optMode.empty()) {
                optMode = ReaderInformation.checkDefines(optMode);
                makeLowerCase(optMode);
                gridOptObject* retypedObject =
                    optObjectFactory->createObject(optMode, targetObject);
                if (retypedObject == nullptr) {
                    WARNPRINT(READER_WARN_IMPORTANT, "unknown economic retype " << optMode);
                } else {
                    // TODO(phlpt): This isn't quite right yet.
                    optObject->clone(retypedObject);
                    optObject->getParent()->remove(optObject);
                    delete optObject;
                    searchObject->add(retypedObject);
                    optObject = retypedObject;
                }
            }
        }
    } else {
        std::string objecttype =
            getElementFieldOptions(element, {"objecttype", "type"}, readerConfig::defMatchType);
        if (!objecttype.empty()) {
            objectType = ReaderInformation.checkDefines(objecttype);
            makeLowerCase(objectType);
        } else {
            WARNPRINT(READER_WARN_IMPORTANT, "economic object type must be specified ");
            return nullptr;
        }
        std::string mode = getElementField(element, "mode", readerConfig::defMatchType);
        if (mode.empty()) {
            optObject = optObjectFactory->createObject(objectType);
        } else {
            mode = ReaderInformation.checkDefines(mode);
            makeLowerCase(mode);
            optObject = optObjectFactory->createObject(mode, objectType);
            if (optObject == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown economic mode " << mode);
            }
        }
    }

    if (optObject == nullptr) {
        std::string optMode =
            getElementFieldOptions(element, {"mode", "retype"}, readerConfig::defMatchType);
        if (!optMode.empty()) {
            optMode = ReaderInformation.checkDefines(optMode);
            makeLowerCase(optMode);
            optObject = optObjectFactory->createObject(optMode, targetObject);
            if (optObject == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown economic mode " << optMode);
            }
        }
        std::string refName = getElementField(element, "ref", readerConfig::defMatchType);
        if (!refName.empty()) {
            ename = ReaderInformation.checkDefines(refName);
            obj = ReaderInformation.makeLibraryObject(ename, optObject);
            if (obj == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unable to locate reference object " << ename << " in library");
            } else {
                optObject = dynamic_cast<gridOptObject*>(obj);
                if (optObject == nullptr) {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Invalid reference object " << ename << ": wrong type");
                    delete obj;
                }
            }
        }
    }

    // check for library references

    if (optObject == nullptr) {
        optObject = optObjectFactory->createObject(targetObject);
        if (optObject == nullptr) {
            WARNPRINT(READER_WARN_IMPORTANT, "Unable to create object ");
            return nullptr;
        }
    }
    ename = getElementField(element, "name", readerConfig::defMatchType);
    if (!ename.empty()) {
        ename = ReaderInformation.checkDefines(ename);
        if (ReaderInformation.prefix.empty()) {
            optObject->setName(ename);
        } else {
            optObject->setName(ReaderInformation.prefix + '_' + ename);
        }
    }
    // locate a parent if any
    ename = getElementField(element, "parent", readerConfig::defMatchType);
    if (!ename.empty()) {
        ename = ReaderInformation.checkDefines(ename);
        if (optObject->isRoot()) {
            obj = locateObject(ename, searchObject);

            if (obj != nullptr) {
                if (dynamic_cast<gridOptObject*>(obj) != nullptr) {
                    parentOptObject = static_cast<gridOptObject*>(obj);
                } else {
                    parentOptObject = optimizationRoot->getOptimizationObject(obj);
                }
                if (parentOptObject == nullptr) {
                    parentOptObject = optimizationRoot->makeOptimizationObjectPath(obj);
                }
                addToParent(optObject, parentOptObject);
            }
        } else {
            WARNPRINT(READER_WARN_IMPORTANT,
                      "Parent " << ename << "specified for " << optObject->getName()
                                << " even though it already has a parent");
        }
    } else if (optObject->isRoot()) {
        if ((targetObject != nullptr) && (targetObject->getParent() != nullptr)) {
            parentOptObject =
                optimizationRoot->makeOptimizationObjectPath(targetObject->getParent());
            addToParent(optObject, parentOptObject);
        } else {
            // set the base power to the system default (usually used for library objects
            optObject->set("basepower", ReaderInformation.base);
        }
    }

    // properties from link attributes

    objSetAttributes(optObject, element, "econ", ReaderInformation, econIgnoreElements());
    loadSubObjects(element, ReaderInformation, optObject);

    // get all element fields
    paramLoopElement(optObject, element, "econ", ReaderInformation, econIgnoreElements());

    LEVELPRINT(READER_NORMAL_PRINT, "loaded econ data " << optObject->getName());

    ReaderInformation.closeScope(riScope);
    return optObject;
}
#endif
}  // namespace griddyn
