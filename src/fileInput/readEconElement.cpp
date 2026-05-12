/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/objectInterpreter.h"
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
coreObject* readEconElement(std::shared_ptr<readerElement>& /*element*/,
                            readerInfo& /*ri*/,
                            coreObject* searchObject)
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
}
coreObject* readEconElement(std::shared_ptr<readerElement>& element,
                            readerInfo& readerInformation,
                            coreObject* searchObject)
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
    auto riScope = readerInformation.newScope();
    // run the boilerplate code to setup the object
    // lnk = XMLReaderSetup(aP, lnk, "econ", ri, searchObject);
    std::string objectType;
    std::string ename;
    auto optObjectFactory = coreOptObjectFactory::instance();
    coreObject* obj;
    coreObject* targetObject = nullptr;
    gridOptObject* parentOptObject = nullptr;

    loadDefines(element, readerInformation);
    loadDirectories(element, readerInformation);
    if (searchObject != nullptr) {
        targetName =
            getElementFieldOptions(element, {"target", "source"}, readerConfig::defMatchType);
        if (targetName.empty()) {
            targetObject = searchObject;
        } else {
            targetName = readerInformation.checkDefines(targetName);
            targetObject = locateObject(targetName, searchObject);
        }
        optObject = optimizationRoot->getOptData(targetObject);
        if (optObject != nullptr)  // check for retyping
        {
            // if we need to do a type override
            auto optMode = getElementField(element, "retype", readerConfig::defMatchType);
            if (!optMode.empty()) {
                optMode = readerInformation.checkDefines(optMode);
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
            objectType = readerInformation.checkDefines(objecttype);
            makeLowerCase(objectType);
        } else {
            WARNPRINT(READER_WARN_IMPORTANT, "economic object type must be specified ");
            return nullptr;
        }
        std::string mode = getElementField(element, "mode", readerConfig::defMatchType);
        if (mode.empty()) {
            optObject = optObjectFactory->createObject(objectType);
        } else {
            mode = readerInformation.checkDefines(mode);
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
            optMode = readerInformation.checkDefines(optMode);
            makeLowerCase(optMode);
            optObject = optObjectFactory->createObject(optMode, targetObject);
            if (optObject == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown economic mode " << optMode);
            }
        }
        std::string refName = getElementField(element, "ref", readerConfig::defMatchType);
        if (!refName.empty()) {
            ename = readerInformation.checkDefines(refName);
            obj = readerInformation.makeLibraryObject(ename, optObject);
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
        ename = readerInformation.checkDefines(ename);
        if (readerInformation.prefix.empty()) {
            optObject->setName(ename);
        } else {
            optObject->setName(readerInformation.prefix + '_' + ename);
        }
    }
    // locate a parent if any
    ename = getElementField(element, "parent", readerConfig::defMatchType);
    if (!ename.empty()) {
        ename = readerInformation.checkDefines(ename);
        if (optObject->isRoot()) {
            obj = locateObject(ename, searchObject);

            if (obj != nullptr) {
                if (dynamic_cast<gridOptObject*>(obj) != nullptr) {
                    parentOptObject = static_cast<gridOptObject*>(obj);
                } else {
                    parentOptObject = optimizationRoot->getOptData(obj);
                }
                if (parentOptObject == nullptr) {
                    parentOptObject = optimizationRoot->makeOptObjectPath(obj);
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
            parentOptObject = optimizationRoot->makeOptObjectPath(targetObject->getParent());
            addToParent(optObject, parentOptObject);
        } else {
            // set the base power to the system default (usually used for library objects
            optObject->set("basepower", readerInformation.base);
        }
    }

    // properties from link attributes

    objSetAttributes(optObject, element, "econ", readerInformation, econIgnoreElements());
    loadSubObjects(element, readerInformation, optObject);

    // get all element fields
    paramLoopElement(
        optObject, element, "econ", readerInformation, econIgnoreElements());

    LEVELPRINT(READER_NORMAL_PRINT, "loaded econ data " << optObject->getName());

    readerInformation.closeScope(riScope);
    return optObject;
}
#endif
}  // namespace griddyn
