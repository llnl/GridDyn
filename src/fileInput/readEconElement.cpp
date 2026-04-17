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
static const IgnoreListType econIgnoreElements{"mode", "objecttype", "retype", "parent"};

#ifndef ENABLE_OPTIMIZATION_LIBRARY
coreObject* readEconElement(std::shared_ptr<readerElement>& /*element*/,
                            readerInfo& /*ri*/,
                            coreObject* searchObject)
{
    return searchObject;
}

#else
coreObject* readEconElement(std::shared_ptr<readerElement>& element,
                            readerInfo& ri,
                            coreObject* searchObject)
{
    // get the optimization root
    auto gdo = dynamic_cast<gridDynOptimization*>(searchObject->getRoot());
    if (gdo == nullptr)  // there is no optimization engine defined so ignore the economic data
    {
        return nullptr;
    }
    gridOptObject* oo = nullptr;
    std::string targetname;
    auto riScope = ri.newScope();
    // run the boilerplate code to setup the object
    // lnk = XMLReaderSetup(aP, lnk, "econ", ri, searchObject);
    std::string objectType;
    std::string ename;
    auto coof = coreOptObjectFactory::instance();
    coreObject* obj;
    coreObject* targetObject = nullptr;
    gridOptObject* parentOo = nullptr;

    loadDefines(element, ri);
    loadDirectories(element, ri);
    if (searchObject != nullptr) {
        targetname =
            getElementFieldOptions(element, {"target", "source"}, readerConfig::defMatchType);
        if (targetname.empty()) {
            targetObject = searchObject;
        } else {
            targetname = ri.checkDefines(targetname);
            targetObject = locateObject(targetname, searchObject);
        }
        oo = gdo->getOptData(targetObject);
        if (oo != nullptr)  // check for retyping
        {
            // if we need to do a type override
            auto ooMode = getElementField(element, "retype", readerConfig::defMatchType);
            if (!ooMode.empty()) {
                ooMode = ri.checkDefines(ooMode);
                makeLowerCase(ooMode);
                gridOptObject* rtObj = coof->createObject(ooMode, targetObject);
                if (rtObj == nullptr) {
                    WARNPRINT(READER_WARN_IMPORTANT, "unknown economic retype " << ooMode);
                } else {
                    // TODO(phlpt): This isn't quite right yet.
                    oo->clone(rtObj);
                    oo->getParent()->remove(oo);
                    delete oo;
                    searchObject->add(rtObj);
                    oo = rtObj;
                }
            }
        }
    } else {
        std::string objecttype =
            getElementFieldOptions(element, {"objecttype", "type"}, readerConfig::defMatchType);
        if (!objecttype.empty()) {
            objectType = ri.checkDefines(objectType);
            makeLowerCase(objectType);
        } else {
            WARNPRINT(READER_WARN_IMPORTANT, "economic object type must be specified ");
            return nullptr;
        }
        std::string mode = getElementField(element, "mode", readerConfig::defMatchType);
        if (mode.empty()) {
            oo = coof->createObject(objectType);
        } else {
            mode = ri.checkDefines(mode);
            makeLowerCase(mode);
            oo = coof->createObject(mode, objectType);
            if (oo == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown economic mode " << mode);
            }
        }
    }

    if (oo == nullptr) {
        std::string ooMode =
            getElementFieldOptions(element, {"mode", "retype"}, readerConfig::defMatchType);
        if (!ooMode.empty()) {
            ooMode = ri.checkDefines(ooMode);
            makeLowerCase(ooMode);
            oo = coof->createObject(ooMode, targetObject);
            if (oo == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown economic mode " << ooMode);
            }
        }
        std::string refName = getElementField(element, "ref", readerConfig::defMatchType);
        if (!refName.empty()) {
            ename = ri.checkDefines(ename);
            obj = ri.makeLibraryObject(ename, oo);
            if (obj == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unable to locate reference object " << ename << " in library");
            } else {
                oo = dynamic_cast<gridOptObject*>(obj);
                if (oo == nullptr) {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Invalid reference object " << ename << ": wrong type");
                    delete obj;
                }
            }
        }
    }

    // check for library references

    if (oo == nullptr) {
        oo = coof->createObject(targetObject);
        if (oo == nullptr) {
            WARNPRINT(READER_WARN_IMPORTANT, "Unable to create object ");
            return nullptr;
        }
    }
    ename = getElementField(element, "name", readerConfig::defMatchType);
    if (!ename.empty()) {
        ename = ri.checkDefines(ename);
        if (ri.prefix.empty()) {
            oo->setName(ename);
        } else {
            oo->setName(ri.prefix + '_' + ename);
        }
    }
    // locate a parent if any
    ename = getElementField(element, "parent", readerConfig::defMatchType);
    if (!ename.empty()) {
        ename = ri.checkDefines(ename);
        if (oo->isRoot()) {
            obj = locateObject(ename, searchObject);

            if (obj != nullptr) {
                if (dynamic_cast<gridOptObject*>(obj) != nullptr) {
                    parentOo = static_cast<gridOptObject*>(obj);
                } else {
                    parentOo = gdo->getOptData(obj);
                }
                if (parentOo == nullptr) {
                    parentOo = gdo->makeOptObjectPath(obj);
                }
                addToParent(oo, parentOo);
            }
        } else {
            WARNPRINT(READER_WARN_IMPORTANT,
                      "Parent " << ename << "specified for " << oo->getName()
                                << " even though it already has a parent");
        }
    } else if (oo->isRoot()) {
        if ((targetObject != nullptr) && (targetObject->getParent() != nullptr)) {
            parentOo = gdo->makeOptObjectPath(targetObject->getParent());
            addToParent(oo, parentOo);
        } else {
            // set the base power to the system default (usually used for library objects
            oo->set("basepower", ri.base);
        }
    }

    // properties from link attributes

    objSetAttributes(oo, element, "econ", ri, econIgnoreElements);
    loadSubObjects(element, ri, oo);

    // get all element fields
    paramLoopElement(oo, element, "econ", ri, econIgnoreElements);

    LEVELPRINT(READER_NORMAL_PRINT, "loaded econ data " << oo->getName());

    ri.closeScope(riScope);
    return oo;
}
#endif
}  // namespace griddyn
