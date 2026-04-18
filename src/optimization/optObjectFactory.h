/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridOptObjects.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <functional>

namespace griddyn {
// class definitions for the object factories that can create the objects
// cFactory is a virtual base class for object Construction functions
class optFactory {
  public:
    std::string name;
    int m_level = 0;
    optFactory(std::string_view component, std::string_view objName, int level = 0):
        name(objName), m_level(level)
    {
    }
    optFactory(const stringVec& component, std::string_view objName, int level = 0):
        name(objName), m_level(level)
    {
    }
    virtual gridOptObject* makeObject(coreObject* obj) = 0;
    virtual gridOptObject* makeObject() = 0;
    virtual void prepObjects(count_t /*count*/, coreObject* /*obj*/) {}
    virtual count_t remainingPrepped() const { return 0; }
    virtual bool testObject(coreObject*) { return true; }
};

using optMap = std::map<std::string, optFactory*, std::less<>>;

class optComponentFactory {
  public:
    std::string name;
    optComponentFactory() {}
    optComponentFactory(std::string_view typeName);
    ~optComponentFactory();
    stringVec getObjNames();
    gridOptObject* makeObject(coreObject* obj);
    gridOptObject* makeObject(std::string_view objType);
    gridOptObject* makeObject();
    void registerFactory(optFactory* optFac);
    bool isValidObject(std::string_view objName);
    optFactory* getFactory(std::string_view typeName);

  protected:
    optMap m_factoryMap;
    std::vector<optFactory*> m_factoryList;
};

// create a high level object factory for the coreObject class
using optfMap = std::map<std::string, std::shared_ptr<optComponentFactory>, std::less<>>;

class coreOptObjectFactory {
  public:
    /** public destructor
     * Destructor must be public to work with shared_ptr
     */
    ~coreOptObjectFactory() {}
    static std::shared_ptr<coreOptObjectFactory> instance();
    void registerFactory(std::string_view name, std::shared_ptr<optComponentFactory> componentFac);
    void registerFactory(std::shared_ptr<optComponentFactory> componentFac);
    stringVec getFactoryNames();
    stringVec getObjNames(std::string_view factoryName);
    gridOptObject* createObject(std::string_view optType, std::string_view typeName);
    gridOptObject* createObject(std::string_view optType, coreObject* obj);
    gridOptObject* createObject(coreObject* obj);
    gridOptObject* createObject(std::string_view typeName);
    std::shared_ptr<optComponentFactory> getFactory(std::string_view factoryName);
    bool isValidType(std::string_view obComponent);
    bool isValidObject(std::string_view optType, std::string_view objName);
    void setDefaultType(std::string_view defType);
    void prepObjects(std::string_view optType,
                     std::string_view typeName,
                     count_t numObjects,
                     coreObject* baseObj);
    void prepObjects(std::string_view typeName, count_t numObjects, coreObject* baseObj);

  private:
    coreOptObjectFactory() {}

    optfMap m_factoryMap;
    std::string m_defaultType;
};

/*** template class for opt object ownership*/
template<class Ntype, class gdType>
class gridOptObjectHolder: public coreObject {
    static_assert(std::is_base_of<gridOptObject, Ntype>::value,
                  "opt class must have a base class of gridOptObject");
    static_assert(std::is_base_of<coreObject, gdType>::value,
                  "gridDyn class must have base class type of coreObject");

  private:
    std::vector<Ntype> objArray;
    count_t next = 0;
    count_t objCount = 0;

  public:
    gridOptObjectHolder(count_t objs): objArray(objs), objCount(objs)
    {
        for (auto& so : objArray) {
            so.addOwningReference();
        }
    }
    Ntype* getNext()
    {
        Ntype* obj = nullptr;
        if (next < objCount) {
            obj = &(objArray[next]);
            ++next;
        }
        return obj;
    }

    count_t remaining() const { return objCount - next; }
};

// opt factory is a template class that inherits from cFactory to actually to the construction of a
// specific object
template<class Ntype, class gdType>
class optObjectFactory: public optFactory {
    static_assert(std::is_base_of<gridOptObject, Ntype>::value,
                  "opt class must have a base class of gridOptObject");
    static_assert(std::is_base_of<coreObject, gdType>::value,
                  "gridDyn class must have base class type of coreObject");

  private:
    bool useBlock = false;
    gridOptObjectHolder<Ntype, gdType>* gOOH = nullptr;

  public:
    optObjectFactory(std::string_view component,
                     std::string_view objName,
                     int level = 0,
                     bool makeDefault = false): optFactory(component, objName, level)
    {
        auto coof = coreOptObjectFactory::instance();
        auto fac = coof->getFactory(component);
        fac->registerFactory(this);
        if (makeDefault) {
            coof->setDefaultType(component);
        }
    }

    optObjectFactory(const stringVec& components,
                     const std::string& objName,
                     int level = 0,
                     bool makeDefault = false): optFactory(components[0], objName, level)
    {
        auto coof = coreOptObjectFactory::instance();
        for (auto& tname : components) {
            coof->getFactory(tname)->registerFactory(this);
        }
        if (makeDefault) {
            coof->setDefaultType(components[0]);
        }
    }

    bool testObject(coreObject* obj) override
    {
        if (dynamic_cast<gdType*>(obj)) {
            return true;
        } else {
            return false;
        }
    }

    gridOptObject* makeObject(coreObject* obj) override
    {
        gridOptObject* ret = nullptr;
        if (useBlock) {
            ret = gOOH->getNext();
            if (ret == nullptr) {  // means the block was used up
                useBlock = false;
                gOOH = nullptr;
            } else {
                ret->add(obj);
            }
        }
        if (ret == nullptr) {
            ret = new Ntype(obj);
        }
        return ret;
    }

    gridOptObject* makeObject() override
    {
        gridOptObject* ret = nullptr;
        if (useBlock) {
            ret = gOOH->getNext();
            if (ret == nullptr) {  // means the block was used up
                useBlock = false;
                gOOH = nullptr;
            }
        }
        if (ret == nullptr) {
            ret = new Ntype();
        }
        return ret;
    }

    Ntype* makeTypeObject(coreObject* obj)
    {
        Ntype* ret = nullptr;
        if (useBlock) {
            ret = gOOH->getNext();
            if (ret == nullptr) {  // means the block was used up
                useBlock = false;
                gOOH = nullptr;
            } else {
                ret->add(obj);
            }
        }
        if (ret == nullptr) {
            ret = new Ntype(obj);
        }
        return ret;
    }

    virtual void prepObjects(count_t count, coreObject* obj) override
    {
        auto root = obj->getRoot();
        gOOH = new gridOptObjectHolder<Ntype, gdType>(count);
        root->add(gOOH);
        useBlock = true;
    }
    virtual count_t remainingPrepped() const override { return (gOOH) ? (gOOH->remaining()) : 0; }
};

}  // namespace griddyn
