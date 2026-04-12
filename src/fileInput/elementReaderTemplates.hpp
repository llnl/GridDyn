/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreExceptions.h"
#include "core/objectFactory.hpp"
#include "core/objectInterpreter.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "readElement.h"
#include "readerHelper.h"
#include <string>

namespace griddyn {
const IgnoreListType emptyIgnoreList{};

template<class COMPONENT>
void loadParentInfo(std::shared_ptr<readerElement>& element,
                    COMPONENT* mobj,
                    readerInfo& ri,
                    coreObject* parentObject)
{
    coreObject* newParentObject = getParent(element, ri, parentObject, parentSearchComponent(mobj));
    if (newParentObject) {
        if (mobj->isRoot()) {
            addToParent(mobj, newParentObject);
        } else if (!isSameObject(mobj->getParent(), newParentObject)) {
            WARNPRINT(READER_WARN_IMPORTANT,
                      "Parent " << newParentObject->getName() << " specified for "
                                << mobj->getName() << " even though it already has a parent");
        }
    }

    if (mobj->isRoot()) {
        if (parentObject) {
            addToParent(mobj, parentObject);
        } else {
            // set the base power to the system default (usually used for library objects
            mobj->set("basepower", ri.base);
        }
    }
}

template<class COMPONENT>
coreObject* updateSearchObject(std::shared_ptr<readerElement>& element,
                               readerInfo& ri,
                               coreObject* parentObject)
{
    coreObject* alternateObject =
        getParent(element,
                  ri,
                  parentObject,
                  parentSearchComponent(static_cast<COMPONENT*>(nullptr)));

    return (alternateObject) ? alternateObject : parentObject;
}

const stringVec NumandIndexNames{"number", "index"};

const stringVec typeandRetype{"type", "retype"};

template<class COMPONENT>
COMPONENT* locateObjectFromElement(std::shared_ptr<readerElement>& element,
                                   const std::string& component,
                                   readerInfo& ri,
                                   coreObject* searchObject)
{
    coreObject* obj;
    // try to locate the object if it exists already
    std::string ename = getElementField(element, "name", readerConfig::defMatchType);
    if (!ename.empty()) {
        // check if the object can be found in the current search object
        ename = ri.checkDefines(ename);
        if ((obj = searchObject->find(ename)) != nullptr) {
            return dynamic_cast<COMPONENT*>(obj);
        }
    }

    ename = getElementFieldOptions(element, NumandIndexNames, readerConfig::defMatchType);
    if (!ename.empty()) {
        // check if the object can be found in the current search object
        double val = interpretString(ename, ri);
        auto index = static_cast<index_t>(val);
        if ((obj = searchObject->getSubObject(component, index)) != nullptr) {
            return dynamic_cast<COMPONENT*>(obj);
        }
    }

    ename = getElementField(element, "id", readerConfig::defMatchType);
    if (!ename.empty()) {
        // check if the object can be found in the current search object
        double val = interpretString(ename, ri);
        auto index = static_cast<index_t>(val);
        if ((obj = searchObject->findByUserID(component, index)) != nullptr) {
            return dynamic_cast<COMPONENT*>(obj);
        }
    }
    if (component == "extra") {
        if ((obj = searchObject->find(element->getName())) != nullptr) {
            return dynamic_cast<COMPONENT*>(obj);
        }
    }
    return nullptr;
}

template<class COMPONENT>
COMPONENT* buildObject(std::shared_ptr<readerElement>& element,
                       COMPONENT* mobj,
                       const std::string& component,
                       readerInfo& ri,
                       coreObject* searchObject)
{
    auto cof = coreObjectFactory::instance();
    auto tf = cof->getFactory(component);
    auto objectName = getObjectName(element, ri);
    bool preexist = (mobj != nullptr);
    if (mobj == nullptr) {
        // check if type is explicit
        std::string valType =
            getElementFieldOptions(element, typeandRetype, readerConfig::defMatchType);
        if (!valType.empty()) {
            valType = ri.checkDefines(valType);
            gmlc::utilities::makeLowerCase(valType);
            if (!tf->isValidType(valType)) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unknown " << component << " type (" << valType
                                     << ") reverting to default type ");
            }
            if (objectName.empty()) {
                mobj = dynamic_cast<COMPONENT*>(tf->makeObject(valType));
            } else {
                mobj = dynamic_cast<COMPONENT*>(tf->makeObject(valType, objectName));
            }
            if (mobj == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown " << component << " type " << valType);
            }
        } else {  // check if there was a type in the element name
            valType = element->getName();
            if (valType != component) {
                if (objectName.empty()) {
                    mobj = dynamic_cast<COMPONENT*>(tf->makeObject(valType));
                } else {
                    mobj = dynamic_cast<COMPONENT*>(tf->makeObject(valType, objectName));
                }
                if (mobj == nullptr) {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "unknown " << component << " type " << valType);
                }
            }
        }
    } else {
        // if we need to do a type override

        std::string valType = getElementField(element, "retype", readerConfig::defMatchType);
        if (!valType.empty()) {
            valType = ri.checkDefines(valType);
            gmlc::utilities::makeLowerCase(valType);
            if (!tf->isValidType(valType)) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unknown " << component << " retype reverting to default type"
                                     << valType);
            }
            auto rtObj = dynamic_cast<COMPONENT*>(tf->makeObject(valType, mobj->getName()));

            if (rtObj == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT, "unknown " << component << " retype " << valType);
            } else {
                // the reference count in the object needs to be incremented so we are in control of
                // the object deletion,  the remove call will (most likely) call delete but in case
                // it doesn't we still need access to it here to ensure it gets deleted by the call
                // to remove ref,  otherwise we either run the risk of a memory leak or
                // dereferencing an invalid pointer, so we just claim ownership for a short time to
                // alleviate the issue.
                mobj->clone(rtObj);
                mobj->addOwningReference();
                searchObject->remove(mobj);
                removeReference(mobj, searchObject);  // decrement the reference again
                searchObject->add(rtObj);  // add the new object back into the searchObject
                mobj = rtObj;  // move the pointer to the new object
            }
        }
    }

    if (!preexist) {  // check for library references if the object didn't exist before
        std::string ename = getElementField(element, "ref", readerConfig::defMatchType);
        if (!ename.empty()) {
            ename = ri.checkDefines(ename);
            auto obj = ri.makeLibraryObject(ename, mobj);
            if (obj == nullptr) {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "unable to locate reference object " << ename << " in library");
            } else {
                mobj = dynamic_cast<COMPONENT*>(obj);
                if (mobj == nullptr) {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Invalid reference object " << ename << ": wrong type");
                    removeReference(obj);
                } else {
                    if (!objectName.empty()) {
                        mobj->setName(objectName);
                    }
                }
            }
        }
    }

    // make the default object of component
    if (mobj == nullptr) {
        if (objectName.empty()) {
            mobj = dynamic_cast<COMPONENT*>(tf->makeObject());
        } else {
            mobj = dynamic_cast<COMPONENT*>(tf->makeObject("", objectName));
        }
        if (mobj == nullptr) {
            WARNPRINT(READER_WARN_IMPORTANT, "Unable to create object " << component);
            return nullptr;
        }
    }
    return mobj;
}

template<class COMPONENT>
COMPONENT* ElementReaderSetup(std::shared_ptr<readerElement>& element,
                              COMPONENT* mobj,
                              const std::string& component,
                              readerInfo& ri,
                              coreObject* searchObject)
{
    loadDefines(element, ri);
    loadDirectories(element, ri);
    searchObject = updateSearchObject<COMPONENT>(element, ri, searchObject);
    if ((mobj == nullptr) && (searchObject)) {
        mobj = locateObjectFromElement<COMPONENT>(element, component, ri, searchObject);
    }
    mobj = buildObject(element, mobj, component, ri, searchObject);

    setIndex(element, mobj, ri);

    loadParentInfo(element, mobj, ri, searchObject);
    // locate a parent if any

    return mobj;
}

template<class COMPONENT>
COMPONENT* ElementReader(std::shared_ptr<readerElement>& element,
                         COMPONENT* mobj,
                         const std::string& component,
                         readerInfo& ri,
                         coreObject* searchObject)
{
    auto riScope = ri.newScope();

    mobj = ElementReaderSetup(element, mobj, component, ri, searchObject);

    loadElementInformation(mobj, element, component, ri, emptyIgnoreList);

    ri.closeScope(riScope);
    return mobj;
}

}  // namespace griddyn
